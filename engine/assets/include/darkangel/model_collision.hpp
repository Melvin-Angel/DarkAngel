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

// A static model's own collision shape (SCENE_SCALE_PLAN.md phase 1): its meshes welded in model space, exactly as
// bake_model_collision welds a placed model, cooked once with the model as a "model-collision" product. A placement is this
// shape at the placement's transform. The product's identity derives from the model's, so sidecars need no new subasset.
constexpr std::size_t model_collision_vertex_limit=65536,model_collision_triangle_limit=65536;
AssetId model_collision_id(AssetId model);
ModelCollisionBake cook_model_collision(const RuntimeModel&);
std::string encode_model_collision(const CollisionMeshData&);
std::shared_ptr<const CollisionMeshData> decode_model_collision(std::string_view,AssetId runtime);
// The model's collision shape from a package registry; null when the model was imported with "collision": "none".
std::shared_ptr<const CollisionMeshData> load_cooked_model_collision(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId model);
}
