#include <darkangel/inventory.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool kind_name(std::string_view name){return !name.empty()&&name.size()<=32&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==name.npos;}
void shape(const InventorySnapshot& value){
    require(value.schema==1&&bool(value.profile)&&value.next_instance>0&&value.entries.size()<=128,"Inventory snapshot schema/identity/bound");
    std::set<std::uint64_t> ids;
    for(const auto& entry:value.entries)require(entry.instance&&entry.instance<value.next_instance&&entry.item!=AssetId{}&&entry.quantity&&entry.quantity<=1000000&&ids.insert(entry.instance).second,"Inventory entry identity/quantity");
    std::set<std::uint64_t> equipped;
    for(auto id:value.equipped)if(id)require(ids.contains(id)&&equipped.insert(id).second,"Inventory equipment identity");
}
}
ItemCatalog::ItemCatalog(){
    register_kind({"regular"});register_kind({"key"});register_kind({"material"});register_kind({"quest"});
    register_kind({"consumable",true});register_kind({"mask",false,true,true});
}
void ItemCatalog::register_kind(ItemKind kind){
    require(kind_name(kind.name)&&(!kind.mask||kind.equippable),"Invalid item classification");
    require(kinds_.emplace(kind.name,kind).second,"Duplicate item classification");
}
void ItemCatalog::insert(ItemDefinition item){
    auto kind=kinds_.find(item.classification);
    require(item.id!=AssetId{}&&!item.name.empty()&&item.name.size()<=128&&item.description.size()<=4096&&kind!=kinds_.end(),"Invalid item definition");
    require(item.stack_limit&&item.stack_limit<=1000000&&(!item.unique||item.stack_limit==1),"Invalid item quantity rules");
    require((!item.usable||kind->second.usable)&&(!item.equippable||kind->second.equippable),"Invalid item behaviour");
    require(kind->second.mask?(item.mask!=AssetId{}&&item.equippable&&item.stack_limit==1):(item.mask==AssetId{}),"Invalid mask reference");
    require(items_.emplace(item.id,std::move(item)).second,"Duplicate item definition");
}
const ItemDefinition& ItemCatalog::get(AssetId id)const{auto found=items_.find(id);require(found!=items_.end(),"Unknown inventory item");return found->second;}
Inventory::Inventory(StableId profile,unsigned capacity):capacity_(capacity){require(bool(profile)&&capacity&&capacity<=128,"Inventory owner/capacity");state_.profile=profile;}
unsigned Inventory::quantity(AssetId item)const{unsigned total{};for(const auto& entry:state_.entries)if(entry.item==item)total+=entry.quantity;return total;}
InventoryReceipt Inventory::apply(StableId authenticated,const ItemCatalog& catalog,const InventoryRequest& request){
    require(authenticated==state_.profile,"Inventory ownership mismatch");
    require(request.operation>retired_,"Retired inventory operation");
    if(auto found=receipts_.find(request.operation);found!=receipts_.end()){
        require(found->second.first==request,"Conflicting inventory operation");return found->second.second;
    }
    require(request.operation>highest_&&receipts_.size()<128,"Inventory operation order/receipt capacity");
    auto candidate=state_;auto failure=InventoryFailure::None;
    auto entry=std::find_if(candidate.entries.begin(),candidate.entries.end(),[&](const auto& value){return value.instance==request.instance;});
    auto equipped=[&](std::uint64_t id){return std::find(candidate.equipped.begin(),candidate.equipped.end(),id)!=candidate.equipped.end();};
    switch(request.mutation){
    case InventoryMutation::Grant:{
        const auto& definition=catalog.get(request.item);
        if(request.instance||request.slot||!request.quantity||request.quantity>1000000){failure=InventoryFailure::Invalid;break;}
        if(definition.unique&&(request.quantity!=1||quantity(request.item))){failure=InventoryFailure::Unique;break;}
        auto remaining=request.quantity;
        for(auto& stack:candidate.entries)if(stack.item==request.item){const auto add=std::min(remaining,definition.stack_limit-stack.quantity);stack.quantity+=add;remaining-=add;}
        while(remaining){
            if(candidate.entries.size()==capacity_||candidate.next_instance==UINT64_MAX){failure=InventoryFailure::Capacity;break;}
            const auto count=std::min(remaining,definition.stack_limit);candidate.entries.push_back({candidate.next_instance++,request.item,count});remaining-=count;
        }break;}
    case InventoryMutation::Remove:case InventoryMutation::Consume:
        if(request.item!=AssetId{}||request.slot||!request.quantity){failure=InventoryFailure::Invalid;break;}
        if(entry==candidate.entries.end()||entry->quantity<request.quantity){failure=InventoryFailure::Missing;break;}
        if(equipped(entry->instance)){failure=InventoryFailure::Equipped;break;}
        if(request.mutation==InventoryMutation::Consume&&!catalog.get(entry->item).usable){failure=InventoryFailure::NotUsable;break;}
        entry->quantity-=request.quantity;if(!entry->quantity)candidate.entries.erase(entry);break;
    case InventoryMutation::Equip:
        if(request.item!=AssetId{}||request.quantity||request.slot>=4){failure=InventoryFailure::Invalid;break;}
        if(entry==candidate.entries.end()){failure=InventoryFailure::Missing;break;}
        if(const auto& definition=catalog.get(entry->item);definition.mask==AssetId{}||!definition.equippable){failure=InventoryFailure::NotMask;break;}
        if(equipped(entry->instance)&&candidate.equipped[request.slot]!=entry->instance){failure=InventoryFailure::Equipped;break;}
        candidate.equipped[request.slot]=entry->instance;break;
    case InventoryMutation::Unequip:
        if(request.item!=AssetId{}||request.instance||request.quantity||request.slot>=4){failure=InventoryFailure::Invalid;break;}
        candidate.equipped[request.slot]=0;break;
    default:failure=InventoryFailure::Invalid;break;
    }
    if(failure==InventoryFailure::None){require(candidate.revision!=UINT64_MAX,"Inventory revision exhausted");++candidate.revision;shape(candidate);}
    const InventoryReceipt receipt{request.operation,failure==InventoryFailure::None?candidate.revision:state_.revision,failure};
    // Allocate the receipt before publishing state: allocation failure preserves the inventory.
    receipts_.emplace(request.operation,std::pair{request,receipt});highest_=request.operation;
    if(failure==InventoryFailure::None)state_=std::move(candidate);
    return receipt;
}
void Inventory::retire(std::uint64_t through){require(through>=retired_&&through<=highest_,"Inventory retirement range");receipts_.erase(receipts_.begin(),receipts_.upper_bound(through));retired_=through;}
void Inventory::restore(const ItemCatalog& catalog,const InventorySnapshot& value){
    require(highest_==0&&receipts_.empty()&&state_.revision==0,"Cannot restore an active inventory");shape(value);
    require(value.profile==state_.profile&&value.entries.size()<=capacity_,"Inventory restore owner/capacity");
    std::set<AssetId> unique;
    for(const auto& entry:value.entries){const auto& definition=catalog.get(entry.item);require(entry.quantity<=definition.stack_limit&&(!definition.unique||unique.insert(entry.item).second),"Inventory restore stack/unique rule");}
    for(auto id:value.equipped)if(id){auto found=std::find_if(value.entries.begin(),value.entries.end(),[&](const auto& entry){return entry.instance==id;});require(catalog.get(found->item).mask!=AssetId{}&&catalog.get(found->item).equippable,"Inventory restore mask rule");}
    auto candidate=value;state_=std::move(candidate);
}
std::string encode_inventory(const InventorySnapshot& value){
    shape(value);nlohmann::json entries=nlohmann::json::array();for(const auto& entry:value.entries)entries.push_back({{"instance",entry.instance},{"item",entry.item.text()},{"quantity",entry.quantity}});
    return nlohmann::json{{"schema",value.schema},{"profile",value.profile.text()},{"revision",value.revision},{"next_instance",value.next_instance},{"entries",entries},{"equipped",value.equipped}}.dump();
}
InventorySnapshot decode_inventory(std::string_view text){
    require(text.size()<=65536,"Inventory document bound");unsigned depth{};
    auto data=nlohmann::json::parse(text,[&](int level,nlohmann::json::parse_event_t,nlohmann::json&){require(level<=8,"Inventory document depth");return true;});
    require(data.is_object()&&data.size()==6&&data.at("entries").is_array()&&data.at("entries").size()<=128&&data.at("equipped").is_array()&&data.at("equipped").size()==4,"Inventory document shape");
    auto number=[](const nlohmann::json& value){require(value.is_number_unsigned(),"Inventory unsigned number required");return value.get<std::uint64_t>();};
    InventorySnapshot value;require(number(data.at("schema"))==1,"Inventory schema unsupported");value.profile=StableId::parse(data.at("profile").get<std::string>());value.revision=number(data.at("revision"));value.next_instance=number(data.at("next_instance"));
    for(const auto& entry:data.at("entries")){require(entry.is_object()&&entry.size()==3,"Inventory entry shape");const auto count=number(entry.at("quantity"));require(count<=1000000,"Inventory quantity bound");value.entries.push_back({number(entry.at("instance")),AssetId::parse(entry.at("item").get<std::string>()),unsigned(count)});}
    for(unsigned slot=0;slot<4;++slot)value.equipped[slot]=number(data.at("equipped")[slot]);shape(value);return value;
}
}
