#include "world_session_impl.hpp"

namespace darkangel {
auto WorldSession::Impl::ability(AbilityOwnerHandle h) const -> const AbilityState& {
    thread();
    require(role == SessionRole::Server, "Ability API requires server authority");
    auto it = abilities.find(h.network);
    require(h.session_epoch == hello.session_epoch && view->valid(h.entity) && it != abilities.end() && it->second->owner() == h,
            "Stale/wrong-domain ability owner");
    return *it->second;
}

auto WorldSession::Impl::queue_actions(std::span<const AbilityActionUpdate> updates) const -> std::vector<AbilityActionUpdate> {
    auto candidate = ability_actions;
    candidate.insert(candidate.end(), updates.begin(), updates.end());
    require(candidate.size() <= 128, "Ability lifecycle queue full; drain or resync");
    std::size_t work{};
    for (const auto& update : candidate)
        work += update.batch.events.size() + update.batch.traversed.size();
    require(work <= 2048, "Ability action work queue full; drain or resync");
    return candidate;
}

auto WorldSession::Impl::queue_commitments(std::span<AbilityState* const> states) const -> std::vector<AbilityCommitUpdate> {
    auto candidate = ability_commitments;
    for (auto state : states) {
        auto updates = state->take_commitments();
        candidate.insert(candidate.end(), updates.begin(), updates.end());
    }
    require(candidate.size() <= 128, "Ability commitment queue full; drain before publication");
    return candidate;
}

auto WorldSession::Impl::queue_commitments(const std::map<std::uint64_t, std::unique_ptr<AbilityState>>& states) const
    -> std::vector<AbilityCommitUpdate> {
    std::vector<AbilityState*> values;
    for (const auto& [id, state] : states)
        values.push_back(state.get());
    return queue_commitments(values);
}

auto WorldSession::Impl::commit_ability(std::uint64_t id, std::unique_ptr<AbilityState> candidate,
                                        std::span<const AbilityActionUpdate> updates) -> void {
    auto queue = queue_actions(updates);
    std::array<AbilityState*, 1> states{candidate.get()};
    auto commitments = queue_commitments(states);
    auto health = candidate->health();
    auto& object = objects.at(id);
    if (object.health.maximum != health.maximum || object.health.current != health.current) {
        view->apply_server_health(candidate->owner().entity, health);
        object.health = health;
        ++revision;
    }
    abilities.at(id) = std::move(candidate);
    ability_actions = std::move(queue);
    ability_commitments = std::move(commitments);
}

auto WorldSession::Impl::release_ability_control(ConnectionHandle peer) -> void {
    std::erase_if(owner_messages, [&](const auto& message) { return message.source == peer; });
    if (role != SessionRole::Server)
        return;
    std::map<std::uint64_t, std::unique_ptr<AbilityState>> prepared;
    std::vector<AbilityActionUpdate> updates;
    for (const auto& [id, input] : motor_inputs)
        if (input.owner == peer && abilities.contains(id)) {
            auto state = std::make_unique<AbilityState>(*abilities.at(id));
            auto cleanup = state->disconnect();
            updates.insert(updates.end(), cleanup.begin(), cleanup.end());
            prepared.emplace(id, std::move(state));
        }
    auto queue = queue_actions(updates);
    auto commitments = queue_commitments(prepared);
    bool changed = false;
    // Releasing action tags can suppress/remove modifiers and clamp Health.
    // Publish those resource changes with the prepared disconnect cleanup.
    for (const auto& [id, state] : prepared) {
        auto health = state->health();
        auto& object = objects.at(id);
        if (object.health.maximum != health.maximum || object.health.current != health.current) {
            view->apply_server_health(state->owner().entity, health);
            object.health = health;
            changed = true;
        }
    }
    for (auto& [id, state] : prepared)
        abilities.at(id) = std::move(state);
    ability_actions = std::move(queue);
    ability_commitments = std::move(commitments);
    ability_notices.erase(peer);
    interaction_notices.erase(peer);
    if (changed)
        ++revision;
    for (auto& [id, input] : ability_inputs)
        std::erase_if(input, [&](const auto& record) { return record.second.peer == peer; });
}

AbilityOwnerHandle WorldSession::configure_abilities(std::uint64_t id, std::vector<AttributeDefinition> schema, AttributeId health,
                                                     AttributeId maximum) {
    auto& s = *impl_;
    s.authority();
    require(s.objects.contains(id) && !s.abilities.contains(id) && s.abilities.size() < session_character_capacity,
            "Ability owner missing/already configured/capacity");
    AbilityOwnerHandle handle{s.view->find(s.objects.at(id).id), s.hello.session_epoch, id};
    auto state = std::make_unique<AbilityState>(handle, s.ability_tick, std::move(schema), health, maximum);
    auto initial = state->health();
    require(initial.maximum == s.objects.at(id).health.maximum && initial.current == s.objects.at(id).health.current,
            "Ability Health must match existing authoritative object");
    s.abilities.emplace(id, std::move(state));
    return handle;
}

void WorldSession::equip_combat_kit(AbilityOwnerHandle owner, std::shared_ptr<const CombatKitDefinition> kit, const InputProfile& input,
                                    std::span<const std::shared_ptr<const AbilityDefinition>> catalogue) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(owner));
    for (const auto& ability : catalogue)
        if (ability)
            for (const auto& hit : ability->melee)
                require(bool(s.melee_query) && s.damage_evaluators.contains(hit.evaluator),
                        "Missing authoritative query binding/game damage evaluator");
    auto updates = state->equip(std::move(kit), input, catalogue);
    s.commit_ability(owner.network, std::move(state), updates);
}

