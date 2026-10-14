#pragma once
#include <darkangel/inventory.hpp>
#include <memory>
#include <span>

namespace darkangel {
struct InventoryStartingItem {AssetId item;unsigned quantity{};};
struct InventoryOwnerState {InventorySnapshot inventory;std::uint64_t highest_operation{};InventoryReceipt receipt;};
// Lives with the host party/save, outside scene and transport lifetimes. Only the
// authority adapter supplies profiles. Client requests cannot grant/remove items.
class PartyInventory {
public:
    explicit PartyInventory(std::shared_ptr<const ItemCatalog>);
    ~PartyInventory();
    PartyInventory(const PartyInventory&)=delete;
    void join(StableId profile,std::span<const InventoryStartingItem> starting={});
    bool contains(StableId profile)const;
    InventoryOwnerState state(StableId profile)const;
    InventoryReceipt grant(StableId profile,AssetId item,unsigned quantity);
    InventoryOwnerState request(StableId authenticated,const InventoryRequest&);
    // Versioned durable state; restore into a fresh party. Operation receipts stay
    // alive during travel/reconnect but are deliberately not slot-file state.
    std::vector<InventorySnapshot> capture()const;
    void restore(std::span<const InventorySnapshot>);
    const ItemCatalog& catalog()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
