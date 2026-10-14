#include <darkangel/party_inventory.hpp>
#include <map>
#include <stdexcept>

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
void PartyInventory::join(StableId profile,std::span<const InventoryStartingItem> starting){
    require(bool(profile)&&starting.size()<=128,"Starting inventory identity/bound");if(contains(profile))return;
    require(impl_->profiles.size()<64,"Party saved profile capacity");Impl::Record prepared(profile);
    for(const auto& item:starting){auto result=prepared.apply(catalog(),{0,InventoryMutation::Grant,item.item,0,item.quantity});require(result.failure==InventoryFailure::None,"Starting inventory does not fit");}
    impl_->profiles.emplace(profile,std::move(prepared));
}
InventoryOwnerState PartyInventory::state(StableId profile)const{const auto& record=impl_->profiles.at(profile);return {record.inventory.snapshot(),record.highest_owner,{}};}
InventoryReceipt PartyInventory::grant(StableId profile,AssetId item,unsigned quantity){auto& record=impl_->profiles.at(profile);auto candidate=record;auto receipt=candidate.apply(catalog(),{0,InventoryMutation::Grant,item,0,quantity});receipt.operation=0;record=std::move(candidate);return receipt;}
InventoryOwnerState PartyInventory::request(StableId authenticated,const InventoryRequest& request){
    auto& record=impl_->profiles.at(authenticated);auto answer=[&](InventoryReceipt receipt){return InventoryOwnerState{record.inventory.snapshot(),record.highest_owner,receipt};};
    if(auto found=record.receipts.find(request.operation);found!=record.receipts.end())return answer(found->second.first==request?found->second.second:InventoryReceipt{request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid});
    // Exact next operation, or an identical retained retry. Reconnect starts from
    // the authoritative high-water mark; neither gaps nor retired IDs grant again.
    if(!request.operation||record.highest_owner==UINT64_MAX||request.operation!=record.highest_owner+1)return answer({request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid});
    auto candidate=record;InventoryReceipt receipt{request.operation,record.inventory.snapshot().revision,InventoryFailure::Invalid};
    if(request.mutation==InventoryMutation::Consume||request.mutation==InventoryMutation::Equip||request.mutation==InventoryMutation::Unequip){
        auto applied=candidate.apply(catalog(),request);receipt.revision=applied.revision;receipt.failure=applied.failure;
    }
    candidate.receipts.emplace(request.operation,std::pair{request,receipt});candidate.highest_owner=request.operation;
    if(candidate.receipts.size()>128)candidate.receipts.erase(candidate.receipts.begin());
    record=std::move(candidate);return answer(receipt);
}
std::vector<InventorySnapshot> PartyInventory::capture()const{std::vector<InventorySnapshot> result;for(const auto& [profile,record]:impl_->profiles)result.push_back(record.inventory.snapshot());return result;}
void PartyInventory::restore(std::span<const InventorySnapshot> snapshots){
    require(impl_->profiles.empty()&&snapshots.size()<=64,"Party inventory restore lifetime/bound");std::map<StableId,Impl::Record> candidate;
    for(const auto& snapshot:snapshots){Impl::Record record(snapshot.profile);record.inventory.restore(catalog(),snapshot);require(candidate.emplace(snapshot.profile,std::move(record)).second,"Duplicate saved inventory profile");}
    impl_->profiles=std::move(candidate);
}
}