void WorldSession::configure_ability_loadouts(AbilityOwnerHandle owner, std::span<const std::optional<AbilityLoadout>> loadouts,
                                              unsigned active) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(owner));
    for (const auto& loadout : loadouts)
        if (loadout)
            for (const auto& ability : loadout->abilities)
                if (ability)
                    for (const auto& hit : ability->melee)
                        require(bool(s.melee_query) && s.damage_evaluators.contains(hit.evaluator),
                                "Missing authoritative query binding/game damage evaluator");
    auto updates = state->configure_loadouts(loadouts, active);
    s.commit_ability(owner.network, std::move(state), updates);
}

AbilityFailure WorldSession::can_activate(const AbilityRequest& request) const {
    return impl_->ability(request.owner).can_activate(request);
}

AbilityReceipt WorldSession::request_ability(const AbilityRequest& request) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(request.owner));
    auto outcome = state->request(request);
    s.commit_ability(request.owner.network, std::move(state), outcome.second);
    return outcome.first;
}

void WorldSession::refuse_next_ability(std::uint64_t network, AbilityFailure reason) {
    auto& s = *impl_;
    s.authority();
    require(reason != AbilityFailure::None && s.abilities.contains(network),
            "A refusal is injected for a known ability owner, with a reason");
    s.injected_refusals[network] = reason;
}

AbilityFailure WorldSession::cancel_ability(AbilityActivationHandle handle) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(handle.owner));
    auto outcome = state->cancel(handle, AbilityActionReason::Cancelled);
    s.commit_ability(handle.owner.network, std::move(state), outcome.second);
    return outcome.first;
}

