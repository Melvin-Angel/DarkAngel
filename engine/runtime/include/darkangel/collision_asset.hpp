#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
namespace darkangel {
struct CollisionTriangle {std::array<std::uint32_t,3> vertices{};std::uint32_t key{},material{};};
struct CollisionMeshData {AssetId runtime;std::string signature;std::vector<MotorVec> vertices;std::vector<CollisionTriangle> triangles;std::vector<std::string> materials;};
struct CollisionMeshDefinition {std::uint64_t id{};std::shared_ptr<const CollisionMeshData> data;};
// Wire collision state carries asset generation references, never SDK objects.
struct CollisionGeometryReference {std::uint64_t id{};AssetId runtime;std::string signature;};
struct CollisionStreamFrame {std::uint64_t tick{},topology{};std::vector<CollisionBox> boxes;std::vector<CollisionActor> actors;std::vector<CollisionGeometryReference> meshes;};
struct CollisionPresentationPose {std::uint64_t id{},epoch{};bool character{};MotorVec position{};std::array<double,4> rotation{0,0,0,1};bool crouched{};};
// Presentation data cannot be passed to the authoritative collision-frame loader.
struct CollisionPresentationFrame {double tick{};std::uint64_t topology{};bool held{};std::vector<CollisionPresentationPose> poses;};
class ObserverCollision {
public:void push(CollisionStreamFrame);CollisionPresentationFrame sample(double tick)const;std::size_t size()const{return frames_.size();}
private:std::deque<CollisionStreamFrame> frames_;
};
void validate_collision_stream(const CollisionStreamFrame&);
CollisionStreamFrame collision_stream(const CollisionFrame&);
CollisionFrame prepare_collision_frame(const CollisionStreamFrame&,std::span<const CollisionMesh> prepared_geometry);
void validate_collision_mesh(const CollisionMeshData&);
std::string collision_mesh_signature(const CollisionMeshData&);
struct CollisionDefinition {AssetId id;unsigned schema{};std::vector<CollisionBox> boxes;std::vector<CollisionMeshDefinition> meshes;};
// Source validation is shared by native authoring and cooking, without Jolt.
CollisionDefinition decode_collision_source(std::string_view);
}
