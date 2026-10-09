#pragma once
#include <darkangel/character_scene.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/assembly.hpp>
#include <functional>
namespace darkangel::editor_app {
struct CharacterPreviewResources {AssetId skin;CookedRig rig;std::shared_ptr<const AnimationGraphPlan> graph;StableId player;std::optional<CharacterSceneCombat> combat;};
CharacterPreviewResources load_character_preview(const std::filesystem::path& registry,const std::filesystem::path& cas,std::string_view clips,const RuntimeSkinnedModel&,StableId player={},StableId target={});
std::pair<StableId,std::unique_ptr<CharacterSceneSession>> prepare_character_preview(const SpawnPlan&,const World&,const CharacterPreviewResources&,const std::function<RuntimeModel(AssetId)>&);
}
