#include "world_session_impl.hpp"

namespace darkangel {
auto WorldSession::Impl::pump_ability_notices(Peer& p) -> void {
    auto found = ability_notices.find(p.handle);
    if (found == ability_notices.end())
        return;
    while (!found->second.empty()) {
        const auto& notice = found->second.front();
        const auto& receipt = notice.receipt;
        auto w = header(Kind::AbilityReceipt, hello, revision, notice.network);
        w.u64(1);
        for (auto value :
             {receipt.operation, receipt.tick, receipt.inclusion_revision, receipt.handle.activation, std::uint64_t(receipt.failure),
              std::uint64_t(receipt.committed) | std::uint64_t(receipt.duplicate) * 2 | std::uint64_t(notice.terminal) * 4})
            w.u64(value);
        if (!send(p, std::move(w)))
            return;
        found->second.pop_front();
    }
    ability_notices.erase(found);
}

auto WorldSession::Impl::publish_public(std::uint64_t index, std::uint64_t rev, std::uint64_t ability_revision, std::uint64_t avatar_epoch,
                                        const std::vector<std::byte>& complete) -> void {
    auto frame = session_detail::decode_ability_public(complete);
    require(frame.network == index && frame.session_epoch == hello.session_epoch && frame.world_revision == rev &&
                frame.avatar_epoch == avatar_epoch && frame.ability.revision == ability_revision,
            "Public ability header/content identity");
    {
        auto held = motor_states.find(index);
        if (held != motor_states.end())
            frame.motor = held->second;
        else {
            frame.motor.epoch = avatar_epoch;
            const auto& at = objects.at(index).transform;
            frame.motor.position = {at.x, at.y, at.z};
            frame.motor.yaw = at.yaw;
        }
    }
    double health{}, maximum{};
    for (const auto& value : frame.ability.attributes) {
        if (value.id == frame.ability.health_attribute)
            health = value.value;
        if (value.id == frame.ability.maximum_health_attribute)
            maximum = value.value;
    }
    require(health == objects.at(index).health.current && maximum == objects.at(index).health.maximum,
            "Public ability Health/lifecycle baseline mismatch");
    frame.ability.owner.entity = view->find(objects.at(index).id);
    if (frame.ability.active)
        frame.ability.active->owner = frame.ability.owner;
    require(public_abilities.contains(index) || public_abilities.size() < session_character_capacity,
            "Public ability publication capacity");
    public_abilities[index] = std::move(frame);
    public_resync.erase(index);
}

auto WorldSession::Impl::pump_abilities(Peer& p) -> void {
    if (hello.protocol < 3 || p.state != SessionReadiness::Ready)
        return;
    auto owned = [&](std::uint64_t id) {
        return abilities.contains(id) && motor_inputs.contains(id) && motor_inputs.at(id).owner == p.handle;
    };
    std::erase_if(p.ability_sent, [&](const auto& item) { return !owned(item.first); });
    std::erase_if(p.ability_sending, [&](const auto& item) { return !owned(item.first); });
    for (const auto& [id, state] : abilities) {
        auto control = motor_inputs.find(id);
        if (control == motor_inputs.end() || control->second.owner != p.handle || !motor_states.contains(id))
            continue;
        auto snapshot = state->owner_snapshot();
        if (snapshot.tick != motor_states.at(id).tick || !state->pending_hits.empty())
            continue;
        auto sending = p.ability_sending.find(id);
        if (sending != p.ability_sending.end() && sending->second.world_revision != revision) {
            p.ability_sending.erase(sending);
            sending = p.ability_sending.end();
        }
        if (sending == p.ability_sending.end()) {
            auto sent = p.ability_sent.find(id);
            if (sent != p.ability_sent.end() && sent->second == std::pair{snapshot.revision, revision} && clock % 30 != 0)
                continue;
            AbilityCorrection correction{id, hello.session_epoch, revision, motor_states.at(id), snapshot};
            auto bytes = session_detail::encode_ability_correction(correction);
            p.ability_sending[id] = {revision, snapshot.revision, snapshot.highest_operation, std::move(bytes), 0};
            p.correction_tick[id] = snapshot.tick;
        }
        auto& frame = p.ability_sending.at(id);
        auto chunks = (frame.bytes.size() + ability_chunk_bytes - 1) / ability_chunk_bytes;
        for (; frame.next < chunks; ++frame.next) {
            auto w = header(Kind::AbilityCorrection, hello, frame.world_revision, id, chunks, frame.bytes.size());
            w.u64(frame.ability_revision);
            w.u64(frame.next);
            auto first = frame.next * ability_chunk_bytes;
            w.bytes.insert(w.bytes.end(), frame.bytes.begin() + first,
                           frame.bytes.begin() + std::min(frame.bytes.size(), first + ability_chunk_bytes));
            require(w.bytes.size() <= packet_limit, "Ability correction packet budget");
            if (!send(p, std::move(w), Delivery::UnreliableState))
                return;
        }
        p.ability_sent[id] = {frame.ability_revision, frame.world_revision};
        auto tuple = std::tuple{id, frame.world_revision, frame.ability_revision, frame.highest_operation};
        if (p.ability_sent_frames.empty() || p.ability_sent_frames.back() != tuple)
            p.ability_sent_frames.push_back(tuple);
        while (p.ability_sent_frames.size() > 64)
            p.ability_sent_frames.pop_front();
        p.correction_motor[id] = {p.correction_tick[id], frame.world_revision};
        p.ability_sending.erase(id);
    }
}

auto WorldSession::Impl::pair_motor(Peer& p, std::uint64_t id) -> void {
    auto sent = p.motor_sent.find(id);
    auto state = motor_states.find(id);
    if (sent != p.motor_sent.end() && state != motor_states.end() && sent->second.first != state->second.tick)
        p.motor_sent.erase(sent);
}

auto WorldSession::Impl::pump_public_abilities(Peer& p) -> void {
    if (hello.protocol < 3 || p.state != SessionReadiness::Ready)
        return;
    std::erase_if(p.public_sent, [&](const auto& item) { return !abilities.contains(item.first); });
    std::erase_if(p.public_content, [&](const auto& item) { return !abilities.contains(item.first); });
    std::erase_if(p.public_sending, [&](const auto& item) { return !abilities.contains(item.first); });
    // Frames that fit a packet together travel together; one is marked sent only when its packet has gone out.
    struct Packed {
        std::uint64_t id{}, ability_revision{}, avatar_epoch{}, content{};
        std::vector<std::byte> bytes;
    };
    std::vector<Packed> batch;
    std::size_t batch_bytes = header_size;
    auto flush = [&] {
        if (batch.empty())
            return true;
        auto w = header(Kind::PublicBatch, hello, revision, 0, 0, batch.size());
        for (const auto& entry : batch) {
            w.u64(entry.id);
            w.u64(entry.ability_revision);
            w.u64(entry.avatar_epoch);
            w.u64(entry.bytes.size());
            w.bytes.insert(w.bytes.end(), entry.bytes.begin(), entry.bytes.end());
        }
        require(w.bytes.size() <= packet_limit, "Public batch packet budget");
        if (!send(p, std::move(w), Delivery::UnreliableState))
            return false;
        for (const auto& entry : batch) {
            p.public_sent[entry.id] = {entry.ability_revision, revision, entry.avatar_epoch};
            p.public_content[entry.id] = entry.content;
            pair_motor(p, entry.id);
        }
        batch.clear();
        batch_bytes = header_size;
        return true;
    };
    for (const auto& [id, state] : abilities) {
        if (!motor_states.contains(id) || !state->pending_hits.empty())
            continue;
        auto snapshot = state->public_snapshot();
        const auto& motor = motor_states.at(id);
        if (snapshot.tick != motor.tick)
            continue;
        auto sending = p.public_sending.find(id);
        if (sending != p.public_sending.end() &&
            (sending->second.world_revision != revision || sending->second.avatar_epoch != motor.epoch)) {
            p.public_sending.erase(sending);
            sending = p.public_sending.end();
        }
        if (sending == p.public_sending.end()) {
            const auto content = public_ability_content(snapshot);
            auto sent = p.public_sent.find(id);
            auto known = p.public_content.find(id);
            // A frame goes out when its content changed, not when only its clock did; the index tells the client the held frame is current.
            if (sent != p.public_sent.end() && std::get<1>(sent->second) == revision && std::get<2>(sent->second) == motor.epoch &&
                known != p.public_content.end() && known->second == content && clock % 30 != 0)
                continue;
            auto snapshot_revision = snapshot.revision;
            AbilityPublicFrame outgoing{id, hello.session_epoch, revision, {}, std::move(snapshot)};
            outgoing.avatar_epoch = motor.epoch;
            auto bytes = session_detail::encode_ability_public(outgoing);
            if (header_size + 32 + bytes.size() <= packet_limit) {
                if (batch_bytes + 32 + bytes.size() > packet_limit && !flush())
                    return;
                batch_bytes += 32 + bytes.size();
                batch.push_back({id, snapshot_revision, motor.epoch, content, std::move(bytes)});
                continue;
            }
            p.public_content[id] = content;
            p.public_sending[id] = {revision, snapshot_revision, motor.epoch, std::move(bytes), 0};
        }
        auto& frame = p.public_sending.at(id);
        auto chunks = (frame.bytes.size() + ability_chunk_bytes - 1) / ability_chunk_bytes;
        for (; frame.next < chunks; ++frame.next) {
            auto w = header(Kind::AbilityPublic, hello, frame.world_revision, id, chunks, frame.bytes.size());
            w.u64(frame.ability_revision);
            w.u64(frame.avatar_epoch);
            w.u64(frame.next);
            auto first = frame.next * ability_chunk_bytes;
            w.bytes.insert(w.bytes.end(), frame.bytes.begin() + first,
                           frame.bytes.begin() + std::min(frame.bytes.size(), first + ability_chunk_bytes));
            require(w.bytes.size() <= packet_limit, "Public ability packet budget");
            if (!send(p, std::move(w), Delivery::UnreliableState))
                return;
        }
        p.public_sent[id] = {frame.ability_revision, frame.world_revision, frame.avatar_epoch};
        pair_motor(p, id);
        p.public_sending.erase(id);
    }
    flush();
}

auto WorldSession::Impl::pump_public_index(Peer& p) -> void {
    if (hello.protocol < 3 || p.state != SessionReadiness::Ready)
        return;
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> entries;
    for (const auto& [id, state] : abilities) {
        if (!motor_states.contains(id) || !state->pending_hits.empty())
            continue;
        auto snapshot = state->public_snapshot();
        if (snapshot.tick != motor_states.at(id).tick)
            continue;
        entries.push_back({id, snapshot.tick, public_ability_content(snapshot)});
    }
    if (entries.empty() || (entries == p.index_sent && clock % 30 != 0))
        return;
    require(entries.size() <= session_character_capacity, "Public index capacity");
    auto w = header(Kind::PublicIndex, hello, revision, 0, 0, entries.size());
    for (const auto& [id, tick, content] : entries) {
        w.u64(id);
        w.u64(tick);
        w.u64(content);
    }
    require(w.bytes.size() <= packet_limit, "Public index packet budget");
    if (send(p, std::move(w), Delivery::UnreliableState))
        p.index_sent = std::move(entries);
}

auto WorldSession::Impl::pump_effects(Peer& p) -> void {
    if (hello.protocol < 3 || p.state != SessionReadiness::Ready)
        return;
    auto eligible = [&](const EffectKey& key) {
        return abilities.contains(key.first) && motor_states.contains(key.first) &&
               (key.second == AttributeVisibility::Public ||
                (motor_inputs.contains(key.first) && motor_inputs.at(key.first).owner == p.handle));
    };
    std::erase_if(p.effect_sent, [&](const auto& item) { return !eligible(item.first); });
    std::erase_if(p.effect_sending, [&](const auto& item) { return !eligible(item.first); });
    for (const auto& [id, state] : abilities) {
        if (!motor_states.contains(id) || !state->pending_hits.empty())
            continue;
        auto snapshot = state->snapshot();
        const auto& motor = motor_states.at(id);
        if (snapshot.tick != motor.tick)
            continue;
        for (auto audience : {AttributeVisibility::Owner, AttributeVisibility::Public}) {
            EffectKey key{id, audience};
            if (!eligible(key))
                continue;
            auto sending = p.effect_sending.find(key);
            if (sending != p.effect_sending.end() &&
                (sending->second.state.world_revision != revision || sending->second.state.avatar_epoch != motor.epoch)) {
                p.effect_sending.erase(sending);
                sending = p.effect_sending.end();
            }
            if (sending != p.effect_sending.end())
                continue;
            std::vector<EffectReplica> effects;
            for (const auto& effect : state->effects().snapshot(audience))
                effects.push_back({effect.handle.value,
                                   effect.definition,
                                   effect.generation,
                                   {effect.credit.session_epoch, effect.credit.source_network, effect.credit.activation},
                                   effect.start,
                                   effect.end,
                                   effect.next_period,
                                   effect.suppressed});
            auto sent = p.effect_sent.find(key);
            if (effects.empty() && sent == p.effect_sent.end())
                continue;
            if (sent != p.effect_sent.end() && sent->second.world_revision == revision && sent->second.avatar_epoch == motor.epoch &&
                sent->second.effects == effects && clock % 30 != 0)
                continue;
            EffectFrame frame{state->owner(), revision, motor.epoch, snapshot.tick, snapshot.revision, audience, effects};
            auto bytes = session_detail::encode_effect_frame(frame);
            p.effect_sending[key] = {snapshot.revision, {revision, motor.epoch, std::move(effects)}, std::move(bytes), 0};
        }
    }
    // At most a quarter of the shared packet budget; round-robin fragments
    // across actors/audiences prevent a dense status set starving another.
    auto quota = std::max<std::size_t>(1, limits.packets_per_tick / 4);
    for (std::size_t work = 0; work < quota && !p.effect_sending.empty(); ++work) {
        auto it = p.effect_sending.upper_bound(p.effect_cursor);
        if (it == p.effect_sending.end())
            it = p.effect_sending.begin();
        auto key = it->first;
        auto& frame = it->second;
        auto chunks = (frame.bytes.size() + ability_chunk_bytes - 1) / ability_chunk_bytes;
        auto w = header(key.second == AttributeVisibility::Owner ? Kind::EffectOwner : Kind::EffectPublic, hello,
                        frame.state.world_revision, key.first, chunks, frame.bytes.size());
        w.u64(frame.revision);
        w.u64(frame.state.avatar_epoch);
        w.u64(frame.next);
        auto first = frame.next * ability_chunk_bytes;
        w.bytes.insert(w.bytes.end(), frame.bytes.begin() + first,
                       frame.bytes.begin() + std::min(frame.bytes.size(), first + ability_chunk_bytes));
        require(w.bytes.size() <= packet_limit, "Effect fragment packet bound");
        if (!send(p, std::move(w), Delivery::UnreliableState))
            return;
        p.effect_cursor = key;
        if (++frame.next == chunks) {
            p.effect_sent[key] = std::move(frame.state);
            p.effect_sending.erase(it);
        }
    }
}

auto WorldSession::Impl::pump_projectiles(Peer& p) -> void {
    if (hello.protocol < 3 || p.state != SessionReadiness::Ready || !projectile_revision ||
        (p.projectile_sent == projectile_revision && (projectile_views.empty() || clock % 30 != 0)))
        return;
    auto w = header(Kind::Projectiles, hello, revision, projectile_tick, 0, projectile_views.size());
    for (const auto& view : projectile_views) {
        w.u64(view.id);
        write_vector(w, view.position);
        write_vector(w, view.velocity);
    }
    require(w.bytes.size() <= packet_limit, "Projectile frame packet budget");
    if (send(p, std::move(w), Delivery::UnreliableState))
        p.projectile_sent = projectile_revision;
}

const std::vector<ProjectileView>& WorldSession::projectiles() const {
    impl_->thread();
    return impl_->projectile_views;
}

const std::map<std::uint64_t, AbilityCorrection>& WorldSession::ability_corrections() const {
    impl_->thread();
    return impl_->ability_corrections;
}

const std::map<std::uint64_t, AbilityPublicFrame>& WorldSession::public_abilities() const {
    impl_->thread();
    return impl_->public_abilities;
}

const std::map<std::uint64_t, PublicIndexEntry>& WorldSession::public_index() const {
    impl_->thread();
    return impl_->public_index;
}

const std::map<std::uint64_t, EffectFrame>& WorldSession::effect_frames(AttributeVisibility audience) const {
    impl_->thread();
    require(audience == AttributeVisibility::Owner || audience == AttributeVisibility::Public, "Effect frame audience");
    return audience == AttributeVisibility::Owner ? impl_->owner_effect_frames : impl_->public_effect_frames;
}

bool WorldSession::effect_frame_needs_resync(std::uint64_t network, AttributeVisibility audience) const {
    impl_->thread();
    require(audience == AttributeVisibility::Owner || audience == AttributeVisibility::Public, "Effect resync audience");
    return impl_->effect_resync.contains({network, audience});
}

bool WorldSession::public_ability_needs_resync(std::uint64_t network) const {
    impl_->thread();
    return !impl_->public_abilities.contains(network) || impl_->public_resync.contains(network);
}

bool WorldSession::ability_correction_needs_resync() const {
    impl_->thread();
    return impl_->ability_resync;
}

void WorldSession::acknowledge_ability_correction(std::uint64_t network) {
    auto& s = *impl_;
    s.thread();
    require(s.role == SessionRole::Client && s.hello.protocol >= 3 && s.ability_corrections.contains(network) && !s.peers.empty() &&
                s.peers.begin()->second.state == SessionReadiness::Ready,
            "Ability correction ACK requires a complete owner bundle");
    const auto& correction = s.ability_corrections.at(network);
    require(correction.world_revision == s.revision && s.view->valid(correction.ability.owner.entity),
            "Ability correction ACK stale lifecycle");
    s.ability_ack_pending[network] = {correction.world_revision, correction.ability.revision, correction.ability.highest_operation};
    s.ability_resync = false;
}

bool WorldSession::submit_ability_intent(const AbilityIntent& intent) {
    auto& s = *impl_;
    s.thread();
    require(s.role == SessionRole::Client && s.hello.protocol >= 3 && s.peers.size() == 1, "Ability intent requires a protocol-3 client");
    auto& peer = s.peers.begin()->second;
    require(intent.network && intent.operation && intent.tick && intent.avatar_epoch &&
                static_cast<unsigned>(intent.slot) < combat_slot_count + loadout_slot_count &&
                static_cast<unsigned>(intent.edge) <= static_cast<unsigned>(InputEdge::Tapped),
            "Ability intent identity/edge");
    if (peer.state != SessionReadiness::Ready || s.ability_resync || !s.ability_corrections.contains(intent.network))
        return false;
    const auto& correction = s.ability_corrections.at(intent.network);
    if (correction.world_revision != s.revision || intent.operation <= correction.ability.retired_through ||
        intent.tick > correction.ability.tick + 8)
        return false;
    if (!collision_control_ready())
        return false;
    auto w = header(Kind::AbilityCommand, s.hello, 0, intent.network);
    w.u64(1);
    for (auto value : {intent.operation, intent.tick, intent.avatar_epoch, intent.grant_generation, std::uint64_t(intent.slot),
                       std::uint64_t(intent.edge), std::uint64_t(intent.cancelled) | std::uint64_t(intent.replace_active) * 2})
        w.u64(value);
    return s.send(peer, std::move(w));
}

std::vector<AbilityOperationNotice> WorldSession::drain_ability_receipts() {
    auto& s = *impl_;
    s.thread();
    require(s.role == SessionRole::Client, "Ability wire receipts are a client view");
    auto result = std::move(s.received_ability_notices);
    s.received_ability_notices.clear();
    for (auto& notice : result) {
        auto object = s.objects.find(notice.network);
        notice.receipt.handle.owner.entity = object == s.objects.end() ? EntityHandle{} : s.view->find(object->second.id);
    }
    return result;
}
} // namespace darkangel
