#pragma once
#include <darkangel/scene_definitions.hpp>

namespace darkangel {
// One party member's place in a scene session: its identity and the connection its client holds. The first seat is the
// scene's player object; a later seat's character is created at its position.
struct SceneSeat {StableId id;Transport* transport{};ConnectionHandle peer;MotorVec position{};double yaw{};};
// The host half of a scene session: the server WorldSession, the authoritative physics world, the party members'
// characters, every server-driven actor, world events, interactables, trigger volumes and encounters. It reads nothing
// from a client but what arrives through a Transport, and a client reads nothing from it but what it publishes.
// In stage 1a it is stepped in lockstep: step(tick) requires every member's motor command for that tick.
class SceneAuthority {
public:
    SceneAuthority(SessionHandshake,std::span<const ObjectData>,std::span<const SceneSeat> party,const CollisionDefinition&,std::shared_ptr<const CharacterSceneCombat>,const SceneStart& = {});
    ~SceneAuthority();
    SceneAuthority(const SceneAuthority&)=delete;
    // One authoritative fixed tick, from receiving the members' commands to publishing the tick's collision frame.
    void step(std::uint64_t tick);
    // One round of the server session's packet work.
    void pump();
    std::uint64_t tick()const;
    // Party travel a scene transition trigger raised on the latest tick, handed over once.
    std::optional<SceneTransitionRequest> take_transition();
    // Every persistent object of the scene into the world ledger, as an unload does before the scene is destroyed.
    void capture();
    void remove_collision(std::uint64_t);
    // Party members in seat order, and every character (members, then actors) by network identity.
    std::span<const StableId> party()const;
    std::vector<std::uint64_t> characters()const;
    std::optional<std::uint64_t> network(StableId member)const;
    const MotorState* motor(StableId member)const;
    std::span<const AbilityCommitUpdate> commitments()const;
    std::span<const StableId> actors()const;
    const CharacterSceneActor* actor(StableId)const;
    const AbilityReceipt* controller_receipt(StableId)const;
    std::string controller_status(StableId)const;
    std::optional<StableId> controller_target(StableId)const;
    const Perception* perception(StableId)const;
    void emit_world_event(std::string name);
    void refuse_next_activation(StableId member);
    const WorldEvents& world_events()const;
    std::span<const StableId> encounters()const;
    std::optional<EncounterStatus> encounter(StableId)const;
    std::optional<StableId> actor_encounter(StableId actor)const;
    std::optional<unsigned> trigger_fired(StableId)const;
    std::optional<unsigned> interactable_used(StableId)const;
    std::uint64_t server_packets(StableId member)const;
    std::vector<std::uint64_t> server_packet_kinds(StableId member)const;
    const CharacterSceneProfile& profile()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
