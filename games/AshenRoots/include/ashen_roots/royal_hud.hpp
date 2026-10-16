#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/ui_document.hpp>
#include <optional>
#include <span>
#include <string_view>
namespace ashen_roots {
// Game-owned mapping from confirmed Royal combat state to the HUD view model: which
// attributes are Health and Stamina and which tag means staggered are game rules.
darkangel::OwnerUiModel royal_hud(const darkangel::AbilityOwnerSnapshot& owner,std::span<const darkangel::AttributeDefinition> schema,std::optional<darkangel::Health> target,std::string_view mask={});
}
