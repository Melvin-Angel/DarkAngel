#pragma once
#include <darkangel/actor_controller.hpp>
#include <darkangel/scene_inventory.hpp>
#include <darkangel/collision_asset.hpp>
// Private shared state; public ownership and tick order remain in the session facade.
#include <darkangel/scene_authority.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/inventory_session.hpp>
#include <darkangel/assembly.hpp>
#include "scene_shared.hpp"
#include "scene_world_objects.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iterator>
#include <set>


namespace darkangel {
using scene_detail::require;
struct SceneAuthority::Impl {
    // Pickups, altars and movers; the party's downed/revive/wipe state; the world view last sent to each member.
    std::unique_ptr<scene_detail::SceneWorldObjects> world_objects;std::unique_ptr<PartyLifecycleCoordinator> lifecycle;WorldStateView view_now;std::map<std::uint64_t,std::uint64_t> view_sent;bool respawn_raised{};
    WorldSession server;std::unique_ptr<PartyInventory> owned_inventory;PartyInventory* inventory_store{};std::unique_ptr<InventoryHostSession> inventory_session;
    SceneTiming timing;
    PhysicsWorld authoritative;
    // A party member: its character's authoritative motor and ability owner, the connection that owns it, and this tick's
    // accepted command and action motion. Kept in seat order, which is ascending network identity.
    struct Member {StableId identity,profile;std::uint64_t id{};ConnectionHandle peer;MotorVec foot{};double yaw{};std::unique_ptr<CharacterMotor> motor;AbilityOwnerHandle owner;MotorInput accepted,held;MotionRequest motion;AbilityOwnerSnapshot before;};
    // A server-driven actor: its scene configuration, authoritative motor and ability owner, the latest controller
    // press and chosen target. Kept in ascending network identity.
    struct Actor {StableId identity;std::uint64_t id{};const CharacterSceneActor* config{};MotorVec foot{};double yaw{};std::uint32_t faction{};std::unique_ptr<CharacterMotor> motor;AbilityOwnerHandle owner;std::uint64_t operation{1};std::optional<AbilityReceipt> receipt;std::optional<StableId> chosen,locked;std::unique_ptr<Perception> perception;std::optional<std::size_t> encounter;std::uint64_t died{};bool recorded{};double hold_x{},hold_z{};std::optional<double> hold_face;};
    std::shared_ptr<const CharacterSceneCombat> combat;std::vector<Member> members;std::vector<StableId> member_identities;std::vector<Actor> actors;std::vector<StableId> actor_identities;std::uint32_t player_faction{1};std::vector<SensedSubject> seen;std::vector<MotorInput> actor_commands;std::vector<MotionRequest> actor_motions;std::vector<AbilityOwnerSnapshot> actor_before;std::vector<char> actor_requested;
    // World events, trigger volumes and encounters; enemies an encounter spawned keep their configuration here.
    WorldEvents world;std::vector<WorldInteractable> interactables;std::vector<WorldTrigger> triggers;std::vector<EncounterDirector> directors;std::vector<StableId> encounter_identities;std::deque<CharacterSceneActor> spawned;struct Entering {std::size_t first{};EncounterSpawn second;unsigned attempts{};};std::deque<Entering> spawn_queue;std::size_t sense_cursor{},decision_cursor{};std::vector<char> turn;std::map<std::string,std::uint32_t> factions;
    std::vector<AbilityCommitUpdate> commitment_updates;
    // Scene transition triggers: a box entered by walking in fires once it has seen itself empty. travel is the party
    // travel raised on the latest tick, until its owner takes it. director_phases: each encounter's phase as last recorded.
    struct Portal {EncounterVolume volume;bool armed{};};std::vector<Portal> portals;std::optional<SceneTransitionRequest> travel;std::vector<EncounterPhase> director_phases;
    CharacterSceneProfile profile;
    // Characters one session holds beside the party.
    std::size_t actor_limit()const;
    bool living(std::uint64_t id)const;
    // The world ledger and this scene's half of a persistent identity; null when the session records nothing.
    WorldLedger* ledger{};
    PersistentId persistent(StableId object)const;
    // The one mechanism by which this scene's objects reach the ledger: each kind reports its logical properties with
    // their default policy, and takes them back before the first tick. Defaults per kind; per-object overrides come with
    // the Inspector's policy editor.
    struct InteractableState final:PersistentObject {WorldInteractable& item;explicit InteractableState(WorldInteractable& held):item(held){}
        std::string_view kind()const override{return "interactable";}
        void capture(LedgerWriter& out)const override{if(item.definition().once&&item.used())out.set("used",std::int64_t(item.used()),{LedgerScope::Save});}
        void restore(const LedgerReader& in)override{if(const auto used=in.integer("used");used&&*used>0)item.restore(unsigned(std::min<std::int64_t>(*used,1000000)));}};
    struct TriggerState final:PersistentObject {WorldTrigger& trigger;explicit TriggerState(WorldTrigger& held):trigger(held){}
        std::string_view kind()const override{return "trigger";}
        void capture(LedgerWriter& out)const override{if(trigger.definition().once&&trigger.fired())out.set("fired",std::int64_t(trigger.fired()),{LedgerScope::Save});}
        void restore(const LedgerReader& in)override{if(const auto fired=in.integer("fired");fired&&*fired>0)trigger.restore(unsigned(std::min<std::int64_t>(*fired,1000000)));}};
    // A completed encounter stays completed until a rest or a party wipe. One that had started is recorded as started and
    // starts again when its scene is built again: a half-fought encounter restarts, it is not resumed mid-wave.
    struct EncounterState final:PersistentObject {EncounterDirector& director;explicit EncounterState(EncounterDirector& held):director(held){}
        std::string_view kind()const override{return "encounter";}
        void capture(LedgerWriter& out)const override{const LedgerPolicy policy=director.definition().completed_policy;const auto phase=director.status().phase;if(phase==EncounterPhase::Complete)out.set("completed",true,policy);else if(phase==EncounterPhase::Running)out.set("started",true,policy);}
        void restore(const LedgerReader& in)override{if(in.flag("completed").value_or(false))director.restore(EncounterPhase::Complete);else if(in.flag("started").value_or(false))director.restore(EncounterPhase::Running);}};
    // A scene actor (a spawn point's NPC, a placed dummy) that was defeated is not created again until a rest or a party wipe.
    struct ActorState final:PersistentObject {bool defeated{};
        std::string_view kind()const override{return "actor";}
        void capture(LedgerWriter& out)const override{if(defeated)out.set("defeated",true,{LedgerScope::Save,LedgerReset::Rest|LedgerReset::PartyWipe});}
        void restore(const LedgerReader& in)override{defeated=in.flag("defeated").value_or(false);}};
    void record_interactable(std::size_t index);
    void record_trigger(std::size_t index);
    void record_encounter(std::size_t index);
    // Every persistent object of the scene into the ledger. Each fact was already written in the tick it changed, so
    // this changes nothing in a healthy session; it is what an unload calls before the scene is destroyed.
    void capture();
    scene_detail::SceneWorldObjects::Who who(const Member& m)const;
    scene_detail::SceneWorldObjects::Context context()const;
    // The view every member's client holds: sent when it changes, and again to a member that has not received it.
    void publish_view();
    // The party enters in a fixed formation: the first member on the point, the others on a ring of 1.2 m around it in
    // seat order, starting behind it. Each place is put on the ground under it and, while a scene actor or an earlier
    // member stands there, moved further out.
    MotorVec formation(const SceneArrival& arrival,std::size_t seat,std::span<const MotorVec> taken)const;
    Impl(SessionHandshake hello,std::span<const ObjectData> objects,std::span<const SceneSeat> party,const CollisionDefinition& collision,std::shared_ptr<const CharacterSceneCombat> configured,const SceneStart& start,SceneTiming mode);
    const AbilityDefinition& definition(const AbilityOwnerSnapshot& snapshot)const;
    MotionRequest motion(const AbilityOwnerSnapshot& snapshot,std::uint64_t from,std::uint64_t to,MotorInput& command)const;
    // Root/lock request of an owner's action across one authoritative tick, including the
    // final partial step of an action that completed on this tick.
    MotionRequest action_motion(const AbilityOwnerSnapshot& before,const AbilityOwnerSnapshot& after,std::span<const AbilityActionUpdate> updates,MotorInput& command,double motor_yaw,bool requested=false)const;
    // Movement restriction from statuses and death. Authority reads its own state after effects advanced on this tick.
    bool restricted(AbilityOwnerHandle owner,std::uint64_t id)const;
    // Server side of one actor: senses, authoritative motor, ability owner with its kit. Runs at session start for the scene's
    // actors and during play for an enemy an encounter spawns.
    void arm(Actor& actor);
    // An enemy enters the session: a replicated object at the requested place, then the same setup as any actor.
    // False when the place is blocked by a body: nothing is left behind and the caller tries again.
    bool spawn(std::size_t index,const EncounterSpawn& request);
    // An actor leaves the session: its replicated object, motor and ability owner. Its clients drop what they prepared
    // for it when the object leaves their world.
    void retire(std::size_t position);
    // Round robin over the actors that have a controller: up to budget of them get this tick's turn, starting where the last tick stopped.
    void slice(std::size_t& cursor,unsigned budget);
    const Member* member(StableId identity)const;
    const Member* member(std::uint64_t network)const;
    // A trigger volume and an encounter's bounds see the party through one member: the first living one inside,
    // otherwise the first member.
    std::pair<MotorVec,bool> witness(std::span<const EncounterVolume> volumes)const;
    // World step, before sensing: deliver last tick's events, remove the dead whose time is up, let trigger volumes and
    // encounters act on authoritative state, then bring in at most spawn_budget of the enemies that are waiting to enter.
    void run_world(std::uint64_t tick);
    const Actor* find(StableId identity)const;
    // Soft-lock candidates for a party member: every live actor of another faction, from authority's own state.
    std::vector<TargetCandidate> server_candidates()const;
    // The motor of the character an actor's controller chose: a party member or another actor.
    const MotorState* locked_motor(const Actor& actor,std::uint64_t& id)const;
    std::vector<TargetCandidate> locked_candidates(const Actor& actor)const;
    // Shared aim for NPCs: an aimed ability points from its muzzle at the body of the target the controller chose for this action,
    // raised by the drop a projectile will suffer on the way. A projectile is aimed at where the target will be when it arrives:
    // the target's current movement over the ground, held for the flight time, refined twice because the lead changes the distance.
    // An instant shot has no flight time and no lead. The controller only chooses the target and presses the slot.
    void aim_actor(const Actor& actor,const AbilityDefinition& ability,MotorInput& command)const;
    // Controller seam: an actor's controller sees a bounded view of authoritative state as it stood before this tick and asks for
    // move, face and a slot press. Activation, cost, cooldown, hits and effects are the ordinary authoritative ability path.
    std::shared_ptr<const AbilityDefinition> actor_ability(const Actor& actor,CombatSlot slot)const;
    // Sensing: every actor perceives the others, party members included, from authoritative state as it stood before this
    // tick, in ascending network identity, with at most one ray per other character against static collision. Nothing
    // from a client is read.
    bool sight(MotorVec from,MotorVec to)const;
    void sense(Actor& actor,std::uint64_t tick);
    const PerceivedActor* perceived(const Actor& actor,StableId identity)const;
    bool drive_controller(Actor& actor,std::uint64_t tick,MotorInput& command);
    // A member that left stops being a member at once, but its avatar leaves the world at the head of the next tick: a new
    // world revision needs that tick's motor and collision frames to be a coherent baseline, and a held scene runs no tick.
    struct Departed {std::uint64_t network{};std::unique_ptr<CharacterMotor> motor;};std::vector<Departed> departed;
    void retire_departed();
    ObjectData member_template;std::vector<SceneSeat> arriving;std::vector<std::pair<StableId,std::string>> refused;
    void add_member(const SceneSeat& seat);
    // The same setup as a member that entered with the session, at the tick the scene has reached.
    void enter(const SceneSeat& seat);
    void admit_arrivals();
    bool remove_member(StableId id);
    void pump();
    void step(std::uint64_t tick);
};
}
