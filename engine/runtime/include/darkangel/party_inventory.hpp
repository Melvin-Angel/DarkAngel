#pragma once
#include <darkangel/inventory.hpp>
#include <memory>
#include <span>
#include <functional>

namespace darkangel {
struct InventoryOwnerState {InventorySnapshot inventory;std::uint64_t highest_operation{};InventoryReceipt receipt;};
// Refusal/exception must leave runtime state unchanged. Called after receipt
// allocation; success is followed by no-throw inventory publication.
using InventoryEquipmentPublish=std::function<InventoryFailure(const InventorySnapshot&,const InventorySnapshot&)>;
// Lives with the host party/save, outside scene and transport lifetimes. Only the
// authority adapter supplies profiles. Client requests cannot grant/remove items.
class PartyInventory {
public:
    explicit PartyInventory(std::shared_ptr<const ItemCatalog>);
    ~PartyInventory();
    PartyInventory(const PartyInventory&)=delete;
    void join(StableId profile,std::span<const InventoryStartingItem> starting={},std::array<AssetId,4> equipped={});
    bool contains(StableId profile)const;
    InventoryOwnerState state(StableId profile)const;
    InventoryReceipt grant(StableId profile,AssetId item,unsigned quantity);
    // Trusted removal of quantity of an item across its unequipped stacks; all or nothing.
    InventoryReceipt take(StableId profile,AssetId item,unsigned quantity);
    unsigned quantity(StableId profile,AssetId item)const;
    InventoryOwnerState request(StableId authenticated,const InventoryRequest&,const InventoryEquipmentPublish& publish={},bool masks_only=false);
    // Versioned durable state; restore into a fresh party. Operation receipts stay
    // alive during travel/reconnect but are deliberately not slot-file state.
    std::vector<InventorySnapshot> capture()const;
    void restore(std::span<const InventorySnapshot>);
    const ItemCatalog& catalog()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
