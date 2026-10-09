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
struct SessionLimits {std::size_t objects{128},peers{4},packets_per_tick{16};unsigned timeout_ticks{600},collision_timeout_ticks{30};};
struct CollisionStreamDiagnostics {std::uint64_t sent_packets{},sent_bytes{},decoded{},ack_tick{},ack_topology{},expired{},superseded{};std::size_t partial_chunks{};bool has_ack{},needs_resync{};};
struct AbilityCorrection {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityOwnerSnapshot ability;};
struct AbilityPublicFrame {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityPublicSnapshot ability;};
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
    void tick();
    std::uint64_t create(ObjectData);
    void move(std::uint64_t,const Transform&);
    void destroy(std::uint64_t);
    // Checked server-only immediate-commit ability subset. No client RPC or
    // speculative owner prediction is provided by these methods yet.
    AbilityOwnerHandle configure_abilities(std::uint64_t,std::vector<AttributeDefinition>,AttributeId health,AttributeId maximum_health);
    void equip_combat_kit(AbilityOwnerHandle,std::shared_ptr<const CombatKitDefinition>,const InputProfile&,std::span<const std::shared_ptr<const AbilityDefinition>>);
    bool submit_ability_intent(const AbilityIntent&);
    std::vector<AbilityOperationNotice> drain_ability_receipts();
    AbilityFailure can_activate(const AbilityRequest&)const;
    AbilityReceipt request_ability(const AbilityRequest&);
    AbilityFailure cancel_ability(AbilityActivationHandle);
    void advance_abilities(std::uint64_t simulation_tick,unsigned action_rate=action_tick_units);
    void bind_melee_query(std::shared_ptr<const MeleeQuery>);
    void register_damage_evaluator(std::uint32_t,DamageEvaluator);
    void configure_ability_tags(AbilityOwnerHandle,std::shared_ptr<const TagDictionary>);
    void register_effect_evaluator(std::uint32_t,EffectEvaluator);
    EffectHandle apply_effect(AbilityOwnerHandle target,AbilityOwnerHandle source,const EffectDefinition&,double power,std::uint64_t source_activation=0);
    void remove_effect(AbilityOwnerHandle,EffectHandle);
    void cleanse_effects(AbilityOwnerHandle,const TagRequirement&);
    std::vector<EffectSnapshot> ability_effects(AbilityOwnerHandle)const;
    std::vector<TagId> ability_tags(AbilityOwnerHandle,AttributeVisibility=AttributeVisibility::Server)const;
    std::vector<EffectOutcome> drain_effect_outcomes();
    // After authoritative motor post_physics/publication, once per fixed tick.
    std::vector<DamageResult> resolve_ability_hits(std::uint64_t);
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
    void own_motor(std::uint64_t,ConnectionHandle);
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
    const World& world() const;
    const std::map<std::uint64_t,ObjectData>& objects() const;
    std::uint64_t revision() const;
    SessionReadiness readiness(ConnectionHandle) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