void WorldSession::advance_abilities(std::uint64_t tick, unsigned rate) {
    auto& s = *impl_;
    s.authority();
    require(s.ability_tick != std::numeric_limits<std::uint64_t>::max() && tick == s.ability_tick + 1 && rate <= 4 * action_tick_units,
            "Ability simulation must advance one fixed step");
    std::map<std::uint64_t, std::unique_ptr<AbilityState>> prepared;
    std::vector<AbilityActionUpdate> updates;
    auto pending = s.ability_inputs;
    auto notices = s.ability_notices;
    auto effects = s.effect_outcomes;
    // Expiry and periodic effects across all owners precede every activation.
    for (const auto& [id, old] : s.abilities) {
        auto state = std::make_unique<AbilityState>(*old);
        state->begin_tick(tick);
        auto results = state->advance_effects([&](auto evaluator) { return s.effect_evaluators.at(evaluator); }, updates);
        for (auto& result : results) {
            require(effects.size() < 128, "Effect execution queue full; drain before advance");
            effects.push_back({state->owner(), std::move(result)});
        }
        prepared.emplace(id, std::move(state));
    }
    for (auto& [id, state] : prepared) {
        auto input = pending.find(id);
        if (input != pending.end())
            for (auto request = input->second.begin(); request != input->second.end();) {
                auto& queued = request->second;
                if (queued.intent.tick > tick) {
                    ++request;
                    continue;
                }
                if (!s.peers.contains(queued.peer) || !s.motor_inputs.contains(id) || s.motor_inputs.at(id).owner != queued.peer) {
                    request = input->second.erase(request);
                    continue;
                }
                std::optional<AbilityFailure> rejection;
                if (!s.motor_states.contains(id) || queued.intent.avatar_epoch != s.motor_states.at(id).epoch)
                    rejection = AbilityFailure::AvatarMismatch;
                else if (queued.expired_bootstrap || queued.intent.tick < tick)
                    rejection = AbilityFailure::InputExpired;
                else if (queued.intent.edge == InputEdge::Pressed && !queued.intent.cancelled)
                    if (const auto injected = s.injected_refusals.find(id); injected != s.injected_refusals.end()) {
                        rejection = injected->second;
                        s.injected_refusals.erase(injected);
                    }
                auto outcome = state->request_wire(queued.intent, rejection);
                require(outcome.first.failure != AbilityFailure::OperationConflict, "Changed wire operation payload; resynchronize");
                updates.insert(updates.end(), outcome.second.begin(), outcome.second.end());
                auto receipt = outcome.first;
                if (!receipt.operation) {
                    receipt.operation = queued.intent.operation;
                    receipt.tick = tick;
                }
                const bool terminal = receipt.failure != AbilityFailure::HistoryFull;
                if (terminal || !queued.resync_notified) {
                    auto& output = notices[queued.peer];
                    require(output.size() < 128, "Ability terminal receipt queue full; drain/resynchronize");
                    output.push_back({id, receipt, terminal});
                }
                if (terminal)
                    request = input->second.erase(request);
                else {
                    queued.resync_notified = true;
                    ++request;
                }
            }
        auto batch = state->finish_tick(rate);
        for (auto& update : batch)
            if (update.phase != ActionPhase::Active || !update.batch.events.empty() || !update.batch.traversed.empty())
                updates.push_back(std::move(update));
    }
    auto queue = s.queue_actions(updates);
    auto commitments = s.queue_commitments(prepared);
    bool changed = false;
    for (const auto& [id, state] : prepared) {
        auto health = state->health();
        auto& object = s.objects.at(id);
        if (object.health.maximum != health.maximum || object.health.current != health.current) {
            s.view->apply_server_health(state->owner().entity, health);
            object.health = health;
            changed = true;
        }
    }
    s.abilities = std::move(prepared);
    s.ability_inputs = std::move(pending);
    s.ability_notices = std::move(notices);
    s.ability_actions = std::move(queue);
    s.ability_commitments = std::move(commitments);
    s.effect_outcomes = std::move(effects);
    s.ability_tick = tick;
    if (changed)
        ++s.revision;
}

AbilityOwnerSnapshot WorldSession::ability_snapshot(AbilityOwnerHandle owner) const {
    return impl_->ability(owner).snapshot();
}

std::vector<AbilityCommitUpdate> WorldSession::drain_ability_commitments() {
    auto& s = *impl_;
    s.authority();
    auto result = std::move(s.ability_commitments);
    s.ability_commitments.clear();
    return result;
}

std::vector<AbilityActionUpdate> WorldSession::drain_ability_actions() {
    auto& s = *impl_;
    s.authority();
    auto result = std::move(s.ability_actions);
    s.ability_actions.clear();
    return result;
}

void WorldSession::retire_ability_operations(AbilityOwnerHandle owner, std::uint64_t through) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(owner));
    state->retire(through);
    s.commit_ability(owner.network, std::move(state), {});
}

void WorldSession::configure_ability_tags(AbilityOwnerHandle owner, std::shared_ptr<const TagDictionary> dictionary) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(owner));
    state->configure_tags(std::move(dictionary));
    s.commit_ability(owner.network, std::move(state), {});
}

void WorldSession::register_effect_evaluator(std::uint32_t id, EffectEvaluator evaluator) {
    auto& s = *impl_;
    s.authority();
    require(id && bool(evaluator) && s.effect_evaluators.size() < 16 && !s.effect_evaluators.contains(id),
            "Native effect evaluator registration");
    s.effect_evaluators.emplace(id, std::move(evaluator));
}

