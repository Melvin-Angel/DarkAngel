#include "scene_authority_impl.hpp"

namespace darkangel {
auto SceneAuthority::Impl::persistent(StableId object) const -> PersistentId {
    return {combat->scene, object};
}

auto SceneAuthority::Impl::record_interactable(std::size_t index) -> void {
    if (ledger)
        ledger->capture(persistent(combat->interactables[index].id), InteractableState(interactables[index]));
}

auto SceneAuthority::Impl::record_trigger(std::size_t index) -> void {
    if (ledger)
        ledger->capture(persistent(combat->triggers[index].id), TriggerState(triggers[index]));
}

auto SceneAuthority::Impl::record_encounter(std::size_t index) -> void {
    if (ledger)
        ledger->capture(persistent(encounter_identities[index]), EncounterState(directors[index]));
}

auto SceneAuthority::Impl::capture() -> void {
    if (!ledger)
        return;
    for (std::size_t index = 0; index < interactables.size(); ++index)
        record_interactable(index);
    for (std::size_t index = 0; index < triggers.size(); ++index)
        record_trigger(index);
    for (std::size_t index = 0; index < directors.size(); ++index)
        record_encounter(index);
    for (auto& actor : actors)
        if (!actor.encounter && actor.recorded) {
            ActorState state;
            state.defeated = true;
            ledger->capture(persistent(actor.identity), state);
        }
    if (world_objects)
        world_objects->capture();
}

auto SceneAuthority::Impl::who(const Member& m) const -> scene_detail::SceneWorldObjects::Who {
    return {m.id, m.profile, m.motor->state().position, living(m.id) && (!lifecycle || lifecycle->state(m.id) == PartyMemberState::Alive),
            &m == &members.front()};
}

auto SceneAuthority::Impl::context() const -> scene_detail::SceneWorldObjects::Context {
    return {world, [this](StableId id) {
                for (std::size_t index = 0; index < directors.size(); ++index)
                    if (encounter_identities[index] == id)
                        return directors[index].status().phase == EncounterPhase::Complete;
                return false;
            }};
}

auto SceneAuthority::Impl::publish_view() -> void {
    if (!world_objects)
        return;
    WorldStateView view;
    world_objects->view(view);
    view.members = lifecycle->view();
    for (auto& m : view.members)
        m.revive = std::floor(m.revive * 10) / 10;
    view.wiped = lifecycle->wiped();
    view.revision = view_now.revision;
    if (view != view_now) {
        view.revision = view_now.revision + 1;
        view_now = std::move(view);
    }
    const auto bytes = world_bytes(encode_world_state(view_now));
    for (const auto& m : members) {
        auto& sent = view_sent[m.id];
        if (sent != view_now.revision && server.send_owner_message(m.id, world_state_channel, bytes))
            sent = view_now.revision;
    }
}

auto SceneAuthority::Impl::spawn(std::size_t index, const EncounterSpawn& request) -> bool {
    const auto& encounter = combat->encounters[index];
    require(actors.size() < actor_limit(), "An encounter spawned more actors than one session holds");
    auto made = encounter.npcs.at(request.npc);
    made.id = encounter_actor(encounter.id, request.index);
    made.controller = encounter.controller ? encounter.controller(request.npc) : nullptr;
    spawned.push_back(std::move(made));
    const auto& config = spawned.back();
    ObjectData object;
    object.id = config.id;
    object.transform.x = request.position.x;
    object.transform.y = request.position.y;
    object.transform.z = request.position.z;
    object.transform.yaw = request.yaw;
    for (const auto& attribute : config.attributes) {
        if (attribute.id == combat->health)
            object.health.current = attribute.base;
        if (attribute.id == combat->maximum_health)
            object.health.maximum = attribute.base;
    }
    Actor actor;
    actor.identity = config.id;
    actor.id = server.create(object);
    actor.config = &config;
    actor.foot = request.position;
    actor.yaw = request.yaw;
    actor.encounter = index;
    require(actors.empty() || actors.back().id < actor.id, "A spawned actor's network identity is out of order");
    actor.faction = factions.emplace(config.faction, std::uint32_t(factions.size() + 1)).first->second;
    server.assign_team(actor.id, actor.faction);
    try {
        arm(actor);
    } catch (const std::exception&) {
        actor.motor.reset();
        server.destroy(actor.id);
        spawned.pop_back();
        return false;
    }
    actors.push_back(std::move(actor));
    actor_identities.push_back(config.id);
    actor_commands.resize(actors.size());
    actor_motions.resize(actors.size());
    actor_before.resize(actors.size());
    actor_requested.resize(actors.size());
    return true;
}

auto SceneAuthority::Impl::witness(std::span<const EncounterVolume> volumes) const -> std::pair<MotorVec, bool> {
    for (const auto& known : members)
        if (living(known.id) && encounter_contains(volumes, known.motor->state().position))
            return {known.motor->state().position, true};
    return {members.front().motor->state().position, living(members.front().id)};
}

auto SceneAuthority::Impl::run_world(std::uint64_t tick) -> void {
    world.begin_tick(tick);
    // Interaction requests, each resolved for the member that sent it from authoritative state. Every request is answered.
    for (const auto& request : server.drain_interactions()) {
        auto result = InteractionResult::Unknown;
        if (const auto* sender = member(request.network)) {
            bool handled = false;
            // Holding Interact on a downed teammate revives it; any other world object answers for itself.
            if (const auto* target = member(request.object); target && lifecycle) {
                handled = true;
                result = lifecycle->state(sender->id) == PartyMemberState::Alive && living(sender->id) &&
                                 lifecycle->state(target->id) == PartyMemberState::Downed
                             ? InteractionResult::Accepted
                             : InteractionResult::Unavailable;
                if (result == InteractionResult::Accepted)
                    lifecycle->hold(sender->id, target->id);
            } else if (world_objects)
                if (auto answered = world_objects->interact(who(*sender), request.object, context(), travel)) {
                    handled = true;
                    result = *answered;
                }
            // A single-use interactable that was just spent is recorded, and the other members' clients are told.
            if (!handled)
                for (std::size_t index = 0; index < interactables.size(); ++index)
                    if (combat->interactables[index].id == request.object) {
                        result = interactables[index].use(world, sender->motor->state().position, living(sender->id));
                        if (result == InteractionResult::Accepted && interactables[index].definition().once) {
                            record_interactable(index);
                            for (const auto& other : members)
                                if (other.id != sender->id)
                                    server.answer_interaction(other.id, request.object, std::uint8_t(InteractionResult::UsedUp));
                        }
                        break;
                    }
            // A scene transition used on purpose is the same request; what it raises is travel for the whole party.
            if (!handled)
                for (std::size_t index = 0; index < portals.size(); ++index) {
                    const auto& item = combat->transitions[index];
                    if (item.id != request.object || item.definition.activation != TransitionActivation::Interact)
                        continue;
                    const auto& at = sender->motor->state().position;
                    const double dx = at.x - item.center.x, dy = at.y - item.center.y, dz = at.z - item.center.z;
                    result = !living(sender->id)                                                             ? InteractionResult::Dead
                             : dx * dx + dy * dy + dz * dz > item.definition.radius * item.definition.radius ? InteractionResult::OutOfReach
                             : travel ? InteractionResult::Unavailable
                                      : InteractionResult::Accepted;
                    if (result == InteractionResult::Accepted)
                        travel = SceneTransitionRequest{item.id, item.definition.scene, item.definition.entry};
                    break;
                }
        }
        if(result==InteractionResult::Accepted&&world_objects)world_objects->accepted_puzzle(request.network,request.object);
        server.answer_interaction(request.network, request.object, std::uint8_t(result));
    }
    // Altar menus: rest and fast travel, decided here. A malformed request closes its sender.
    if (world_objects)
        for (const auto& message : server.drain_owner_messages(world_state_channel)) {
            if (!server.attached(message.source))
                continue;
            const auto* sender = member(message.network);
            try {
                require(sender != nullptr, "World request from a non-member");
                world_objects->request(who(*sender), decode_world_request(world_text(message.bytes)), travel);
            } catch (const std::exception&) {
                server.disconnect(message.source);
            }
        }
    for (auto& actor : actors)
        if (actor.encounter && !actor.died && !living(actor.id))
            actor.died = tick;
    for (auto& actor : actors)
        if (!actor.encounter && !actor.recorded && !living(actor.id)) {
            actor.recorded = true;
            if (ledger) {
                ActorState state;
                state.defeated = true;
                ledger->capture(persistent(actor.identity), state);
            }
        }
    for (std::size_t position = actors.size(); position-- > 0;) {
        const auto& actor = actors[position];
        if (!actor.encounter || !actor.died)
            continue;
        const auto keep = std::uint64_t(std::llround(combat->encounters[*actor.encounter].definition.corpse_seconds * 60));
        if (keep && tick - actor.died >= keep)
            retire(position);
    }
    for (std::size_t index = 0; index < triggers.size(); ++index) {
        auto& trigger = triggers[index];
        const auto seen_by = witness({&trigger.volume(), 1});
        if (trigger.step(world, seen_by.first, seen_by.second) && trigger.definition().once)
            record_trigger(index);
    }
    const auto ground = [this](double x, double from, double z) -> std::optional<double> {
        const auto found = authoritative.query_ray({x, from, z}, {0, -(std::abs(from) + 500), 0}, {StaticCollision, 0});
        const CollisionHit* nearest = nullptr;
        for (const auto& hit : found.hits)
            if (!nearest || hit.fraction < nearest->fraction)
                nearest = &hit;
        if (!nearest)
            return std::nullopt;
        return nearest->point.y;
    };
    const auto occupied = [this](MotorVec point) {
        auto near = [&](const MotorVec& at) { return std::hypot(at.x - point.x, at.z - point.z) < 1. && std::abs(at.y - point.y) < 2.5; };
        for (const auto& known : members)
            if (living(known.id) && near(known.motor->state().position))
                return true;
        for (const auto& actor : actors)
            if (near(actor.motor->state().position))
                return true;
        for (const auto& waiting : spawn_queue)
            if (near(waiting.second.position))
                return true;
        return false;
    };
    for (std::size_t index = 0; index < directors.size(); ++index) {
        unsigned alive = 0;
        for (const auto& actor : actors)
            if (actor.encounter == index && living(actor.id))
                ++alive;
        for (const auto& waiting : spawn_queue)
            if (waiting.first == index)
                ++alive;
        auto room = [&] { return unsigned(actor_limit() - std::min(actor_limit(), actors.size() + spawn_queue.size())); };
        // A spawn that needs room takes it from the dead, oldest first, before their time is up.
        for (const auto need = directors[index].need(); room() < need;) {
            std::size_t oldest = actors.size();
            for (std::size_t position = 0; position < actors.size(); ++position)
                if (actors[position].encounter && actors[position].died &&
                    (oldest == actors.size() || actors[position].died < actors[oldest].died))
                    oldest = position;
            if (oldest == actors.size())
                break;
            retire(oldest);
        }
        for (const auto& request :
             directors[index].step(tick, world, witness(directors[index].volumes()).first, alive, ground, occupied, room()))
            spawn_queue.push_back({index, request, 0});
        if (const auto phase = directors[index].status().phase; phase != director_phases[index]) {
            director_phases[index] = phase;
            record_encounter(index);
        }
    }
    // A transition entered by walking in fires for any living member, once it has seen its box empty: a party that
    // arrives standing inside one is not sent straight back.
    for (std::size_t index = 0; index < portals.size(); ++index) {
        const auto& item = combat->transitions[index];
        if (item.definition.activation != TransitionActivation::Enter)
            continue;
        bool inside = false;
        for (const auto& known : members)
            inside |= living(known.id) && encounter_contains({&portals[index].volume, 1}, known.motor->state().position);
        if (!inside)
            portals[index].armed = true;
        else if (portals[index].armed && !travel) {
            travel = SceneTransitionRequest{item.id, item.definition.scene, item.definition.entry};
            portals[index].armed = false;
        }
    }
    for (unsigned entered = 0; entered < combat->spawn_budget && !spawn_queue.empty(); ++entered) {
        auto next = spawn_queue.front();
        spawn_queue.pop_front();
        auto request = next.second;
        // A blocked place is tried again on later ticks, a little further out each time.
        if (next.attempts) {
            const double angle = 2.399963229728653 * next.attempts, reach = .45 * next.attempts;
            request.position.x += std::sin(angle) * reach;
            request.position.z += std::cos(angle) * reach;
        }
        if (!spawn(next.first, request)) {
            require(next.attempts < 12, "An encounter found no free place to bring an enemy in");
            ++next.attempts;
            spawn_queue.push_back(next);
            break;
        }
    }
    if (world_objects)
        world_objects->step(1. / 60, world);
    if (lifecycle) {
        std::vector<PartyLifecycleCoordinator::Member> party;
        for (const auto& m : members)
            party.push_back({m.id, m.motor->state().position, server.objects().at(m.id).health.current, server.attached(m.peer)});
        const auto outcome = lifecycle->step(party, 1. / 60);
        for (const auto id : outcome.revived)
            for (const auto& m : members)
                if (m.id == id) {
                    const auto& health = server.objects().at(id).health;
                    server.heal(m.owner, std::max(1., health.maximum * combat->party.revive_health));
                }
        if (outcome.wipe)
            world.emit("party.wiped");
        if (lifecycle->respawn_due() && !respawn_raised) {
            respawn_raised = true;
            auto respawn = world_objects->respawn();
            respawn.source = members.front().identity;
            travel = respawn;
        }
    }
}
} // namespace darkangel
