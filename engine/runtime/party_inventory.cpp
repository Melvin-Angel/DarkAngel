#include <darkangel/party_inventory.hpp>
#include <map>
#include <stdexcept>
#include <algorithm>
#include <type_traits>

namespace darkangel {
namespace {void require(bool b,const char* e){if(!b)throw std::invalid_argument(e);}}
struct PartyInventory::Impl {
    struct Record {
        Inventory inventory;std::uint64_t next_internal{1},highest_owner{};
        std::map<std::uint64_t,std::pair<InventoryRequest,InventoryReceipt>> receipts;
        explicit Record(StableId profile):inventory(profile){}
        InventoryReceipt apply(const ItemCatalog& catalog,InventoryRequest request){
            require(next_internal!=UINT64_MAX,"Inventory internal operation exhausted");request.operation=next_internal;
            auto receipt=inventory.apply(inventory.snapshot().profile,catalog,request);inventory.retire(next_internal++);return receipt;
        }
    };
    std::shared_ptr<const ItemCatalog> catalog;std::map<StableId,Record> profiles;
    explicit Impl(std::shared_ptr<const ItemCatalog> value):catalog(std::move(value)){require(bool(catalog),"Party inventory needs prepared item catalog");}
};
PartyInventory::PartyInventory(std::shared_ptr<const ItemCatalog> value):impl_(std::make_unique<Impl>(std::move(value))){}
PartyInventory::~PartyInventory()=default;
const ItemCatalog& PartyInventory::catalog()const{return *impl_->catalog;}
bool PartyInventory::contains(StableId profile)const{return impl_->profiles.contains(profile);}
void PartyInventory::join(StableId profile,std::span<const InventoryStartingItem> starting,std::array<AssetId,4> equipped){
    require(bool(profile)&&starting.size()<=128,"Starting inventory identity/bound");if(contains(profile))return;
    require(impl_->profiles.size()<64,"Party saved profile capacity");Impl::Record prepared(profile);
    for(const auto& item:starting){auto result=prepared.apply(catalog(),{0,InventoryMutation::Grant,item.item,0,item.quantity});require(result.failure==InventoryFailure::None,"Starting inventory does not fit");}
    for(unsigned slot=0;slot<4;++slot)if(equipped[slot]!=AssetId{}){
        const auto& state=prepared.inventory.snapshot();auto entry=std::find_if(state.entries.begin(),state.entries.end(),[&](const auto& e){return e.item==equipped[slot]&&std::find(state.equipped.begin(),state.equipped.end(),e.instance)==state.equipped.end();});
        require(entry!=state.entries.end(),"Starting equipment must be owned");auto result=prepared.apply(catalog(),{0,InventoryMutation::Equip,{},entry->instance,0,slot});require(result.failure==InventoryFailure::None,"Starting equipment must be a mask");
    }
    impl_->profiles.emplace(profile,std::move(prepared));
}
InventoryOwnerState PartyInventory::state(StableId profile)const{const auto& record=impl_->profiles.at(profile);return {record.inventory.snapshot(),record.highest_owner,{}};}
InventoryReceipt PartyInventory::grant(StableId profile,AssetId item,unsigned quantity){auto& record=impl_->profiles.at(profile);auto candidate=record;auto receipt=candidate.apply(catalog(),{0,InventoryMutation::Grant,item,0,quantity});receipt.operation=0;record=std::move(candidate);return receipt;}
unsigned PartyInventory::quantity(StableId profile,AssetId item)const{auto found=impl_->profiles.find(profile);return found==impl_->profiles.end()?0:found->second.inventory.quantity(item);}
InventoryReceipt PartyInventory::take(StableId profile,AssetId item,unsigned quantity){auto& record=impl_->profiles.at(profile);auto candidate=record;InventoryReceipt receipt{0,record.inventory.snapshot().revision,InventoryFailure::Missing};
    // Unequipped stacks of the item, newest first, until the quantity is taken; anything short leaves the inventory as it was.
    auto remaining=quantity;const auto& snapshot=record.inventory.snapshot();for(auto entry=snapshot.entries.rbegin();entry!=snapshot.entries.rend()&&remaining;++entry){if(entry->item!=item||std::find(snapshot.equipped.begin(),snapshot.equipped.end(),entry->instance)!=snapshot.equipped.end())continue;
        const auto count=std::min(remaining,entry->quantity);receipt=candidate.apply(catalog(),{0,InventoryMutation::Remove,{},entry->instance,count});if(receipt.failure!=InventoryFailure::None)return receipt;remaining-=count;}
    if(!quantity||remaining)return {0,record.inventory.snapshot().revision,InventoryFailure::Missing};receipt.operation=0;record=std::move(candidate);return receipt;}
InventoryOwnerState PartyInventory::request(StableId authenticated,const InventoryRequest& request,const InventoryEquipmentPublish& publish,bool masks_only){
    auto& record=impl_->profiles.at(authenticated);auto answer=[&](InventoryReceipt receipt){return InventoryOwnerState{record.inventory.snapshot(),record.highest_owner,receipt};};
    if(auto found=record.receipts.find(request.operation);found!=record.receipts.end())return answer(found->second.first==request?found->second.second:InventoryReceipt{request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid});
    // Exact next operation, or an identical retained retry. Reconnect starts from
    // the authoritative high-water mark; neither gaps nor retired IDs grant again.
    if(!request.operation||record.highest_owner==UINT64_MAX||request.operation!=record.highest_owner+1)return answer({request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid});
    auto candidate=record;InventoryReceipt receipt{request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid};
    if((!masks_only&&request.mutation==InventoryMutation::Consume)||request.mutation==InventoryMutation::Equip||request.mutation==InventoryMutation::Unequip){
        auto applied=candidate.apply(catalog(),request);receipt.revision=applied.revision;receipt.failure=applied.failure;
    }
    candidate.receipts.emplace(request.operation,std::pair{request,receipt});candidate.highest_owner=request.operation;
    if(candidate.receipts.size()>128)candidate.receipts.erase(candidate.receipts.begin());
    if(publish&&receipt.failure==InventoryFailure::None&&(request.mutation==InventoryMutation::Equip||request.mutation==InventoryMutation::Unequip||request.mutation==InventoryMutation::Consume)){
        const auto failure=publish(record.inventory.snapshot(),candidate.inventory.snapshot());
        if(failure!=InventoryFailure::None){candidate.inventory=record.inventory;receipt={request.operation,record.inventory.snapshot().revision,failure};candidate.receipts.at(request.operation).second=receipt;}
    }
    static_assert(std::is_nothrow_move_assignable_v<Impl::Record>);
    record=std::move(candidate);return answer(receipt);
}
std::vector<InventorySnapshot> PartyInventory::capture()const{std::vector<InventorySnapshot> result;for(const auto& [profile,record]:impl_->profiles)result.push_back(record.inventory.snapshot());return result;}
void PartyInventory::restore(std::span<const InventorySnapshot> snapshots){
    require(impl_->profiles.empty()&&snapshots.size()<=64,"Party inventory restore lifetime/bound");std::map<StableId,Impl::Record> candidate;
    for(const auto& snapshot:snapshots){Impl::Record record(snapshot.profile);record.inventory.restore(catalog(),snapshot);require(candidate.emplace(snapshot.profile,std::move(record)).second,"Duplicate saved inventory profile");}
    impl_->profiles=std::move(candidate);
}
}
