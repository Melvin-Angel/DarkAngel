#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace darkangel {
struct CollisionMeshData;
struct SpawnPlan;
// Static scene collision (docs/implementation/current/SCENE_SCALE_PLAN.md, "Design rules"). A layer is an immutable snapshot of
// the scene's static bodies: each a shared collision geometry at a placement. Physics worlds point at one layer; collision frames
// carry the layer they were captured with, and the wire carries only its revision. Static bodies never change inside a topology.
constexpr std::size_t static_collision_body_limit=16384;
// A scene object's static body identity: 62 bits of its StableId's digest with bit 62 set. Moving boxes and authored collision
// meshes use small ids and characters set bit 63, so the spaces never meet; Jolt BodyIDs never leave the physics world.
std::uint64_t static_body_id(StableId object);
constexpr bool is_scene_static_body(std::uint64_t id){return (id>>62)==1;}
struct StaticCollisionBody {
    std::uint64_t id{};
    StableId object; // the placement this body stands for; empty for authored collision meshes
    std::shared_ptr<const CollisionGeometry> geometry;
    MotorVec position{};
    std::array<double,4> rotation{0,0,0,1}; // x,y,z,w; normalized with a canonical sign by the layer
    double scale{1}; // uniform, in (0, 1000]
};
class StaticCollisionLayer {
public:
    // Validates, canonicalizes rotations, sorts by id and computes the revision. Throws on duplicate ids, missing geometry,
    // non-finite or out-of-range transforms, or more than static_collision_body_limit bodies.
    explicit StaticCollisionLayer(std::vector<StaticCollisionBody>);
    static const std::shared_ptr<const StaticCollisionLayer>& empty();
    std::span<const StaticCollisionBody> bodies()const{return bodies_;}
    const StaticCollisionBody* find(std::uint64_t id)const;
    // sha256 over the canonical encoding of every body in id order: identity, geometry runtime and signature, and the exact bits of
    // its transform. Equal scenes give equal revisions whatever the insertion order; visual-only edits never reach it.
    const std::string& revision()const{return revision_;}
    std::size_t unique_geometry()const;
    std::shared_ptr<const StaticCollisionLayer> without(std::uint64_t id)const;
private:
    std::vector<StaticCollisionBody> bodies_;std::string revision_;
};
// The renderer's rotation convention (DirectX roll-pitch-yaw) as a quaternion.
std::array<double,4> placement_rotation(double pitch,double yaw,double roll);
// The scene's static collision: one body per model placement, at the placement's transform, using the model's cooked collision
// shape. The player's skin, pickups, altars, doors and platforms are not static, and a model imported with "collision": "none"
// (geometry returns null) has no body. Every participant builds this from the same scene and models, so revisions agree.
std::shared_ptr<const StaticCollisionLayer> scene_static_collision(const SpawnPlan&,const World&,AssetId skin,
    const std::function<std::shared_ptr<const CollisionGeometry>(AssetId model)>& geometry);
// One CollisionGeometry (and Jolt shape) per distinct collision mesh, however many placements use it.
class CollisionGeometryCache {
public:
    std::shared_ptr<const CollisionGeometry> get(const std::shared_ptr<const CollisionMeshData>&);
    std::size_t builds()const{return builds_;}
private:
    std::map<std::pair<AssetId,std::string>,std::weak_ptr<const CollisionGeometry>> cache_;std::size_t builds_{};
};
}
