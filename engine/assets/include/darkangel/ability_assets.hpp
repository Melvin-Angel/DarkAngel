#pragma once
#include <darkangel/ability.hpp>
#include <filesystem>
namespace darkangel {
enum class AttributeVisibility {Server,Owner,Public};
struct AttributeAssetField {AttributeDefinition definition;std::string unit;AttributeVisibility visibility{};};
struct AttributeAsset {
    AssetId id;std::string generation;std::vector<AttributeAssetField> fields;
    std::vector<AttributeDefinition> definitions()const;
};
struct CookedAbility {
    std::shared_ptr<const AbilityDefinition> definition;
    AttributeAsset attributes;
};
// Loads only verified registry/CAS products. Source locations never enter runtime.
AttributeAsset load_cooked_attributes(const std::filesystem::path&,const std::filesystem::path&,AssetId);
CookedAbility load_cooked_ability(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
