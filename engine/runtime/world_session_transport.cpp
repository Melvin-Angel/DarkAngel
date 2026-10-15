#include "world_session_impl.hpp"

namespace darkangel {
auto WorldSession::Impl::timeout_clock() const -> std::uint64_t {
    return limits.wall_clock_timeouts
               ? std::uint64_t(std::chrono::duration<double>(std::chrono::steady_clock::now() - timer_origin).count() * 60)
               : clock;
}

WorldSession::Impl::Impl(SessionRole r, SessionHandshake h, SessionLimits l)
    : role(r), hello(std::move(h)), limits(l),
      view(std::make_unique<World>(r == SessionRole::Server ? WorldDomain::Server : WorldDomain::ClientPresentation, l.objects)) {
    require((r == SessionRole::Server || r == SessionRole::Client) && l.objects > 0 && l.objects <= 512 && l.peers > 0 && l.peers <= 4 &&
                l.packets_per_tick > 0 && l.packets_per_tick <= 32 && l.timeout_ticks > 0 && l.collision_timeout_ticks > 0 &&
                l.collision_timeout_ticks <= 600,
            "Invalid session role/bounds");
    validate_session_handshake(hello);
}

auto WorldSession::Impl::thread() const -> void {
    require(owner == std::this_thread::get_id(), "Session owner thread mismatch");
}

auto WorldSession::Impl::authority() const -> void {
    thread();
    require(role == SessionRole::Server, "Client cannot mutate authoritative state");
    require(revision < std::numeric_limits<std::uint64_t>::max(), "Session revision exhausted");
}

auto WorldSession::Impl::build(const std::map<std::uint64_t, ObjectData>& values) const -> std::unique_ptr<World> {
    auto candidate =
        std::make_unique<World>(role == SessionRole::Server ? WorldDomain::Server : WorldDomain::ClientPresentation, limits.objects);
    std::set<StableId> ids;
    for (const auto& [id, o] : values) {
        require(id > 0 && o.network.value == id && ids.insert(o.id).second, "Invalid/duplicate replicated identity");
        candidate->create(o);
    }
    return candidate;
}

auto WorldSession::Impl::send(Peer& p, Writer w, Delivery delivery) -> bool {
    if (p.packet_work >= limits.packets_per_tick)
        return false;
    if (!p.transport->send(p.handle, delivery, w.bytes))
        return false;
    ++p.packet_work;
    ++p.packets_sent;
    {
        Reader r{w.bytes};
        r.u64();
        const auto kind = r.u64();
        if (kind < p.packets_by_kind.size())
            ++p.packets_by_kind[kind];
    }
    return true;
}

auto WorldSession::Impl::receive(Peer& p, const TransportMessage& m) -> void {
    require(m.sender == p.handle && m.bytes.size() <= packet_limit, "Invalid session source/channel/size");
    Reader r{m.bytes};
    require(r.u64() == 0x3253454144, "Session wire version mismatch");
    auto kind = static_cast<Kind>(r.u64());
    require(m.delivery == (kind == Kind::Baseline
                               ? Delivery::ReliableBootstrap
                               : ((kind == Kind::MotorSnapshot || kind == Kind::CollisionSnapshot || kind == Kind::AbilityCorrection ||
                                   kind == Kind::AbilityPublic || kind == Kind::EffectOwner || kind == Kind::EffectPublic ||
                                   kind == Kind::Projectiles || kind == Kind::MotorBatch || kind == Kind::MotorRemote ||
                                   kind == Kind::PublicIndex || kind == Kind::PublicBatch)
                                      ? Delivery::UnreliableState
                                      : Delivery::ReliableOrdered)),
            "Session traffic class mismatch");
    require(r.u64() == hello.session_epoch, "Stale gameplay session epoch");
    auto rev = r.u64(), index = r.u64(), chunks = r.u64(), total = r.u64();
    if (kind == Kind::Hello) {
        require(rev == 0 && index == 0 && chunks == 0 && total == 0 && !p.greeting, "Duplicate/invalid gameplay handshake");
        require(r.u64() == hello.protocol, "Protocol mismatch");
        for (const auto* hash : {&hello.schema_hash, &hello.content_hash})
            for (char c : *hash) {
                require(r.at < r.bytes.size(), "Truncated handshake digest");
                require(std::to_integer<unsigned char>(r.bytes[r.at++]) == static_cast<unsigned char>(c), "Content/schema mismatch");
            };
        auto flags = r.u64();
        require(flags <= 1 && (role == SessionRole::Client || flags == 0), "Session collision readiness capability");
        p.collision_required = flags != 0;
        r.end();
        p.greeting = true;
        p.state = SessionReadiness::ContentReady;
    } else if (kind == Kind::ContentReady) {
        require(role == SessionRole::Server && p.greeting && !p.content_sent && rev == 0 && index == 0 && chunks == 0 && total == 0,
                "Unexpected content readiness");
        r.end();
        p.content_sent = true;
        p.state = SessionReadiness::Bootstrap;
    } else if (kind == Kind::Baseline) {
        require(role == SessionRole::Client && p.content_sent && total <= limits.objects &&
                    chunks == std::max<std::uint64_t>(1, (total + per_chunk - 1) / per_chunk) && index < chunks,
                "Invalid baseline bounds/state");
        require(rev >= revision && (p.next_chunk == 0 || (rev == p.receiving_revision && total == p.total && chunks == p.chunks)) &&
                    index == p.next_chunk,
                "Baseline order/revision mismatch");
        if (index == 0) {
            p.receiving.clear();
            p.receiving_revision = rev;
            p.total = static_cast<std::size_t>(total);
            p.chunks = static_cast<std::size_t>(chunks);
            p.state = SessionReadiness::Bootstrap;
        }
        auto count = std::min<std::uint64_t>(per_chunk, total - index * per_chunk);
        for (std::size_t i = 0; i < count; ++i) {
            auto o = read_object(r);
            require(p.receiving.emplace(o.network.value, o).second, "Duplicate network identity");
        }
        r.end();
        ++p.next_chunk;
        if (p.next_chunk == p.chunks) {
            require(p.receiving.size() == total, "Incomplete baseline");
            auto candidate = build(p.receiving);
            view = std::move(candidate);
            objects = std::move(p.receiving);
            motor_states.clear();
            collision_states.clear();
            ability_corrections.clear();
            public_abilities.clear();
            public_resync.clear();
            owner_effect_frames.clear();
            public_effect_frames.clear();
            effect_resync.clear();
            ability_ack_pending.clear();
            p.ability_parts.clear();
            p.public_parts.clear();
            p.effect_parts.clear();
            p.collision_parts.clear();
            p.collision_ack_pending.reset();
            p.collision_stats.needs_resync = p.collision_stats.decoded > 0;
            p.collision_stats.has_ack = false; // A new atomic baseline needs fresh motor state at its revision.
            revision = rev;
            p.next_chunk = 0;
            p.state = SessionReadiness::CatchUp;
            p.await_ack = true;
        }
    } else if (kind == Kind::Ack) {
        require(role == SessionRole::Server && p.await_ack && rev == p.sent_revision && index == 0 && chunks == 0 && total == 0,
                "Unknown baseline ACK");
        r.end();
        p.await_ack = false;
        p.acked_revision = rev;
        p.state = SessionReadiness::CatchUp;
    } else if (kind == Kind::Ready) {
        if (role == SessionRole::Client && rev < revision) {
            require(index == 0 && chunks == 0 && total == 0, "Invalid obsolete Ready");
            r.end();
            return;
        }
        require(role == SessionRole::Client && p.state == SessionReadiness::CatchUp && rev == revision && !p.await_ack && index == 0 &&
                    chunks == 0 && total == 0,
                "Premature Ready");
        r.end();
        p.state = SessionReadiness::Ready;
        p.ever_ready = true;
    } else if (kind == Kind::MotorCommand) {
        require(role == SessionRole::Server && (p.state == SessionReadiness::Ready || p.ever_ready) && index && chunks == 0 && total == 0 &&
                    rev == 0,
                "Invalid motor command state");
        auto it = motor_inputs.find(index);
        require(it != motor_inputs.end() && it->second.owner == p.handle, "Motor command ownership denied");
        MotorInput input;
        input.sequence = r.u64();
        input.tick = r.u64();
        input.epoch = r.u64();
        input.x = r.number();
        input.z = r.number();
        input.yaw = r.number();
        auto jump = r.u64(), low = r.u64();
        require(jump <= 1 && low <= 1, "Invalid input flags");
        input.jump = jump != 0;
        input.crouch = low != 0;
        input.pitch = r.number();
        r.end();
        validate_motor_input(input);
        require(input.sequence > 0, "Invalid owner sequence");
        if (p.state != SessionReadiness::Ready)
            return;
        auto& q = it->second;
        if (latest_collision && (!p.collision_stats.has_ack || p.collision_stats.ack_topology != latest_collision->topology))
            return;
        if (motor_states.contains(index) && input.epoch != motor_states.at(index).epoch)
            return;
        if (input.sequence <= q.received_sequence || input.tick <= q.consumed_tick)
            return;
        require(input.tick - q.consumed_tick <= 8 && q.pending.size() < 8, "Motor input lead/work limit");
        require(!q.pending.contains(input.tick), "Multiple movement commands for one tick");
        q.pending.emplace(input.tick, input);
        q.received_sequence = input.sequence;
    } else if (kind == Kind::MotorSnapshot || kind == Kind::MotorBatch) {
        // One motor state per packet, or several packed into one; each is accepted by the same rule.
        auto accept = [&](std::uint64_t id, const MotorState& state) { accept_motor(rev, id, state); };
        if (kind == Kind::MotorSnapshot) {
            require(role == SessionRole::Client && index && chunks == 0 && total == 0, "Invalid motor snapshot direction/header");
            auto state = read_motor(r);
            r.end();
            accept(index, state);
        } else {
            require(role == SessionRole::Client && index == 0 && chunks == 0 && total >= 1 && total <= motor_batch_states,
                    "Invalid motor batch direction/header");
            std::vector<std::pair<std::uint64_t, MotorState>> states;
            for (std::uint64_t item = 0; item < total; ++item) {
                const auto id = r.u64();
                require(id != 0 && std::none_of(states.begin(), states.end(), [&](const auto& known) { return known.first == id; }),
                        "Motor batch identity");
                states.emplace_back(id, read_motor(r));
            }
            r.end();
            for (const auto& [id, state] : states)
                accept(id, state);
        }
    } else if (kind == Kind::MotorRemote) {
        // Characters this peer does not own, as an observer uses them; the topology they share is in the header.
        require(role == SessionRole::Client && index && chunks == 0 && total >= 1 && total <= motor_remote_states,
                "Invalid remote motor direction/header");
        std::vector<std::pair<std::uint64_t, MotorState>> states;
        for (std::uint64_t item = 0; item < total; ++item) {
            const auto id = r.u64();
            require(id != 0 && std::none_of(states.begin(), states.end(), [&](const auto& known) { return known.first == id; }),
                    "Remote motor identity");
            MotorState state;
            state.topology = index;
            state.tick = r.u64();
            state.epoch = r.u64();
            state.action = r.u64();
            const auto flags = r.u64();
            require(flags <= 7, "Remote motor flags");
            state.grounded = flags & 1;
            state.crouched = flags & 2;
            state.position = read_vector(r);
            state.yaw = r.number();
            state.pitch = r.number();
            state.achieved = read_vector(r);
            if (flags & 4)
                state.support_velocity = read_vector(r);
            validate_motor_state(state);
            states.emplace_back(id, state);
        }
        r.end();
        for (const auto& [id, state] : states)
            accept_motor(rev, id, state);
    } else if (kind == Kind::CollisionSnapshot) {
        require(role == SessionRole::Client && chunks >= 1 && chunks <= 33 && total <= 64 + session_character_capacity &&
                    chunks <= std::max<std::uint64_t>(1, total),
                "Collision snapshot header bounds");
        p.collision_required = true;
        auto topology = r.u64(), part = r.u64();
        require(topology && part < chunks, "Collision snapshot topology/chunk");
        if (rev != revision || (p.state != SessionReadiness::Ready && p.state != SessionReadiness::CatchUp) ||
            (!collision_states.empty() && index <= collision_states.back().tick))
            return;
        if (!p.collision_parts.empty() && index < p.collision_receiving_tick)
            return;
        if (p.collision_parts.empty() || index > p.collision_receiving_tick) {
            if (!p.collision_parts.empty()) {
                ++p.collision_stats.superseded;
                p.collision_stats.needs_resync = true;
            }
            p.collision_parts.clear();
            p.collision_partial_birth = timeout_clock();
            p.collision_receiving_tick = index;
            p.collision_receiving_topology = topology;
            p.collision_receiving_total = total;
            p.collision_receiving_chunks = chunks;
        }
        require(topology == p.collision_receiving_topology && total == p.collision_receiving_total &&
                    chunks == p.collision_receiving_chunks,
                "Collision snapshot inconsistent chunks");
        std::vector<std::byte> payload(r.bytes.begin() + r.at, r.bytes.end());
        auto existing = p.collision_parts.find(static_cast<unsigned>(part));
        if (existing != p.collision_parts.end()) {
            require(existing->second == payload, "Collision duplicate chunk differs");
            return;
        }
        p.collision_parts.emplace(static_cast<unsigned>(part), std::move(payload));
        if (p.collision_parts.size() == chunks) {
            CollisionStreamFrame frame;
            frame.tick = index;
            frame.topology = topology;
            std::uint64_t read = 0;
            for (unsigned n = 0; n < chunks; ++n) {
                Reader chunk{p.collision_parts.at(n)};
                while (chunk.at < chunk.bytes.size()) {
                    require(read < total, "Collision snapshot item count");
                    read_collision_item(chunk, frame);
                    ++read;
                }
                chunk.end();
            }
            require(read == total, "Collision snapshot item count");
            validate_collision_stream(frame);
            for (const auto& actor : frame.actors)
                require(objects.contains(actor.id), "Collision actor not replicated");
            ++p.collision_stats.decoded;
            collision_states.push_back(std::move(frame));
            while (collision_states.size() > 31)
                collision_states.pop_front();
            p.collision_parts.clear();
        }
    } else if (kind == Kind::CollisionAck) {
        require(role == SessionRole::Server && chunks == 0 && total == 0, "Collision ACK direction/header");
        auto topology = r.u64();
        r.end();
        if (rev < revision)
            return;
        require(rev == revision && p.state == SessionReadiness::Ready, "Collision ACK lifecycle revision");
        if (!p.collision_sent_frames.empty() && index < std::get<0>(p.collision_sent_frames.front()))
            return;
        auto found = std::find(p.collision_sent_frames.begin(), p.collision_sent_frames.end(), std::tuple{index, rev, topology});
        require(found != p.collision_sent_frames.end(), "Collision ACK not sent/retained");
        if (!p.collision_stats.has_ack || index > p.collision_stats.ack_tick) {
            p.collision_stats.has_ack = true;
            p.collision_stats.ack_tick = index;
            p.collision_stats.ack_topology = topology;
        }
    } else if (kind == Kind::OwnerService) {
        require(p.state == SessionReadiness::Ready || p.ever_ready, "Owner service before bootstrap");
        const auto channel = r.u64(), part = r.u64();
        require(channel <= UINT32_MAX && owner_channels.contains(std::uint32_t(channel)), "Unregistered owner service");
        if (role == SessionRole::Server) {
            auto owner = motor_inputs.find(index);
            require(owner != motor_inputs.end() && owner->second.owner == p.handle, "Owner service ownership denied");
        } else
            require(objects.contains(index) && objects.at(index).id == owner_channels.at(std::uint32_t(channel)),
                    "Owner service audience denied");
        auto message = p.owner_mailbox.receive(p.handle, rev, index, std::uint32_t(channel), part, chunks, total, r.bytes.subspan(r.at));
        if (message) {
            require(owner_messages.size() < 32, "Owner service inbox exhausted; drain before receive");
            owner_messages.push_back(std::move(*message));
        }
    } else if (kind == Kind::Interact) {
        // A request to use a placed interactable. It names the object and claims nothing; the host resolves it from its own state.
        require(role == SessionRole::Server && (p.state == SessionReadiness::Ready || p.ever_ready) && rev == 0 && index && chunks == 0 &&
                    total == 0,
                "Interaction request direction/readiness");
        const StableId object{r.u64(), r.u64()};
        r.end();
        require(bool(object), "Interaction request object identity");
        auto control = motor_inputs.find(index);
        require(control != motor_inputs.end() && control->second.owner == p.handle, "Interaction request ownership denied");
        if (p.state != SessionReadiness::Ready) {
            if (interaction_notices[p.handle].size() < 64)
                interaction_notices[p.handle].push_back({index, object, 3});
            return;
        }
        if (interaction_requests.size() < 16)
            interaction_requests.push_back({index, object});
        else if (interaction_notices[p.handle].size() < 64)
            interaction_notices[p.handle].push_back({index, object, 3});
    } else if (kind == Kind::InteractReceipt) {
        require(role == SessionRole::Client && index && chunks == 0 && total == 0, "Interaction receipt direction");
        InteractionNotice notice;
        notice.network = index;
        notice.object = StableId{r.u64(), r.u64()};
        const auto result = r.u64();
        r.end();
        require(bool(notice.object) && result <= 5 && received_interaction_notices.size() < 64, "Interaction receipt bounds/backlog");
        notice.result = std::uint8_t(result);
        received_interaction_notices.push_back(notice);
    } else if (kind == Kind::AbilityCommand) {
        require(role == SessionRole::Server && hello.protocol >= 3 && (p.state == SessionReadiness::Ready || p.ever_ready) && rev == 0 &&
                    index && chunks == 0 && total == 0,
                "Ability intent direction/profile/readiness");
        require(r.u64() == 1, "Ability intent schema");
        AbilityIntent intent;
        intent.network = index;
        intent.operation = r.u64();
        intent.tick = r.u64();
        intent.avatar_epoch = r.u64();
        intent.grant_generation = r.u64();
        auto slot = r.u64(), edge = r.u64(), flags = r.u64();
        r.end();
        require(intent.operation && intent.tick && intent.avatar_epoch && slot < combat_slot_count + loadout_slot_count &&
                    edge <= static_cast<unsigned>(InputEdge::Tapped) && flags <= 3 && intent.tick <= ability_tick + 8,
                "Ability intent bounded identity/lead/flags");
        intent.slot = static_cast<CombatSlot>(slot);
        intent.edge = static_cast<InputEdge>(edge);
        intent.cancelled = flags & 1;
        intent.replace_active = flags & 2;
        require(motor_inputs.contains(index) && motor_inputs.at(index).owner == p.handle && abilities.contains(index),
                "Ability intent ownership denied");
        const bool expired =
            p.state != SessionReadiness::Ready ||
            (latest_collision && (!p.collision_stats.has_ack || p.collision_stats.ack_topology != latest_collision->topology));
        auto& pending = ability_inputs[index];
        auto found = pending.find(intent.operation);
        if (found != pending.end()) {
            require(found->second.intent == intent && found->second.peer == p.handle, "Conflicting queued ability operation");
            return;
        }
        require(pending.size() < 32, "Ability intent queue exhausted; resynchronize");
        pending.emplace(intent.operation, QueuedAbility{intent, p.handle, false, expired});
    } else if (kind == Kind::AbilityReceipt) {
        require(role == SessionRole::Client && hello.protocol >= 3 && index && chunks == 0 && total == 0,
                "Ability receipt direction/profile");
        require(r.u64() == 1, "Ability receipt schema");
        AbilityOperationNotice notice;
        notice.network = index;
        auto& receipt = notice.receipt;
        receipt.operation = r.u64();
        receipt.tick = r.u64();
        receipt.inclusion_revision = r.u64();
        auto activation = r.u64(), failure = r.u64(), flags = r.u64();
        r.end();
        require(receipt.operation && failure <= static_cast<unsigned>(AbilityFailure::TagRequirements) && flags <= 7 &&
                    (!(flags & 1) || (failure == 0 && activation)),
                "Ability receipt bounds");
        receipt.failure = static_cast<AbilityFailure>(failure);
        receipt.committed = flags & 1;
        receipt.duplicate = flags & 2;
        notice.terminal = flags & 4;
        require(notice.terminal || receipt.failure == AbilityFailure::HistoryFull, "Nonterminal ability receipt policy");
        if (!objects.contains(index))
            return;
        receipt.handle.owner = {view->find(objects.at(index).id), hello.session_epoch, index};
        receipt.handle.activation = activation;
        require(received_ability_notices.size() < 128, "Ability receipt consumer backlog; drain/resynchronize");
        received_ability_notices.push_back(std::move(notice));
        if (!received_ability_notices.back().terminal)
            ability_resync = true;
    } else if (kind == Kind::AbilityCorrection) {
        require(role == SessionRole::Client && hello.protocol >= 3 && index && total > 0 && total <= 8192 &&
                    chunks == (total + ability_chunk_bytes - 1) / ability_chunk_bytes && chunks <= 10,
                "Ability correction envelope bounds/profile");
        auto ability_revision = r.u64(), part = r.u64();
        require(part < chunks, "Ability correction part index");
        auto first = part * ability_chunk_bytes;
        auto count = std::min<std::uint64_t>(ability_chunk_bytes, total - first);
        require(r.bytes.size() - r.at == count, "Ability correction part size");
        if (rev != revision || !objects.contains(index) || (p.state != SessionReadiness::Ready && p.state != SessionReadiness::CatchUp))
            return;
        auto current = ability_corrections.find(index);
        if (current != ability_corrections.end() && ability_revision <= current->second.ability.revision)
            return;
        auto found = p.ability_parts.find(index);
        if (found != p.ability_parts.end() && ability_revision < found->second.ability_revision)
            return;
        if (found == p.ability_parts.end() || found->second.ability_revision != ability_revision) {
            require(found != p.ability_parts.end() || p.ability_parts.size() < 4, "Ability correction owner capacity");
            p.ability_parts[index] = {rev, ability_revision, timeout_clock(), total, chunks, {}};
        }
        auto& partial = p.ability_parts.at(index);
        require(partial.world_revision == rev && partial.total == total && partial.chunks == chunks,
                "Conflicting ability correction fragments");
        std::vector<std::byte> bytes(r.bytes.begin() + r.at, r.bytes.end());
        auto [entry, inserted] = partial.parts.emplace(static_cast<unsigned>(part), bytes);
        require(inserted || entry->second == bytes, "Conflicting duplicate ability correction fragment");
        if (partial.parts.size() == chunks) {
            std::vector<std::byte> complete;
            complete.reserve(static_cast<std::size_t>(total));
            for (unsigned n = 0; n < chunks; ++n) {
                const auto& piece = partial.parts.at(n);
                complete.insert(complete.end(), piece.begin(), piece.end());
            }
            auto correction = session_detail::decode_ability_correction(complete);
            require(correction.network == index && correction.session_epoch == hello.session_epoch && correction.world_revision == rev &&
                        correction.ability.revision == ability_revision,
                    "Ability correction header/content identity");
            double health{}, maximum{};
            for (const auto& attribute : correction.ability.attributes) {
                if (attribute.id == correction.ability.health_attribute)
                    health = attribute.value;
                if (attribute.id == correction.ability.maximum_health_attribute)
                    maximum = attribute.value;
            }
            require(health == objects.at(index).health.current && maximum == objects.at(index).health.maximum,
                    "Ability correction Health/lifecycle baseline mismatch");
            correction.ability.owner.entity = view->find(objects.at(index).id);
            if (correction.ability.active)
                correction.ability.active->owner = correction.ability.owner;
            require(ability_corrections.contains(index) || ability_corrections.size() < 4, "Ability correction publication capacity");
            ability_corrections[index] = std::move(correction);
            p.ability_parts.erase(index);
            // The owner's motor state travels inside its correction; the motor stream does not repeat it.
            accept_motor(rev, index, ability_corrections.at(index).motor);
        }
    } else if (kind == Kind::AbilityPublic) {
        require(role == SessionRole::Client && hello.protocol >= 3 && index && total > 0 && total <= 4096 &&
                    chunks == (total + ability_chunk_bytes - 1) / ability_chunk_bytes && chunks <= 5,
                "Public ability envelope/profile/direction");
        auto ability_revision = r.u64(), avatar_epoch = r.u64(), part = r.u64();
        require(avatar_epoch && part < chunks, "Public ability fragment identity");
        auto first = part * ability_chunk_bytes;
        auto count = std::min<std::uint64_t>(ability_chunk_bytes, total - first);
        require(r.bytes.size() - r.at == count, "Public ability fragment byte count");
        if (rev != revision || !objects.contains(index) || (p.state != SessionReadiness::Ready && p.state != SessionReadiness::CatchUp))
            return;
        auto current = public_abilities.find(index);
        if (current != public_abilities.end() &&
            (avatar_epoch < current->second.avatar_epoch ||
             (avatar_epoch == current->second.avatar_epoch && ability_revision <= current->second.ability.revision)))
            return;
        auto found = p.public_parts.find(index);
        if (found != p.public_parts.end() &&
            (avatar_epoch < found->second.avatar_epoch ||
             (avatar_epoch == found->second.avatar_epoch && ability_revision < found->second.ability_revision)))
            return;
        if (found == p.public_parts.end() || found->second.ability_revision != ability_revision ||
            found->second.avatar_epoch != avatar_epoch) {
            require(found != p.public_parts.end() || p.public_parts.size() < session_character_capacity, "Public ability partial capacity");
            p.public_parts[index] = {rev, ability_revision, avatar_epoch, timeout_clock(), total, chunks, {}};
        }
        auto& partial = p.public_parts.at(index);
        require(partial.world_revision == rev && partial.total == total && partial.chunks == chunks,
                "Conflicting public ability fragments");
        std::vector<std::byte> bytes(r.bytes.begin() + r.at, r.bytes.end());
        auto [entry, inserted] = partial.parts.emplace(static_cast<unsigned>(part), bytes);
        require(inserted || entry->second == bytes, "Conflicting duplicate public ability fragment");
        if (partial.parts.size() == chunks) {
            std::vector<std::byte> complete;
            complete.reserve(static_cast<std::size_t>(total));
            for (unsigned n = 0; n < chunks; ++n) {
                const auto& piece = partial.parts.at(n);
                complete.insert(complete.end(), piece.begin(), piece.end());
            }
            publish_public(index, rev, ability_revision, avatar_epoch, complete);
            p.public_parts.erase(index);
        }
    } else if (kind == Kind::EffectOwner || kind == Kind::EffectPublic) {
        require(role == SessionRole::Client && hello.protocol >= 3 && index && total > 0 && total <= 16384 &&
                    chunks == (total + ability_chunk_bytes - 1) / ability_chunk_bytes && chunks <= 19,
                "Effect frame envelope/profile/direction");
        auto frame_revision = r.u64(), avatar_epoch = r.u64(), part = r.u64();
        require(avatar_epoch && part < chunks, "Effect frame fragment identity");
        auto first = part * ability_chunk_bytes;
        auto count = std::min<std::uint64_t>(ability_chunk_bytes, total - first);
        require(r.bytes.size() - r.at == count, "Effect frame fragment size");
        if (rev != revision || !objects.contains(index) || (p.state != SessionReadiness::Ready && p.state != SessionReadiness::CatchUp))
            return;
        auto audience = kind == Kind::EffectOwner ? AttributeVisibility::Owner : AttributeVisibility::Public;
        EffectKey key{index, audience};
        auto& frames = audience == AttributeVisibility::Owner ? owner_effect_frames : public_effect_frames;
        auto current = frames.find(index);
        if (current != frames.end() && (avatar_epoch < current->second.avatar_epoch ||
                                        (avatar_epoch == current->second.avatar_epoch && frame_revision <= current->second.revision)))
            return;
        auto found = p.effect_parts.find(key);
        if (found != p.effect_parts.end() &&
            (avatar_epoch < found->second.avatar_epoch ||
             (avatar_epoch == found->second.avatar_epoch && frame_revision < found->second.ability_revision)))
            return;
        if (found == p.effect_parts.end() || found->second.ability_revision != frame_revision ||
            found->second.avatar_epoch != avatar_epoch) {
            require(found != p.effect_parts.end() || p.effect_parts.size() < 8, "Effect partial actor/audience capacity");
            p.effect_parts[key] = {rev, frame_revision, avatar_epoch, timeout_clock(), total, chunks, {}};
        }
        auto& partial = p.effect_parts.at(key);
        require(partial.world_revision == rev && partial.total == total && partial.chunks == chunks, "Conflicting effect fragments");
        std::vector<std::byte> bytes(r.bytes.begin() + r.at, r.bytes.end());
        auto [entry, inserted] = partial.parts.emplace(static_cast<unsigned>(part), bytes);
        require(inserted || entry->second == bytes, "Conflicting duplicate effect fragment");
        if (partial.parts.size() == chunks) {
            std::vector<std::byte> complete;
            complete.reserve(static_cast<std::size_t>(total));
            for (unsigned n = 0; n < chunks; ++n) {
                const auto& piece = partial.parts.at(n);
                complete.insert(complete.end(), piece.begin(), piece.end());
            }
            auto frame = session_detail::decode_effect_frame(complete);
            require(frame.owner.network == index && frame.owner.session_epoch == hello.session_epoch && frame.world_revision == rev &&
                        frame.avatar_epoch == avatar_epoch && frame.revision == frame_revision && frame.audience == audience,
                    "Effect frame header/content identity");
            frame.owner.entity = view->find(objects.at(index).id);
            require(frames.contains(index) || frames.size() < session_character_capacity, "Effect actor publication capacity");
            frames[index] = std::move(frame);
            effect_resync.erase(key);
            p.effect_parts.erase(key);
        }
    } else if (kind == Kind::AbilityCorrectionAck) {
        require(role == SessionRole::Server && hello.protocol >= 3 && index && chunks == 0 && total == 0,
                "Ability correction ACK direction/profile");
        auto ability_revision = r.u64(), through = r.u64();
        r.end();
        if (rev < revision)
            return;
        require(rev == revision && p.state == SessionReadiness::Ready && motor_inputs.contains(index) &&
                    motor_inputs.at(index).owner == p.handle,
                "Ability correction ACK ownership/lifecycle");
        auto tuple = std::tuple{index, rev, ability_revision, through};
        auto sent = std::find(p.ability_sent_frames.begin(), p.ability_sent_frames.end(), tuple);
        if (sent == p.ability_sent_frames.end()) {
            if (!p.ability_sent_frames.empty() && ability_revision < std::get<2>(p.ability_sent_frames.front()))
                return;
            require(false, "Ability correction ACK not sent/retained");
        }
        require(abilities.contains(index), "Ability correction ACK owner retired");
        auto snapshot = abilities.at(index)->snapshot();
        if (through > snapshot.retired_through) {
            auto candidate = std::make_unique<AbilityState>(*abilities.at(index));
            candidate->retire(through);
            commit_ability(index, std::move(candidate), {});
        }
    } else if (kind == Kind::PublicBatch) {
        require(role == SessionRole::Client && hello.protocol >= 3 && chunks == 0 && index == 0 && total >= 1 &&
                    total <= session_character_capacity,
                "Public batch direction/bounds");
        // Several whole public ability frames in one packet; each is taken exactly like a frame that arrived on its own.
        struct Entry {
            std::uint64_t id{}, ability_revision{}, avatar_epoch{};
            std::vector<std::byte> bytes;
        };
        std::vector<Entry> entries;
        for (std::uint64_t item = 0; item < total; ++item) {
            Entry entry;
            entry.id = r.u64();
            entry.ability_revision = r.u64();
            entry.avatar_epoch = r.u64();
            const auto size = r.u64();
            require(entry.id && entry.avatar_epoch && size > 0 && size <= r.bytes.size() - r.at, "Public batch entry bounds");
            entry.bytes.assign(r.bytes.begin() + r.at, r.bytes.begin() + r.at + size);
            r.at += size;
            entries.push_back(std::move(entry));
        }
        r.end();
        if (rev != revision || (p.state != SessionReadiness::Ready && p.state != SessionReadiness::CatchUp))
            return;
        for (const auto& entry : entries) {
            if (!objects.contains(entry.id))
                continue;
            auto current = public_abilities.find(entry.id);
            if (current != public_abilities.end() &&
                (entry.avatar_epoch < current->second.avatar_epoch ||
                 (entry.avatar_epoch == current->second.avatar_epoch && entry.ability_revision <= current->second.ability.revision)))
                continue;
            p.public_parts.erase(entry.id);
            publish_public(entry.id, rev, entry.ability_revision, entry.avatar_epoch, entry.bytes);
        }
    } else if (kind == Kind::PublicIndex) {
        require(role == SessionRole::Client && hello.protocol >= 3 && chunks == 0 && index == 0 && total <= session_character_capacity,
                "Public index direction/bounds");
        std::vector<std::pair<std::uint64_t, PublicIndexEntry>> entries;
        for (std::uint64_t item = 0; item < total; ++item) {
            const auto id = r.u64();
            PublicIndexEntry entry;
            entry.tick = r.u64();
            entry.content = r.u64();
            require(id != 0, "Public index identity");
            entries.push_back({id, entry});
        }
        r.end();
        if (rev != revision)
            return;
        for (const auto& [id, entry] : entries) {
            auto& held = public_index[id];
            if (entry.tick >= held.tick)
                held = entry;
        }
    } else if (kind == Kind::Projectiles) {
        require(role == SessionRole::Client && hello.protocol >= 3 && chunks == 0 && total <= projectile_capacity,
                "Projectile frame direction/bounds");
        std::vector<ProjectileView> frame;
        for (std::uint64_t item = 0; item < total; ++item) {
            ProjectileView view;
            view.id = r.u64();
            view.position = read_vector(r);
            view.velocity = read_vector(r);
            for (double n : {view.position.x, view.position.y, view.position.z, view.velocity.x, view.velocity.y, view.velocity.z})
                require(std::isfinite(n) && std::abs(n) <= 100000, "Projectile frame value bounds");
            require(view.id != 0, "Projectile frame identity");
            frame.push_back(view);
        }
        r.end();
        if (rev != revision || index < projectile_tick)
            return;
        projectile_tick = index;
        projectile_views = std::move(frame);
    } else
        throw std::runtime_error("Unapproved client operation/session message");
    p.progress = timeout_clock();
}

auto WorldSession::Impl::accept_motor(std::uint64_t rev, std::uint64_t id, const MotorState& state) -> void {
    if (rev != revision || !objects.contains(id))
        return;
    auto it = motor_states.find(id);
    if (it != motor_states.end() && (state.epoch < it->second.epoch || (state.epoch == it->second.epoch && state.tick <= it->second.tick)))
        return;
    require(motor_states.size() < session_character_capacity || it != motor_states.end(), "Motor snapshot capacity");
    auto transform = objects.at(id).transform;
    transform.x = state.position.x;
    transform.y = state.position.y;
    transform.z = state.position.z;
    transform.yaw = state.yaw;
    view->apply_replica_transform(view->find(objects.at(id).id), transform);
    objects.at(id).transform = transform;
    motor_states[id] = state;
    // The public frame of the same tick is paired with it.
    auto shown = public_abilities.find(id);
    if (shown != public_abilities.end() && shown->second.avatar_epoch == state.epoch && shown->second.ability.tick == state.tick)
        shown->second.motor = state;
}

auto WorldSession::Impl::pump(Peer& p) -> void {
    if (!p.ready_sent) {
        auto w = header(Kind::Hello, hello);
        w.u64(hello.protocol);
        for (const auto* hash : {&hello.schema_hash, &hello.content_hash})
            for (char c : *hash)
                w.bytes.push_back(static_cast<std::byte>(c));
        w.u64(role == SessionRole::Server && latest_collision.has_value() ? 1 : 0);
        if (send(p, std::move(w)))
            p.ready_sent = true;
        return;
    }
    if (!p.greeting)
        return;
    if (role == SessionRole::Client) {
        if (p.state == SessionReadiness::Ready)
            for (auto it = ability_ack_pending.begin(); it != ability_ack_pending.end();) {
                auto [world_revision, ability_revision, through] = it->second;
                if (world_revision != revision) {
                    it = ability_ack_pending.erase(it);
                    continue;
                }
                auto w = header(Kind::AbilityCorrectionAck, hello, world_revision, it->first);
                w.u64(ability_revision);
                w.u64(through);
                if (!send(p, std::move(w)))
                    break;
                it = ability_ack_pending.erase(it);
            }
        if (p.collision_ack_pending && p.state == SessionReadiness::Ready) {
            auto frame = std::find_if(collision_states.begin(), collision_states.end(),
                                      [&](const auto& value) { return value.tick == *p.collision_ack_pending; });
            require(frame != collision_states.end(), "Prepared collision ACK expired before send");
            auto w = header(Kind::CollisionAck, hello, revision, frame->tick);
            w.u64(frame->topology);
            if (send(p, std::move(w))) {
                p.collision_stats.has_ack = true;
                p.collision_stats.ack_tick = frame->tick;
                p.collision_stats.ack_topology = frame->topology;
                p.collision_stats.needs_resync = false;
                p.collision_ack_pending.reset();
            }
        }
        if (!p.content_sent) {
            if (send(p, header(Kind::ContentReady, hello))) {
                p.content_sent = true;
                p.state = SessionReadiness::Bootstrap;
            }
            return;
        }
        if (p.await_ack && send(p, header(Kind::Ack, hello, revision)))
            p.await_ack = false;
        return;
    }
    if (!p.content_sent || p.await_ack)
        return;
    if (p.sending.empty() && p.next_chunk == 0 && p.state != SessionReadiness::Bootstrap && p.acked_revision == revision) {
        if (p.state != SessionReadiness::Ready && send(p, header(Kind::Ready, hello, revision))) {
            p.state = SessionReadiness::Ready;
            p.ever_ready = true;
        }
        return;
    }
    if (p.next_chunk == 0) {
        p.sending.clear();
        for (const auto& [id, o] : objects)
            p.sending.push_back(o);
        if (p.sent_revision != revision)
            p.collision_stats.has_ack = false;
        p.sent_revision = revision;
        p.chunks = std::max<std::size_t>(1, (p.sending.size() + per_chunk - 1) / per_chunk);
        p.state = SessionReadiness::Bootstrap;
    }
    for (std::size_t work = 0; work < limits.packets_per_tick && p.next_chunk < p.chunks; ++work) {
        auto w = header(Kind::Baseline, hello, p.sent_revision, p.next_chunk, p.chunks, p.sending.size());
        auto first = p.next_chunk * per_chunk;
        for (auto i = first; i < std::min(first + per_chunk, p.sending.size()); ++i)
            write_object(w, p.sending[i]);
        if (!send(p, std::move(w), Delivery::ReliableBootstrap))
            break;
        ++p.next_chunk;
        p.progress = timeout_clock();
    }
    if (p.next_chunk == p.chunks) {
        p.await_ack = true;
        p.next_chunk = 0;
        p.sending.clear();
    }
}
} // namespace darkangel
