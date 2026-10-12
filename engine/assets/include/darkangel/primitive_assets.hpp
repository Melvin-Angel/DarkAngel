#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/presentation_primitives.hpp>
namespace darkangel {
// Every weapon trail and decal product of a verified cooked registry, in registry order.
std::vector<TrailDefinition> load_cooked_trails(const std::filesystem::path& registry,const std::filesystem::path& cas);
std::vector<DecalDefinition> load_cooked_decals(const std::filesystem::path& registry,const std::filesystem::path& cas);
}
