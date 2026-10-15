#include <darkangel/inventory_session.hpp>
#include <darkangel/world_session.hpp>
#include "session_wire.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>

namespace darkangel {
namespace {
using namespace session_detail;
constexpr std::uint32_t inventory_channel=1;
void require(bool b,const char* e){if(!b)throw std::invalid_argument(e);}
void item(Writer& w,AssetId value){for(auto byte:value.bytes)w.bytes.push_back(std::byte(byte));}
AssetId item(Reader& r){require(r.bytes.size()-r.at>=16,"Truncated inventory item");AssetId value;for(auto& byte:value.bytes)byte=std::to_integer<std::uint8_t>(r.bytes[r.at++]);return value;}
unsigned bounded(Reader& r,unsigned maximum){auto value=r.u64();require(value<=maximum,"Inventory wire number bound");return unsigned(value);}
std::vector<std::byte> encode_request(const InventoryRequest* request){
    Writer w;w.u64(1);w.u64(request?1:0);
    if(request){w.u64(request->operation);w.u64(unsigned(request->mutation));item(w,request->item);w.u64(request->instance);w.u64(request->quantity);w.u64(request->slot);}return w.bytes;
}
std::optional<InventoryRequest> decode_request(std::span<const std::byte> bytes){
    Reader r{bytes};require(r.u64()==1,"Inventory request schema");const auto type=r.u64();require(type<=1,"Inventory request type");std::optional<InventoryRequest> result;
    if(type){InventoryRequest request;request.operation=r.u64();request.mutation=InventoryMutation(bounded(r,unsigned(InventoryMutation::Unequip)));request.item=item(r);request.instance=r.u64();request.quantity=bounded(r,1000000);request.slot=bounded(r,3);result=request;}
    r.end();return result;
}
std::vector<std::byte> encode_state(const InventoryOwnerState& state){
    const auto& value=state.inventory;Writer w;w.u64(1);w.u64(2);w.u64(value.profile.high);w.u64(value.profile.low);w.u64(value.revision);w.u64(value.next_instance);w.u64(state.highest_operation);
    w.u64(state.receipt.operation);w.u64(state.receipt.revision);w.u64(unsigned(state.receipt.failure));w.u64(value.entries.size());
    for(const auto& entry:value.entries){w.u64(entry.instance);item(w,entry.item);w.u64(entry.quantity);}for(auto id:value.equipped)w.u64(id);return w.bytes;
}
InventoryOwnerState decode_state(std::span<const std::byte> bytes,const ItemCatalog& catalog,StableId profile){
    Reader r{bytes};require(r.u64()==1&&r.u64()==2,"Inventory state schema/type");InventoryOwnerState result;auto& value=result.inventory;value.profile={r.u64(),r.u64()};require(value.profile==profile,"Inventory snapshot owner mismatch");
    value.revision=r.u64();value.next_instance=r.u64();result.highest_operation=r.u64();result.receipt.operation=r.u64();result.receipt.revision=r.u64();result.receipt.failure=InventoryFailure(bounded(r,unsigned(InventoryFailure::Unavailable)));
    require((result.receipt.operation<=result.highest_operation||result.receipt.failure==InventoryFailure::Invalid)&&result.receipt.revision<=value.revision,"Inventory receipt revision/operation");const auto count=bounded(r,128);
    for(unsigned i=0;i<count;++i)value.entries.push_back({r.u64(),item(r),bounded(r,1000000)});for(auto& id:value.equipped)id=r.u64();r.end();
    Inventory validator(profile);validator.restore(catalog,value);return result;
}
}
struct InventoryHostSession::Impl {
    struct Binding {StableId profile;bool synced{};std::optional<std::pair<std::uint64_t,std::uint64_t>> sent;std::deque<InventoryOwnerState> answers;};
    WorldSession& session;PartyInventory& store;Resolve resolve;std::map<std::uint64_t,Binding> bindings;
    Impl(WorldSession& s,PartyInventory& value,Resolve handler):session(s),store(value),resolve(std::move(handler)){session.register_owner_channel(inventory_channel);}
};
InventoryHostSession::InventoryHostSession(WorldSession& s,PartyInventory& store,Resolve handler):impl_(std::make_unique<Impl>(s,store,std::move(handler))){}
InventoryHostSession::~InventoryHostSession()=default;
void InventoryHostSession::bind(std::uint64_t network,StableId profile){auto& p=*impl_;require(network&&p.store.contains(profile)&&p.bindings.size()<4&&!p.bindings.contains(network),"Inventory avatar/profile binding");for(const auto& [id,binding]:p.bindings)require(binding.profile!=profile,"Profile already controls another avatar");p.bindings.emplace(network,Impl::Binding{profile});}
void InventoryHostSession::unbind(std::uint64_t network){impl_->bindings.erase(network);}
void InventoryHostSession::pump(){
    auto& p=*impl_;
    for(const auto& message:p.session.drain_owner_messages(inventory_channel)){
        if(!p.session.attached(message.source))continue;
        try{
            require(p.bindings.contains(message.network),"Inventory request without profile binding");auto request=decode_request(message.bytes);auto& binding=p.bindings.at(message.network);
            require(binding.answers.size()<16,"Inventory answer backlog");
            // Construct a full answer before enqueuing. An identical retry recovers
            // the retained result if sending stalls after an authoritative commit.
            auto answer=request?(p.resolve?p.resolve(message.network,binding.profile,*request):p.store.request(binding.profile,*request)):p.store.state(binding.profile);
            binding.answers.push_back(std::move(answer));binding.synced=true;
        }catch(...){p.session.disconnect(message.source);}
    }
    for(auto& [network,binding]:p.bindings){
        if(!binding.synced)continue;
        if(!binding.answers.empty()){
            const auto& value=binding.answers.front();auto bytes=encode_state(value);
            if(p.session.send_owner_message(network,inventory_channel,bytes)){binding.sent={{value.inventory.revision,value.highest_operation}};binding.answers.pop_front();}
        }else{
            auto value=p.store.state(binding.profile);const auto version=std::pair{value.inventory.revision,value.highest_operation};
            if(binding.sent!=version&&p.session.send_owner_message(network,inventory_channel,encode_state(value)))binding.sent=version;
        }
    }
}
struct InventoryClientSession::Impl {
    WorldSession& session;StableId profile;std::shared_ptr<const ItemCatalog> catalog;std::uint64_t network{};bool synced{};
    std::optional<InventoryOwnerState> state;std::optional<InventoryRequest> pending;
    Impl(WorldSession& s,StableId avatar,StableId owner,std::shared_ptr<const ItemCatalog> definitions):session(s),profile(owner),catalog(std::move(definitions)){
        require(bool(owner)&&bool(catalog),"Inventory client profile/catalog");session.register_owner_channel(inventory_channel,avatar);
    }
};
InventoryClientSession::InventoryClientSession(WorldSession& s,StableId avatar,StableId profile,std::shared_ptr<const ItemCatalog> catalog):impl_(std::make_unique<Impl>(s,avatar,profile,std::move(catalog))){}
InventoryClientSession::~InventoryClientSession()=default;
void InventoryClientSession::pump(std::uint64_t network){
    auto& p=*impl_;require(!p.network||p.network==network,"Inventory client network identity changed within scene");if(!network)return;p.network=network;
    for(const auto& message:p.session.drain_owner_messages(inventory_channel)){
        try{
            require(message.network==network,"Inventory reply avatar mismatch");auto value=decode_state(message.bytes,*p.catalog,p.profile);
            require(!p.state||(value.inventory.revision>=p.state->inventory.revision&&value.highest_operation>=p.state->highest_operation),"Inventory snapshot moved backwards");
            if(p.pending&&value.receipt.operation==p.pending->operation)p.pending.reset();p.state=std::move(value);
        }catch(...){p.session.disconnect(message.source);throw;}
    }
    if(!p.synced&&p.session.send_owner_message(network,inventory_channel,encode_request(nullptr)))p.synced=true;
}
bool InventoryClientSession::submit(InventoryRequest request){auto& p=*impl_;if(!p.state||p.pending||request.operation||p.state->highest_operation==UINT64_MAX)return false;request.operation=p.state->highest_operation+1;
    if(!p.session.send_owner_message(p.network,inventory_channel,encode_request(&request)))return false;p.pending=request;return true;
}
bool InventoryClientSession::retry(){auto& p=*impl_;return p.pending&&p.session.send_owner_message(p.network,inventory_channel,encode_request(&*p.pending));}
const InventoryOwnerState* InventoryClientSession::state()const{return impl_->state?&*impl_->state:nullptr;}
bool InventoryClientSession::pending()const{return impl_->pending.has_value();}
}
