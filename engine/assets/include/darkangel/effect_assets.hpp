#pragma once
#include <darkangel/ability_assets.hpp>
#include <darkangel/effects.hpp>
namespace darkangel {
struct TagAssetField {AssetId key;TagDefinition definition;};
struct TagAsset {
 AssetId id;std::string generation;std::vector<TagAssetField> fields;
 std::shared_ptr<const TagDictionary> dictionary()const;
};
struct CookedEffect {std::shared_ptr<const EffectDefinition> definition;AttributeAsset attributes;TagAsset tags;};
TagAsset load_cooked_tags(const std::filesystem::path&,const std::filesystem::path&,AssetId);
CookedEffect load_cooked_effect(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
