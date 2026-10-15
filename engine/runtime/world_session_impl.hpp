#pragma once
#include "world_session_packet.hpp"
// Private shared state; public ownership and tick order remain in the session facade.
#include <darkangel/world_session.hpp>
#include "ability_state.hpp"
#include "session_wire.hpp"
#include "owner_mailbox.hpp"
#include "ability_wire.hpp"
#include "effect_wire.hpp"
#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <optional>
#include <tuple>

namespace darkangel {
using namespace session_detail;
struct WorldSession::Impl {
    static constexpr std::size_t ability_chunk_bytes = 896;
    struct QueuedAbility {
        AbilityIntent intent;
        ConnectionHandle peer;
        bool resync_notified{}, expired_bootstrap{};
    };
    struct AbilitySending {
        std::uint64_t world_revision{}, ability_revision{}, highest_operation{};
        std::vector<std::byte> bytes;
        unsigned next{};
    };
    struct AbilityPartial {
        std::uint64_t world_revision{}, ability_revision{}, birth{}, total{}, chunks{};
        std::map<unsigned, std::vector<std::byte>> parts;
    };
    struct PublicSending {
        std::uint64_t world_revision{}, ability_revision{}, avatar_epoch{};
        std::vector<std::byte> bytes;
        unsigned next{};
    };
    struct PublicPartial {
        std::uint64_t world_revision{}, ability_revision{}, avatar_epoch{}, birth{}, total{}, chunks{};
        std::map<unsigned, std::vector<std::byte>> parts;
    };
    using EffectKey = std::pair<std::uint64_t, AttributeVisibility>;
    struct EffectSent {
        std::uint64_t world_revision{}, avatar_epoch{};
        std::vector<EffectReplica> effects;
    };
    struct EffectSending {
        std::uint64_t revision{};
        EffectSent state;
        std::vector<std::byte> bytes;
        unsigned next{};
    };

    SessionRole role;
    SessionHandshake hello;
    SessionLimits limits;
    std::thread::id owner = std::this_thread::get_id();
    std::unique_ptr<World> view;
    std::map<std::uint64_t, ObjectData> objects;
    std::uint64_t revision{}, next_id{1}, clock{};
    const std::chrono::steady_clock::time_point timer_origin = std::chrono::steady_clock::now();
    std::uint64_t timeout_clock() const;
    // Server-only projectiles in flight (velocity and gravity per tick), and the public view both roles expose.
    struct Projectile {
        std::uint64_t id{};
        AbilityState::PendingHit hit;
        std::uint64_t source{};
        MotorVec position{}, velocity{};
        double gravity{}, radius{}, remaining{};
        std::uint64_t expires{};
    };
    std::vector<Projectile> projectiles;
    std::vector<ProjectileView> projectile_views;
    std::uint64_t next_projectile{1}, projectile_revision{}, projectile_tick{};
    // Server-only: actors of one team never damage each other. Absent means no team.
    std::map<std::uint64_t, std::uint32_t> teams;
    std::map<std::uint32_t, StableId> owner_channels;
    std::vector<OwnerMessage> owner_messages;
    struct Peer {
        OwnerMailbox owner_mailbox;
        Transport* transport;
        ConnectionHandle handle;
        SessionReadiness state{SessionReadiness::Handshake};
        std::uint64_t progress{}, sent_revision{}, acked_revision{};
        bool greeting{}, content_sent{}, await_ack{}, ready_sent{}, collision_required{}, ever_ready{};
        std::size_t next_chunk{}, total{}, chunks{};
        std::vector<ObjectData> sending;
        std::map<std::uint64_t, ObjectData> receiving;
        std::uint64_t receiving_revision{};
        std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> motor_sent;
        std::optional<CollisionStreamFrame> collision_sending;
        unsigned collision_next{};
        std::uint64_t collision_revision{}, collision_last_tick{}, collision_last_revision{};
        bool collision_sent{};
        std::map<std::uint64_t, AbilitySending> ability_sending;
        std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> ability_sent;
        std::map<std::uint64_t, AbilityPartial> ability_parts;
        std::deque<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>> ability_sent_frames;
        std::map<std::uint64_t, PublicSending> public_sending;
        std::map<std::uint64_t, std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> public_sent;
        std::map<std::uint64_t, PublicPartial> public_parts;
        std::map<EffectKey, EffectSending> effect_sending;
        std::map<EffectKey, EffectSent> effect_sent;
        std::map<EffectKey, PublicPartial> effect_parts;
        EffectKey effect_cursor{};
        std::map<std::uint64_t, std::uint64_t> correction_tick;
        std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> correction_motor;
        std::uint64_t projectile_sent{}, packets_sent{};
        std::array<std::uint64_t, 32> packets_by_kind{};
        std::map<std::uint64_t, std::uint64_t> public_content;
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> index_sent;
        unsigned packet_work{};
        CollisionStreamDiagnostics collision_stats;
        std::optional<std::uint64_t> collision_ack_pending;
        std::deque<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> collision_sent_frames;
        std::uint64_t collision_partial_birth{};
        std::uint64_t collision_receiving_tick{}, collision_receiving_topology{}, collision_receiving_total{}, collision_receiving_chunks{};
        std::map<unsigned, std::vector<std::byte>> collision_parts;
    };
    std::map<ConnectionHandle, Peer> peers;
    struct MotorQueue {
        ConnectionHandle owner;
        std::map<std::uint64_t, MotorInput> pending;
        MotorInput last;
        std::uint64_t consumed_tick{}, received_sequence{};
    };
    std::map<std::uint64_t, MotorQueue> motor_inputs;
    std::map<std::uint64_t, MotorState> motor_states;
    std::map<std::uint64_t, AbilityFailure> injected_refusals;
    std::optional<CollisionStreamFrame> latest_collision;
    std::deque<CollisionStreamFrame> collision_states;
    std::map<std::uint64_t, std::unique_ptr<AbilityState>> abilities;
    std::uint64_t ability_tick{}, resolved_hit_tick{};
    std::map<std::uint32_t, CombatEvaluator> damage_evaluators;
    std::map<std::uint32_t, EffectEvaluator> effect_evaluators;
    std::vector<EffectOutcome> effect_outcomes;
    std::shared_ptr<const MeleeQuery> melee_query;
    std::map<std::uint64_t, AbilityCorrection> ability_corrections;
    std::map<std::uint64_t, AbilityPublicFrame> public_abilities;
    std::map<std::uint64_t, PublicIndexEntry> public_index;
    std::set<std::uint64_t> public_resync;
    std::map<std::uint64_t, EffectFrame> owner_effect_frames, public_effect_frames;
    std::set<EffectKey> effect_resync;
    std::map<std::uint64_t, std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> ability_ack_pending;
    bool ability_resync{};
    std::map<std::uint64_t, std::map<std::uint64_t, QueuedAbility>> ability_inputs;
    std::map<ConnectionHandle, std::deque<AbilityOperationNotice>> ability_notices;
    std::vector<AbilityOperationNotice> received_ability_notices;
    std::vector<InteractionRequest> interaction_requests;
    std::map<ConnectionHandle, std::deque<InteractionNotice>> interaction_notices;
    std::vector<InteractionNotice> received_interaction_notices;
    std::vector<AbilityActionUpdate> ability_actions;
    std::vector<AbilityCommitUpdate> ability_commitments;

