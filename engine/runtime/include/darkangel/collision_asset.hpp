#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
namespace darkangel {
struct CollisionTriangle {std::array<std::uint32_t,3> vertices{};std::uint32_t key{},material{};auto operator<=>(const CollisionTriangle&)const=default;};
struct CollisionMeshData {AssetId runtime;std::string signature;std::vector<MotorVec> vertices;std::vector<CollisionTriangle> triangles;std::vector<std::string> materials;};
struct CollisionMeshDefinition {std::uint64_t id{};std::shared_ptr<const CollisionMeshData> data;};
// Wire collision state carries the static layer's revision, never geometry or SDK objects: every participant derives the layer.
struct CollisionStreamFrame {std::uint64_t tick{},topology{};std::vector<CollisionBox> boxes;std::vector<CollisionActor> actors;std::string static_revision;};
struct CollisionPresentationPose {std::uint64_t id{},epoch{};bool character{};MotorVec position{};std::array<double,4> rotation{0,0,0,1};bool crouched{};};
// Presentation data cannot be passed to the authoritative collision-frame loader.
struct CollisionPresentationFrame {double tick{};std::uint64_t topology{};bool held{};std::vector<CollisionPresentationPose> poses;};
class ObserverCollision {
public:void push(CollisionStreamFrame);CollisionPresentationFrame sample(double tick)const;std::size_t size()const{return frames_.size();}
private:std::deque<CollisionStreamFrame> frames_;
};
void validate_collision_stream(const CollisionStreamFrame&);
CollisionStreamFrame collision_stream(const CollisionFrame&);
// Attaches the receiver's own static layer; a layer whose revision differs from the stream's is a mismatch naming both.
CollisionFrame prepare_collision_frame(const CollisionStreamFrame&,std::shared_ptr<const StaticCollisionLayer>);
void validate_collision_mesh(const CollisionMeshData&);
std::string collision_mesh_signature(const CollisionMeshData&);
// statics: placements' static bodies derived from the scene (SCENE_SCALE_PLAN.md phase 3), joined to the authored meshes on load.
struct CollisionDefinition {AssetId id;unsigned schema{};std::vector<CollisionBox> boxes;std::vector<CollisionMeshDefinition> meshes;std::shared_ptr<const StaticCollisionLayer> statics;};
// Source validation is shared by native authoring and cooking, without Jolt.
CollisionDefinition decode_collision_source(std::string_view);
}
