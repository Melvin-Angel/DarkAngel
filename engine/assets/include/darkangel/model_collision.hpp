#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/world.hpp>
namespace darkangel {
struct ModelCollisionBake {std::shared_ptr<const CollisionMeshData> data;unsigned duplicate_faces{},degenerate_faces{};};
// Input is the existing verified, node-flattened RuntimeModel. Scene placement is
// applied once with the renderer's rotation/scale/pivot convention. No raw glTF.
ModelCollisionBake bake_model_collision(const RuntimeModel&,const Transform&,AssetId runtime);
std::string encode_collision_source(const CollisionDefinition&);
void publish_collision_source(const std::filesystem::path&,std::string_view);
}