EffectHandle WorldSession::apply_effect(AbilityOwnerHandle target, AbilityOwnerHandle source, const EffectDefinition& definition,
                                        double power, std::uint64_t activation) {
    auto& s = *impl_;
    s.authority();
    const auto& origin = s.ability(source);
    require(!activation || activation < origin.snapshot().next_activation, "Invalid effect source activation credit");
    require(!definition.evaluator || s.effect_evaluators.contains(definition.evaluator), "Missing native effect evaluator");
    auto state = std::make_unique<AbilityState>(s.ability(target));
    std::vector<AbilityActionUpdate> updates;
    auto result = state->apply_effect(definition, {s.hello.session_epoch, source.network, activation, power, origin.attribute_values()},
                                      definition.evaluator ? s.effect_evaluators.at(definition.evaluator) : EffectEvaluator{}, updates);
    // Reserve the worst simultaneous periodic batch before publishing an effect.
    // The fixed tick has one atomic outcome queue, so later draining cannot split it.
    std::size_t periodic{};
    for (const auto& [id, current] : s.abilities) {
        const auto& actor = id == target.network ? *state : *current;
        for (const auto& active : actor.effects().snapshot())
            periodic += active.next_period != 0;
    }
    require(periodic <= 128, "Global periodic effect reservation exhausted; remove an effect before applying another");
    auto outcomes = s.effect_outcomes;
    for (auto& execution : result.second) {
        require(outcomes.size() < 128, "Effect outcome queue full; drain before application");
        outcomes.push_back({target, std::move(execution)});
    }
    s.commit_ability(target.network, std::move(state), updates);
    s.effect_outcomes = std::move(outcomes);
    return result.first;
}

void WorldSession::remove_effect(AbilityOwnerHandle target, EffectHandle handle) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(target));
    state->remove_effect(handle);
    auto cleanup = state->damage(0);
    s.commit_ability(target.network, std::move(state), cleanup);
}

void WorldSession::cleanse_effects(AbilityOwnerHandle target, const TagRequirement& tags) {
    auto& s = *impl_;
    s.authority();
    auto state = std::make_unique<AbilityState>(s.ability(target));
    state->cleanse_effects(tags);
    auto cleanup = state->damage(0);
    s.commit_ability(target.network, std::move(state), cleanup);
}

void WorldSession::damage(AbilityOwnerHandle target, double amount) {
    auto& s = *impl_;
    s.authority();
    require(std::isfinite(amount) && amount >= 0, "Damage amount");
    auto state = std::make_unique<AbilityState>(s.ability(target));
    auto cleanup = state->damage(std::min(amount, state->health().current));
    s.commit_ability(target.network, std::move(state), cleanup);
}

void WorldSession::heal(AbilityOwnerHandle target, double amount) {
    auto& s = *impl_;
    s.authority();
    require(std::isfinite(amount) && amount >= 0, "Heal amount");
    auto state = std::make_unique<AbilityState>(s.ability(target));
    state->heal(amount);
    s.commit_ability(target.network, std::move(state), {});
}

std::vector<EffectSnapshot> WorldSession::ability_effects(AbilityOwnerHandle owner) const {
    auto result = impl_->ability(owner).effects().snapshot();
    for (auto& state : result)
        state.handle.owner = owner;
    return result;
}

std::vector<TagId> WorldSession::ability_tags(AbilityOwnerHandle owner, AttributeVisibility audience) const {
    return impl_->ability(owner).effects().tags().values(audience);
}

std::vector<EffectOutcome> WorldSession::drain_effect_outcomes() {
    auto& s = *impl_;
    s.authority();
    auto results = std::move(s.effect_outcomes);
    s.effect_outcomes.clear();
    return results;
}

void WorldSession::bind_melee_query(std::shared_ptr<const MeleeQuery> query) {
    auto& s = *impl_;
    s.authority();
    require(bool(query) && !s.melee_query, "Melee query is a single server-owned collision binding");
    s.melee_query = std::move(query);
}

void WorldSession::register_damage_evaluator(std::uint32_t id, DamageEvaluator evaluator) {
    require(bool(evaluator), "Missing damage evaluator");
    register_combat_evaluator(
        id, [evaluator = std::move(evaluator)](const DamageContext& context) { return CombatEvaluation{evaluator(context), {}}; });
}

void WorldSession::register_combat_evaluator(std::uint32_t id, CombatEvaluator evaluator) {
    auto& s = *impl_;
    s.authority();
    require(id && bool(evaluator) && s.damage_evaluators.size() < 16 && !s.damage_evaluators.contains(id), "Combat evaluator registration");
    s.damage_evaluators.emplace(id, std::move(evaluator));
}

void WorldSession::assign_team(std::uint64_t id, std::uint32_t team) {
    auto& s = *impl_;
    s.authority();
    require(s.objects.contains(id) && s.teams.size() <= session_character_capacity, "Invalid team assignment");
    if (team)
        s.teams[id] = team;
    else
        s.teams.erase(id);
}

