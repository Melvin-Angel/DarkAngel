#pragma once
#include <darkangel/transport.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/world.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability.hpp>
#include <darkangel/melee.hpp>
#include <darkangel/effects.hpp>
#include <darkangel/effect_replication.hpp>
#include <deque>
#include <map>
#include <memory>
namespace darkangel {
enum class SessionRole { Server, Client };
enum class SessionReadiness { Handshake, ContentReady, Bootstrap, CatchUp, Ready };
// relevance_radius and far_interval: a character further than the radius from the receiving peer's own character has its
// motor state sent every far_interval-th tick instead of every tick; observers interpolate. The peer's own character and
// everything inside the radius stay at full rate, so melee, soft lock and aim against nearby characters are unchanged.
// body_radius: the collision stream carries, for a peer that owns a character, only the character bodies within this
// distance of it (its own always): a body further away cannot be touched before the next frame arrives. Level bodies
// (boxes, meshes) always travel.
// With wall_clock_timeouts, deadline ticks are elapsed 1/60 seconds; packet work and resend cadence remain bounded per pump.
struct SessionLimits {std::size_t objects{128},peers{4},packets_per_tick{16};unsigned timeout_ticks{600},collision_timeout_ticks{30};double relevance_radius{12};unsigned far_interval{3};double body_radius{5};bool wall_clock_timeouts{};};
struct CollisionStreamDiagnostics {std::uint64_t sent_packets{},sent_bytes{},decoded{},ack_tick{},ack_topology{},expired{},superseded{};std::size_t partial_chunks{};bool has_ack{},needs_resync{};};
struct AbilityCorrection {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityOwnerSnapshot ability;};
// The wire frame carries the ability snapshot and the avatar epoch, not the motor state: a client pairs it with the
// character's state from the motor stream. motor is that state at the frame's tick once the client holds it, otherwise
// the newest state it has.
struct AbilityPublicFrame {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityPublicSnapshot ability;std::uint64_t avatar_epoch{};};
// What a public ability snapshot says apart from its clock: attributes, tags, loadout and the running action. Two
// snapshots with the same content differ only in tick and revision, and in the clock of a running action by exactly
// its rate for each tick between them: an action is in the content by its identity, rate, phase and the origin of its
// clock (clock minus rate times tick). Server and client compute it with this one function.
std::uint64_t public_ability_content(const AbilityPublicSnapshot&);
// A held snapshot brought to a later tick whose index entry has the same content: the tick, and a running action's
// clock advanced by its rate.
AbilityPublicSnapshot public_ability_at(const AbilityPublicSnapshot&,std::uint64_t tick);
// The server's per-tick index of public ability state: for each character, the tick its state stands at and its
// content. A client holding a frame with the same content knows that frame is current without receiving it again.
struct PublicIndexEntry {std::uint64_t tick{},content{};};
// A projectile in flight as presentation sees it (velocity in metres per second). Authority owns the flight
// and its hit; this is public state and decides nothing.
struct ProjectileView {std::uint64_t id{};MotorVec position{},velocity{};};
// Projectiles one session flies at once; a launch beyond it is dropped.
inline constexpr std::size_t projectile_capacity=16;
// A client's request to use a placed interactable, as the server received it from the peer that owns that character,
// and the server's answer to it (an InteractionResult number). The request claims nothing; the host resolves it.
struct InteractionRequest {std::uint64_t network{};StableId object;};
struct InteractionNotice {std::uint64_t network{};StableId object;std::uint8_t result{};};
// Bounded reliable messages for owner services. The session authenticates the motor
// owner and scene epoch; a service still validates its own schema and permissions.
struct OwnerMessage {ConnectionHandle source;std::uint64_t network{};std::uint32_t channel{};std::vector<std::byte> bytes;};
// All providers carry the same bounded, versioned protocol. Only the server
// creates identities and publishes state. Client worlds are read-only views.
class WorldSession {
public:
    WorldSession(SessionRole,SessionHandshake,SessionLimits={});
    ~WorldSession();
    WorldSession(const WorldSession&)=delete;
    WorldSession& operator=(const WorldSession&)=delete;
    void attach(Transport&,ConnectionHandle);
    void detach(ConnectionHandle);
    // Protocol/service rejection: close transport and release authenticated ownership.
    void disconnect(ConnectionHandle);
    bool attached(ConnectionHandle)const;
    void tick();
    std::uint64_t create(ObjectData);
    void move(std::uint64_t,const Transform&);
    void destroy(std::uint64_t);
    // Checked server-only immediate-commit ability subset. No client RPC or
    // speculative owner prediction is provided by these methods yet.
    AbilityOwnerHandle configure_abilities(std::uint64_t,std::vector<AttributeDefinition>,AttributeId health,AttributeId maximum_health);
    void equip_combat_kit(AbilityOwnerHandle,std::shared_ptr<const CombatKitDefinition>,const InputProfile&,std::span<const std::shared_ptr<const AbilityDefinition>>);
    // Server-only: prepared mask loadouts; clients select one through an ability intent.
    void configure_ability_loadouts(AbilityOwnerHandle,std::span<const std::optional<AbilityLoadout>>,unsigned active);
    bool submit_ability_intent(const AbilityIntent&);
    std::vector<AbilityOperationNotice> drain_ability_receipts();
    // Interaction: a client asks for its own character; the server host drains the requests, resolves each from
    // authoritative state and answers; the client drains the answers.
    bool submit_interaction(std::uint64_t network,StableId object);
    std::vector<InteractionRequest> drain_interactions();
    void answer_interaction(std::uint64_t network,StableId object,std::uint8_t result);
    std::vector<InteractionNotice> drain_interaction_receipts();
    // Register before attaching. Client registrations name its own avatar, never a
    // profile claimed on the wire. Server registrations leave avatar empty.
    void register_owner_channel(std::uint32_t channel,StableId avatar={});
    // Up to eight queued 32 KiB messages per peer, fragmented inside the ordinary
    // packet budget. False means not ready or backpressure; caller retains work.
    bool send_owner_message(std::uint64_t network,std::uint32_t channel,std::span<const std::byte>);
    std::vector<OwnerMessage> drain_owner_messages(std::uint32_t channel);
    AbilityFailure can_activate(const AbilityRequest&)const;
    AbilityReceipt request_ability(const AbilityRequest&);
    // Fault injection for tests and tools: authority refuses this owner's next pressed ability intent with the given
    // reason, through the same path and with the same receipt as a rule that failed. Loopback never refuses a
    // prediction on its own; on a real connection a refusal comes from state the client has not heard of yet.
    void refuse_next_ability(std::uint64_t network,AbilityFailure reason=AbilityFailure::Busy);
    AbilityFailure cancel_ability(AbilityActivationHandle);
    void advance_abilities(std::uint64_t simulation_tick,unsigned action_rate=action_tick_units);
    void bind_melee_query(std::shared_ptr<const MeleeQuery>);
    void register_damage_evaluator(std::uint32_t,DamageEvaluator);
    void register_combat_evaluator(std::uint32_t,CombatEvaluator);
    void configure_ability_tags(AbilityOwnerHandle,std::shared_ptr<const TagDictionary>);
    void register_effect_evaluator(std::uint32_t,EffectEvaluator);
    EffectHandle apply_effect(AbilityOwnerHandle target,AbilityOwnerHandle source,const EffectDefinition&,double power,std::uint64_t source_activation=0);
    void remove_effect(AbilityOwnerHandle,EffectHandle);
    void cleanse_effects(AbilityOwnerHandle,const TagRequirement&);
    // Server-only: restores Health (clamped to its maximum), for revives, consumables and rests.
    void heal(AbilityOwnerHandle,double amount);
    // Server-only: takes Health away outside any ability (tools and gates).
    void damage(AbilityOwnerHandle,double amount);
    std::vector<EffectSnapshot> ability_effects(AbilityOwnerHandle)const;
    std::vector<TagId> ability_tags(AbilityOwnerHandle,AttributeVisibility=AttributeVisibility::Server)const;
    std::vector<EffectOutcome> drain_effect_outcomes();
    // Server-only: actors that share a nonzero team never damage each other. Zero clears it. Team is
    // authority state and is never replicated; an ally in the path of a swept shot still stops it.
    void assign_team(std::uint64_t,std::uint32_t team);
    // After authoritative motor post_physics/publication, once per fixed tick.
    std::vector<DamageResult> resolve_ability_hits(std::uint64_t);
    // Projectiles in flight: the server's own after its latest hit resolution, or the latest frame a client received.
    const std::vector<ProjectileView>& projectiles()const;
    const std::map<std::uint64_t,AbilityCorrection>& ability_corrections()const;
    bool ability_correction_needs_resync()const;
    const std::map<std::uint64_t,AbilityPublicFrame>& public_abilities()const;
    const std::map<std::uint64_t,EffectFrame>& effect_frames(AttributeVisibility)const;
    bool effect_frame_needs_resync(std::uint64_t,AttributeVisibility)const;
    bool public_ability_needs_resync(std::uint64_t network)const;
    // ACK only after assets and native owner reconciliation have been prepared.
    void acknowledge_ability_correction(std::uint64_t network);
    AbilityOwnerSnapshot ability_snapshot(AbilityOwnerHandle)const;
    double ability_available(AbilityOwnerHandle,AttributeId)const;
    std::vector<AbilityCommitUpdate> drain_ability_commitments();
    std::vector<AbilityActionUpdate> drain_ability_actions();
    void retire_ability_operations(AbilityOwnerHandle,std::uint64_t through);
    // Explicit server ownership; movement commands never grant authority.
    // consumed_tick: the tick the owner's commands start after, for a motor that enters a session already running.
    void own_motor(std::uint64_t id,ConnectionHandle peer,std::uint64_t consumed_tick=0);
    bool submit_motor(std::uint64_t,const MotorInput&);
    bool motor_input_ready(std::uint64_t,std::uint64_t) const;
    MotorInput consume_motor(std::uint64_t,std::uint64_t simulation_tick,std::uint64_t epoch);
    void publish_motor(std::uint64_t,const MotorState&);
    const std::map<std::uint64_t,MotorState>& motors() const;
    void publish_collision(CollisionStreamFrame);
    const std::deque<CollisionStreamFrame>& collisions()const;
    // Caller must prepare native geometry/lifecycle dependencies before this ACK.
    void acknowledge_collision(std::uint64_t tick);
    bool collision_control_ready()const;
    CollisionStreamDiagnostics collision_diagnostics(ConnectionHandle)const;
    // Packets this session has handed to the transport for a peer, for budget measurements.
    std::uint64_t packets_sent(ConnectionHandle)const;
    // The same count split by wire kind (index = kind number), for budget measurements.
    std::vector<std::uint64_t> packets_by_kind(ConnectionHandle)const;
    // Latest public ability index received from the server, by network identity. Client role.
    const std::map<std::uint64_t,PublicIndexEntry>& public_index()const;
    const World& world() const;
    const std::map<std::uint64_t,ObjectData>& objects() const;
    std::uint64_t revision() const;
    SessionReadiness readiness(ConnectionHandle) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
