#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/assembly.hpp>
#include <filesystem>
#include <functional>
#include <optional>

namespace darkangel::editor_app {
// A scene's static collision, baked from its placed models: each placed model (except the Player's skin and the models
// of pickups, altars, doors and platforms, which are not static) becomes one mesh body at its placement. The baked
// source lives next to the scene as "<scene>.dacollision", with an asset identity derived from the scene's, so every
// editor process of a party finds the same asset without a scene edit. A scene's "collision" component overrides it.
struct SceneCollisionFile {AssetId asset;std::filesystem::path source;std::string relative;};
SceneCollisionFile scene_collision_file(AssetId scene,const std::filesystem::path& scene_path,const std::filesystem::path& source_root);
// The collision a scene plays with: its component's profile, else its baked file when it exists, else none.
std::optional<AssetId> scene_collision(const SpawnPlan&,AssetId scene,const std::filesystem::path& scene_path,const std::filesystem::path& source_root);
// Bakes the scene's collision source from its current placements. Deterministic: the same placements give the same
// bytes (mesh body identities derive from the collision asset and the placed object). Empty when the scene has no
// static model to bake.
std::optional<std::string> bake_scene_collision(AssetId collision,const SpawnPlan&,const World&,AssetId skin,const std::function<RuntimeModel(AssetId)>& load);
// Writes the baked source when it differs from the file on disk; true when it changed.
bool publish_scene_collision(const SceneCollisionFile&,std::string_view source);
}
