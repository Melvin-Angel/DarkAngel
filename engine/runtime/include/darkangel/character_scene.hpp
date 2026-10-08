#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/world_session.hpp>

namespace darkangel {
struct CharacterSceneInput {double x{},z{},yaw{};bool jump{},crouch{};};
// Local listen-host composition of the existing session, motor and graph.
// Resources are already validated/cooked. This adapter owns no transport codec.
// Combat/action replication and external-provider scene launch remain separate.
class CharacterSceneSession {
public:
    CharacterSceneSession(SessionHandshake,std::span<const ObjectData>,StableId player,
        const CollisionDefinition&,std::shared_ptr<const AnimationGraphPlan>,RigDefinition,std::string_view rig_archive);
    ~CharacterSceneSession();
    CharacterSceneSession(const CharacterSceneSession&)=delete;
    unsigned advance(double seconds,CharacterSceneInput);
    void step(CharacterSceneInput);
    void remove_collision(std::uint64_t);
    const World& presentation() const;
    const MotorState& motor() const;
    const MotorState& predicted_motor() const;
    const GraphState& graph() const;
    const std::vector<JointMatrix>& pose() const;
    std::size_t pending_prediction() const;
    unsigned resynchronizations() const;
    double debt() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