    const AbilityState& ability(AbilityOwnerHandle h) const;
    std::vector<AbilityActionUpdate> queue_actions(std::span<const AbilityActionUpdate> updates) const;
    std::vector<AbilityCommitUpdate> queue_commitments(std::span<AbilityState* const> states) const;
    std::vector<AbilityCommitUpdate> queue_commitments(const std::map<std::uint64_t, std::unique_ptr<AbilityState>>& states) const;
    void commit_ability(std::uint64_t id, std::unique_ptr<AbilityState> candidate, std::span<const AbilityActionUpdate> updates);

    void release_ability_control(ConnectionHandle peer);
    Impl(SessionRole r, SessionHandshake h, SessionLimits l);
    void thread() const;
    void authority() const;
    std::unique_ptr<World> build(const std::map<std::uint64_t, ObjectData>& values) const;
    void pump_owner(Peer& p);
    bool send(Peer& p, Writer w, Delivery delivery = Delivery::ReliableOrdered);
    void receive(Peer& p, const TransportMessage& m);
    void pump_interaction_notices(Peer& p);
    void pump_ability_notices(Peer& p);
    // A motor state from the motor stream or from the owner's correction becomes the client's current one for that character when it is
    // newer.
    void accept_motor(std::uint64_t rev, std::uint64_t id, const MotorState& state);
    // One complete public ability frame, checked against its envelope and the object it describes, becomes the client's current one.
    void publish_public(std::uint64_t index, std::uint64_t rev, std::uint64_t ability_revision, std::uint64_t avatar_epoch,
                        const std::vector<std::byte>& complete);
    void pump_abilities(Peer& p);
    // A public frame is read with the motor state of its tick: a far character's state, otherwise due every far_interval-th tick, goes out
    // with its frame.
    void pair_motor(Peer& p, std::uint64_t id);
    void pump_public_abilities(Peer& p);
    // One packet a tick: every character's public ability tick and content, so a client can tell a held frame is current.
    void pump_public_index(Peer& p);
    void pump_effects(Peer& p);
    // The whole public projectile list in one state packet whenever it changed, and periodically while any fly.
    void pump_projectiles(Peer& p);
    // Collision items are packed whole, as many as fit a packet: a character entry is 88 bytes and a box 176, so a scene of
    // characters takes far fewer packets than a fixed count per packet sized for the largest item.
    static std::vector<std::size_t> collision_chunk_starts(const CollisionStreamFrame& frame);
    // The motor state of a character this peer owns, if it owns one.
    const MotorState* owned_motor(const Peer& p) const;
    void pump_collision(Peer& p);
    void pump(Peer& p);
};
} // namespace darkangel
