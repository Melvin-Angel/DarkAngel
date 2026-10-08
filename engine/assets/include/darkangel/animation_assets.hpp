#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/animation.hpp>
namespace darkangel {
struct CookedRig {RigDefinition definition;std::string archive;};
CookedRig load_cooked_rig(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
struct CookedClip {ClipDefinition definition;std::string archive;};
CookedClip load_cooked_clip(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
std::string load_cooked_human_binding(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
