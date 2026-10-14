#include <darkangel/scene_inventory.hpp>
#include <algorithm>
#include <stdexcept>

namespace darkangel {
std::array<AssetId,4> inventory_masks(const InventorySnapshot& value,const ItemCatalog& catalog){
    std::array<AssetId,4> result{};
    for(unsigned slot=0;slot<4;++slot)if(value.equipped[slot]){
        auto entry=std::find_if(value.entries.begin(),value.entries.end(),[&](const auto& e){return e.instance==value.equipped[slot];});
        if(entry==value.entries.end())throw std::runtime_error("Equipment instance missing");const auto& item=catalog.get(entry->item);if(!item.equippable||item.mask==AssetId{})throw std::runtime_error("Equipment item is not a mask");result[slot]=item.mask;
    }return result;
}
std::vector<std::optional<AbilityLoadout>> inventory_loadouts(const SceneInventoryDefinition& prepared,std::array<AssetId,4> masks,unsigned& active){
    if(active>=4)throw std::runtime_error("Equipment active slot");std::vector<std::optional<AbilityLoadout>> result(4);
    for(unsigned slot=0;slot<4;++slot)if(masks[slot]!=AssetId{})result[slot]=prepared.masks.at(masks[slot]);
    if(!result[active]){active=0;for(unsigned slot=0;slot<4;++slot)if(result[slot]){active=slot;break;}if(!result[active])result[active]=prepared.base;}
    return result;
}
}
