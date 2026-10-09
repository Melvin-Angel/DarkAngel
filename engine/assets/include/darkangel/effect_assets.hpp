#pragma once
#include <darkangel/ability_assets.hpp>
#include <darkangel/effects.hpp>
namespace darkangel {
struct CookedEffect {std::shared_ptr<const EffectDefinition> definition;AttributeAsset attributes;TagAsset tags;};
CookedEffect load_cooked_effect(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
