#pragma once
#include <darkangel/assets.hpp>
#include <string>
#include <utility>
#include <vector>
namespace darkangel {
// A cooked visual effect: the compiled Effekseer effect and the colour textures it names, each under the exact path
// the effect asks its texture loader for. generation is the product digest.
struct CookedVfx {AssetId id;std::string effect;std::vector<std::pair<std::string,CookedTexture>> textures;std::string generation;};
CookedVfx load_cooked_vfx(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
// What a compiled Effekseer effect (.efk) says it needs: its binary version and the resource paths by kind.
struct VfxDependencies {int version{};std::vector<std::string> color,normal,distortion,sound,model,material;};
VfxDependencies read_vfx_dependencies(std::string_view compiled);
// The Effekseer authoring tool the cooker runs to compile an effect source. Empty means the path this build was
// configured with (the pinned tool in .tools), or the DAE_EFFEKSEER_TOOL environment variable when that is set.
void set_effekseer_tool(std::filesystem::path);
std::filesystem::path effekseer_tool();
}