std::vector<DamageResult> WorldSession::resolve_ability_hits(std::uint64_t tick) {
    auto& s = *impl_;
    s.authority();
    require(tick && tick == s.ability_tick && bool(s.melee_query), "Ability hit resolution fixed tick/query binding");
    if (s.resolved_hit_tick == tick)
        return {};
    const auto& query = *s.melee_query;
    std::map<std::uint64_t, std::unique_ptr<AbilityState>> prepared;
    for (const auto& [id, state] : s.abilities)
        prepared.emplace(id, std::make_unique<AbilityState>(*state));
    std::vector<AbilityActionUpdate> updates;
    std::vector<DamageResult> results;
    std::size_t work{};
    auto effects = s.effect_outcomes;
    // One hit of one source on one candidate: team and liveness checks, the game evaluator, damage and on-hit effects.
    // Melee targets and projectile impacts both land through here.
    auto flying = s.projectiles;
    auto strike = [&](std::uint64_t id, AbilityState& source, const AbilityState::PendingHit& hit, const MeleeTarget& target) {
        if (target.network == id || !prepared.contains(target.network) || !s.motor_states.contains(target.network))
            return;
        // No friendly fire: a hit never lands on an actor of the attacker's own team.
        if (auto mine = s.teams.find(id), theirs = s.teams.find(target.network);
            mine != s.teams.end() && theirs != s.teams.end() && mine->second == theirs->second)
            return;
        auto& victim = *prepared.at(target.network);
        auto& motor = s.motor_states.at(target.network);
        if (motor.tick != tick || motor.epoch != target.epoch || !query.matches(target.network, motor) || victim.health().current <= 0)
            return;
        if (!source.remember_hit(hit, target.network, target.epoch))
            return;
        auto source_attributes = source.attribute_values(), target_attributes = victim.attribute_values();
        DamageContext context{hit.handle,
                              victim.owner(),
                              tick,
                              hit.interval.block,
                              hit.interval.loop,
                              hit.profile.damage_type,
                              hit.profile.power,
                              source_attributes,
                              target_attributes,
                              &source.effects().tags(),
                              &victim.effects().tags(),
                              hit.definition->id};
        auto evaluation = s.damage_evaluators.at(hit.profile.evaluator)(context);
        require(std::isfinite(evaluation.damage) && evaluation.damage >= 0 && evaluation.damage <= 1e9 && evaluation.effects.size() <= 8,
                "Game combat evaluator output bounds");
        for (auto& request : evaluation.effects) {
            require(request.definition && std::isfinite(request.power) && request.power >= 0 && request.power <= 1e9 &&
                        (!request.definition->evaluator || s.effect_evaluators.contains(request.definition->evaluator)),
                    "Game combat effect request bounds/evaluator");
            request.definition = victim.prepare_effect(*request.definition);
        }
        auto before = victim.health().current;
        auto death = victim.damage(evaluation.damage);
        updates.insert(updates.end(), death.begin(), death.end());
        auto direct_after = victim.health().current;
        // On-hit statuses target surviving actors only. Every request is
        // prepared in this same batch; no damage or ledger publishes early.
        for (const auto& request : evaluation.effects) {
            if (victim.health().current <= 0)
                break;
            std::vector<AbilityActionUpdate> cleanup;
            auto applied = victim.apply_effect(
                *request.definition, {s.hello.session_epoch, id, hit.handle.activation, request.power, source_attributes},
                request.definition->evaluator ? s.effect_evaluators.at(request.definition->evaluator) : EffectEvaluator{}, cleanup);
            updates.insert(updates.end(), cleanup.begin(), cleanup.end());
            for (auto& outcome : applied.second) {
                require(effects.size() < 128, "Combat effect execution queue full; drain before resolution");
                effects.push_back({victim.owner(), std::move(outcome)});
            }
        }
        auto after = victim.health().current;
        require(results.size() < 128, "Damage result batch bound");
        results.push_back({hit.handle, victim.owner(), tick, hit.interval.block, hit.interval.loop, hit.profile.damage_type, before, after,
                           before - direct_after, after <= 0});
    };
    // Stable owner/activation/block/loop/target order, independent of query insertion.
    for (auto& [id, source] : prepared) {
        auto pending = source->pending_hits;
        std::sort(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
            return std::tie(a.handle.activation, a.interval.block, a.interval.loop, a.interval.from) <
                   std::tie(b.handle.activation, b.interval.block, b.interval.loop, b.interval.from);
        });
        for (const auto& hit : pending) {
            if (source->health().current <= 0)
                break;
            require(hit.tick == tick && s.motor_states.contains(id) && s.motor_states.at(id).tick == tick &&
                        query.matches(id, s.motor_states.at(id)),
                    "Melee requires current achieved authoritative motor pose");
            // A projectile ability launches once per hit block and loop; the projectile resolves its own hit on a later tick.
            if (hit.definition->aim && hit.definition->aim->projectile) {
                if (source->remember_hit(hit, 0, 0) && flying.size() < projectile_capacity) {
                    const auto& shot = *hit.definition->aim->projectile;
                    const auto& motor = s.motor_states.at(id);
                    const double level = std::cos(motor.pitch) * shot.speed / 60;
                    flying.push_back({s.next_projectile++,
                                      hit,
                                      id,
                                      query.origin(id,motor,hit.profile,hit.clip,hit.action_clock),
                                      {std::sin(motor.yaw) * level, std::sin(motor.pitch) * shot.speed / 60, std::cos(motor.yaw) * level},
                                      shot.gravity / 3600,
                                      hit.profile.radius,
                                      hit.definition->aim->range,
                                      tick + shot.lifetime});
                }
                continue;
            }
            auto candidates = query.query(id, s.motor_states.at(id), hit.profile,hit.clip,hit.action_clock);
            require(!candidates.overflow && candidates.targets.size() <= 16, "Melee query overflow");
            std::sort(candidates.targets.begin(), candidates.targets.end());
            candidates.targets.erase(std::unique(candidates.targets.begin(), candidates.targets.end()), candidates.targets.end());
            for (const auto& target : candidates.targets) {
                require(++work <= 256, "Melee batch work bound");
                strike(id, *source, hit, target);
            }
        }
        source->pending_hits.clear();
    }
    // Projectiles in flight take one swept step each, in launch order. The first character in the path takes the hit through
    // the same evaluation as a melee hit; static collision, the aim range or the lifetime ends the flight.
    for (auto shot = flying.begin(); shot != flying.end();) {
        bool spent = !prepared.contains(shot->source);
        if (!spent) {
            shot->velocity.y -= shot->gravity;
            MotorVec delta = shot->velocity;
            double length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
            if (length > shot->remaining && length > 1e-9) {
                const double scale = shot->remaining / length;
                delta = {delta.x * scale, delta.y * scale, delta.z * scale};
                length = shot->remaining;
            }
            const auto traced = query.trace(shot->position, delta, shot->radius, shot->source);
            if (traced.target) {
                require(++work <= 256, "Melee batch work bound");
                strike(shot->source, *prepared.at(shot->source), shot->hit, *traced.target);
                spent = true;
            } else if (traced.blocked)
                spent = true;
            else {
                shot->position = {shot->position.x + delta.x, shot->position.y + delta.y, shot->position.z + delta.z};
                shot->remaining -= length;
                spent = shot->remaining <= 1e-6 || tick >= shot->expires;
            }
        }
        shot = spent ? flying.erase(shot) : shot + 1;
    }
    std::size_t periodic{};
    for (const auto& [id, state] : prepared)
        for (const auto& effect : state->effects().snapshot())
            periodic += effect.next_period != 0;
    require(periodic <= 128, "Combat global periodic effect reservation exhausted");
    auto queue = s.queue_actions(updates);
    auto commitments = s.queue_commitments(prepared);
    bool changed = false;
    // Preparation, evaluator calls and all bounded queue checks completed first.
    for (const auto& [id, state] : prepared) {
        auto health = state->health();
        auto& object = s.objects.at(id);
        if (object.health.current != health.current || object.health.maximum != health.maximum) {
            s.view->apply_server_health(state->owner().entity, health);
            object.health = health;
            changed = true;
        }
    }
    s.abilities = std::move(prepared);
    s.ability_actions = std::move(queue);
    s.ability_commitments = std::move(commitments);
    s.effect_outcomes = std::move(effects);
    s.resolved_hit_tick = tick;
    if (changed)
        ++s.revision;
    if (!flying.empty() || !s.projectiles.empty()) {
        s.projectiles = std::move(flying);
        s.projectile_views.clear();
        for (const auto& shot : s.projectiles)
            s.projectile_views.push_back({shot.id, shot.position, {shot.velocity.x * 60, shot.velocity.y * 60, shot.velocity.z * 60},
                                          shot.hit.definition->aim&&shot.hit.definition->aim->projectile?shot.hit.definition->aim->projectile->visual_model:AssetId{}});
        ++s.projectile_revision;
        s.projectile_tick = tick;
    }
    return results;
}
} // namespace darkangel
