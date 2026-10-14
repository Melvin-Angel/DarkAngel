#pragma once
#include <darkangel/party_inventory.hpp>
#include <darkangel/ability.hpp>

namespace darkangel {
// Frozen, scene-prepared definitions, not live ownership. NPCs retain direct kits.
struct SceneInventoryDefinition {
    std::shared_ptr<const ItemCatalog> catalog;
    std::vector<InventoryStartingItem> starting;
    std::array<AssetId,4> equipped{};unsigned active{};
    AbilityLoadout base;
    std::map<AssetId,AbilityLoadout> masks;
    std::string generation;
};
std::array<AssetId,4> inventory_masks(const InventorySnapshot&,const ItemCatalog&);
std::vector<std::optional<AbilityLoadout>> inventory_loadouts(const SceneInventoryDefinition&,std::array<AssetId,4>,unsigned& active);
}
