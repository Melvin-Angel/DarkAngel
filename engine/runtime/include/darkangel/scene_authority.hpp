#pragma once
#include <darkangel/scene_definitions.hpp>
#include <darkangel/scene_seat.hpp>
#include <darkangel/inventory.hpp>

namespace darkangel {
struct AbilityCommitUpdate;
struct AbilityReceipt;
// The host half of a scene session: the server WorldSession, the authoritative physics world, the party members'
// characters, every server-driven actor, world events, interactables, trigger volumes and encounters. It reads nothing
// from a client but what arrives through a Transport, and a client reads nothing from it but what it publishes.
// In stage 1a it is stepped in lockstep: step(tick) requires every member's motor command for that tick.
class SceneAuthority {
public:
    SceneAuthority(SessionHandshake,std::span<const ObjectData>,std::span<const SceneSeat> party,const CollisionDefinition&,std::shared_ptr<const CharacterSceneCombat>,const SceneStart& = {},SceneTiming = SceneTiming::Lockstep);
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
    bool remove_member(StableId);
    // Independent timing: a member joins the running scene. Its avatar enters at the head of the next tick, in formation
    // around the first member; a seat that finds no place there is reported once by take_refused().
    void add_member(const SceneSeat&);
    std::vector<std::pair<StableId,std::string>> take_refused();
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
    // Host-only debug and gameplay grant into a member's persistent inventory (its profile). Empty without inventories.
    std::optional<InventoryReceipt> grant_item(StableId member,AssetId item,unsigned quantity);
    // The world view sent to the party (pickups, movers, altars, downed members, wipe), and movers' current box centres.
    const WorldStateView& world_state()const;
    // Tools and gates: takes all of a member's Health (it is downed, or the party wipes). False for a non-member.
    bool down_member(StableId member);
    std::vector<std::pair<StableId,MotorVec>> movers()const;
    std::uint64_t server_packets(StableId member)const;
    std::vector<std::uint64_t> server_packet_kinds(StableId member)const;
    const CharacterSceneProfile& profile()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
