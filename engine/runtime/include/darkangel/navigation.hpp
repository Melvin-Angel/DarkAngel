#pragma once
#include <darkangel/actor_controller.hpp>
#include <darkangel/collision_asset.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace darkangel {
// Agent and voxel parameters in metres/degrees. Defaults fit the standing character motor.
struct NavigationSettings {double cell_size{.2},cell_height{.2},agent_height{1.8},agent_radius{.4},agent_climb{.35},slope_degrees{45};};
// Recast/Detour adapter: one static navigation mesh built from a scene's static collision
// (boxes and triangle meshes). Server-side decision support only; movement stays with the
// motor, so a path is advice a controller turns into ordinary move requests.
class NavigationMesh {
public:
    explicit NavigationMesh(const CollisionDefinition&,const NavigationSettings& = {});
    ~NavigationMesh();
    NavigationMesh(const NavigationMesh&)=delete;
    NavigationMesh& operator=(const NavigationMesh&)=delete;
    // Corner points from the nearest walkable point at `from` to the one at `to`, both
    // included. Empty when either end is off the mesh or no complete path exists.
    std::optional<std::vector<MotorVec>> path(MotorVec from,MotorVec to,unsigned limit=64)const;
    unsigned polygons()const;
    // Path queries answered so far and the time they took, for profiling.
    struct Usage {std::uint64_t queries{};double microseconds{};};
    Usage usage()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// A controller that walks its actor along the navigation mesh to its opponent and, once
// within `arrive` metres, presses the attack slot no more often than the attack interval.
// It asks only for move, face and a slot press, like any controller behind the seam.
struct PathFollow {ScriptedAttack attack;double arrive{1.4};unsigned repath_ticks{15};};
std::shared_ptr<ActorController> make_path_follower(std::shared_ptr<const NavigationMesh>,PathFollow);
}
