#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
namespace darkangel {
struct CollisionTriangle {std::array<std::uint32_t,3> vertices{};std::uint32_t key{},material{};};
struct CollisionMeshData {AssetId runtime;std::string signature;std::vector<MotorVec> vertices;std::vector<CollisionTriangle> triangles;std::vector<std::string> materials;};
struct CollisionMeshDefinition {std::uint64_t id{};std::shared_ptr<const CollisionMeshData> data;};
void validate_collision_mesh(const CollisionMeshData&);
std::string collision_mesh_signature(const CollisionMeshData&);
struct CollisionDefinition {AssetId id;unsigned schema{};std::vector<CollisionBox> boxes;std::vector<CollisionMeshDefinition> meshes;};
// Source validation is shared by native authoring and cooking, without Jolt.
CollisionDefinition decode_collision_source(std::string_view);
}
