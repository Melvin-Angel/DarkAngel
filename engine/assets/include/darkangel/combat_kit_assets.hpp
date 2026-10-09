#pragma once
#include <darkangel/ability_assets.hpp>
#include <darkangel/graph_assets.hpp>
namespace darkangel {
// Ready for the existing WorldSession configure/equip boundary. Schema, input,
// optional grants and stance belong to one frozen content generation.
struct CookedCombatKit {
    std::shared_ptr<const CombatKitDefinition> definition;
    InputProfile input;
    AttributeAsset attributes;
    CookedGraph stance;
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    std::optional<TagAsset> tags;
};
CookedCombatKit load_cooked_combat_kit(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
