#include "scene_authority_impl.hpp"

namespace darkangel {
auto SceneAuthority::Impl::actor_limit() const -> std::size_t {
    return session_character_capacity - members.size();
}

auto SceneAuthority::Impl::living(std::uint64_t id) const -> bool {
    return server.objects().at(id).health.current > 0;
}

auto SceneAuthority::Impl::definition(const AbilityOwnerSnapshot& snapshot) const -> const AbilityDefinition& {
    return scene_detail::definition(combat.get(), snapshot);
}

auto SceneAuthority::Impl::motion(const AbilityOwnerSnapshot& snapshot, std::uint64_t from, std::uint64_t to, MotorInput& command) const
    -> MotionRequest {
    auto local = ability_motion_between(definition(snapshot), from, to);
    const auto& v = local.root.translation;
    auto yaw = command.yaw;
    command.yaw = std::remainder(command.yaw + local.root.yaw, 6.283185307179586);
    MotionRequest request;
    request.root = {std::cos(yaw) * v[0] + std::sin(yaw) * v[2], v[1], -std::sin(yaw) * v[0] + std::cos(yaw) * v[2]};
    request.action = snapshot.active->activation;
    request.lock = local.movement_lock;
    request.warp = ability_warp_between(definition(snapshot), from, to);
    return request;
}

auto SceneAuthority::Impl::action_motion(const AbilityOwnerSnapshot& before, const AbilityOwnerSnapshot& after,
                                         std::span<const AbilityActionUpdate> updates, MotorInput& command, double motor_yaw,
                                         bool requested) const -> MotionRequest {
    if (after.active) {
        const bool continuing = before.active && before.active == after.active;
        if (definition(after).face_movement)
            scene_detail::face(command, motor_yaw, !continuing || requested);
        auto from = continuing ? before.action->clock : 0;
        return motion(after, from, after.action->clock, command);
    }
    if (before.active)
        for (const auto& update : updates)
            if (update.handle == *before.active && update.phase == ActionPhase::Completed) {
                if (definition(before).face_movement)
                    scene_detail::face(command, motor_yaw, false);
                auto total = std::uint64_t(definition(before).action->duration) * definition(before).action->loops;
                return motion(before, before.action->clock, total, command);
            }
    return {};
}

auto SceneAuthority::Impl::restricted(AbilityOwnerHandle owner, std::uint64_t id) const -> bool {
    if (!living(id))
        return true;
    for (const auto& active : server.ability_effects(owner))
        if (!active.suppressed)
            for (const auto& effect : combat->effects)
                if (effect->id == active.definition && effect->generation == active.generation && effect->lock_movement)
                    return true;
    return false;
}

auto SceneAuthority::Impl::arm(Actor& actor) -> void {
    const auto& config = *actor.config;
    require(!config.attributes.empty(), "Combat actor needs resolved attributes");
    AttributeSet(config.attributes);
    actor.perception = std::make_unique<Perception>(config.senses);
    actor.motor = std::make_unique<CharacterMotor>(authoritative, actor.foot, actor.id);
    auto state = actor.motor->state();
    state.yaw = actor.yaw;
    state.tick = state.sequence = authoritative.tick();
    actor.motor->restore(state);
    actor.owner = server.configure_abilities(actor.id, config.attributes, combat->health, combat->maximum_health);
    if (combat->tags)
        server.configure_ability_tags(actor.owner, combat->tags);
    if (config.kit) {
        validate_combat_kit(*config.kit, config.input);
        for (const auto& ability : config.abilities)
            require(ability && std::any_of(
                                   combat->abilities.begin(), combat->abilities.end(),
                                   [&](const auto& known) { return known->id == ability->id && known->generation == ability->generation; }),
                    "Actor ability is not in the prepared scene abilities");
        server.equip_combat_kit(actor.owner, config.kit, config.input, config.abilities);
    } else
        require(config.abilities.empty(), "An actor without a kit cannot carry abilities");
    server.publish_motor(actor.id, actor.motor->state());
}

auto SceneAuthority::Impl::retire(std::size_t position) -> void {
    const auto id = actors[position].id;
    actors.erase(actors.begin() + std::ptrdiff_t(position));
    actor_identities.erase(actor_identities.begin() + std::ptrdiff_t(position));
    server.destroy(id);
    actor_commands.resize(actors.size());
    actor_motions.resize(actors.size());
    actor_before.resize(actors.size());
    actor_requested.resize(actors.size());
}

auto SceneAuthority::Impl::slice(std::size_t& cursor, unsigned budget) -> void {
    turn.assign(actors.size(), 0);
    if (actors.empty())
        return;
    if (cursor >= actors.size())
        cursor = 0;
    unsigned given = 0;
    std::size_t last = cursor;
    for (std::size_t step = 0; step < actors.size() && given < budget; ++step) {
        const auto index = (cursor + step) % actors.size();
        if (!actors[index].config->controller)
            continue;
        turn[index] = 1;
        ++given;
        last = index;
    }
    if (given)
        cursor = (last + 1) % actors.size();
}

auto SceneAuthority::Impl::find(StableId identity) const -> const Actor* {
    for (const auto& actor : actors)
        if (actor.identity == identity)
            return &actor;
    return nullptr;
}

auto SceneAuthority::Impl::server_candidates() const -> std::vector<TargetCandidate> {
    std::vector<TargetCandidate> result;
    for (const auto& actor : actors)
        if (actor.faction != player_faction && living(actor.id))
            result.push_back({actor.id, actor.motor->state().position});
    return result;
}

auto SceneAuthority::Impl::locked_motor(const Actor& actor, std::uint64_t& id) const -> const MotorState* {
    if (!actor.locked)
        return nullptr;
    if (const auto* chosen = member(*actor.locked)) {
        id = chosen->id;
        return &chosen->motor->state();
    }
    if (const auto* other = find(*actor.locked)) {
        id = other->id;
        return &other->motor->state();
    }
    return nullptr;
}

auto SceneAuthority::Impl::locked_candidates(const Actor& actor) const -> std::vector<TargetCandidate> {
    std::vector<TargetCandidate> result;
    std::uint64_t id{};
    if (const auto* state = locked_motor(actor, id))
        if (living(id))
            result.push_back({id, state->position});
    return result;
}

auto SceneAuthority::Impl::aim_actor(const Actor& actor, const AbilityDefinition& ability, MotorInput& command) const -> void {
    if (!ability.aim || ability.melee.empty())
        return;
    const auto candidates = locked_candidates(actor);
    if (candidates.empty())
        return;
    const auto& self = actor.motor->state();
    const auto& goal = candidates.front().position;
    MotorVec drift{};
    if (ability.aim->projectile) {
        std::uint64_t id{};
        if (const auto* target = locked_motor(actor, id))
            drift = {target->achieved.x * 60, 0, target->achieved.z * 60};
    }
    double seconds = 0, dx = 0, dz = 0, level = 0;
    MotorVec muzzle{};
    for (unsigned pass = 0; pass < 3; ++pass) {
        const MotorVec ahead{goal.x + drift.x * seconds, goal.y, goal.z + drift.z * seconds};
        MotorState turned = self;
        turned.yaw = std::atan2(ahead.x - self.position.x, ahead.z - self.position.z);
        muzzle = melee_origin(turned, ability.melee.front());
        dx = ahead.x - muzzle.x;
        dz = ahead.z - muzzle.z;
        level = std::hypot(dx, dz);
        if (level < .3)
            return;
        if (!ability.aim->projectile)
            break;
        seconds = level / ability.aim->projectile->speed;
    }
    double height = goal.y + perception_body_height - muzzle.y;
    if (ability.aim->projectile)
        height += .5 * ability.aim->projectile->gravity * seconds * seconds;
    command.yaw = std::atan2(dx, dz);
    command.pitch = std::clamp(std::atan2(height, level), -1.4, 1.4);
}

auto SceneAuthority::Impl::actor_ability(const Actor& actor, CombatSlot slot) const -> std::shared_ptr<const AbilityDefinition> {
    if (!actor.config->kit)
        return {};
    const auto wanted = actor.config->kit->slots[static_cast<unsigned>(slot)].ability;
    if (wanted == AssetId{})
        return {};
    for (const auto& ability : actor.config->abilities)
        if (ability->id == wanted)
            return ability;
    return {};
}

auto SceneAuthority::Impl::sight(MotorVec from, MotorVec to) const -> bool {
    return authoritative.query_ray(from, {to.x - from.x, to.y - from.y, to.z - from.z}, {StaticCollision, 0}).hits.empty();
}

auto SceneAuthority::Impl::sense(Actor& actor, std::uint64_t tick) -> void {
    seen.clear();
    std::size_t next = 0;
    auto list_members = [&](std::uint64_t before) {
        for (; next < members.size() && members[next].id < before; ++next)
            seen.push_back({members[next].identity, player_faction, members[next].motor->state(), living(members[next].id)});
    };
    for (const auto& other : actors) {
        list_members(other.id);
        if (&other != &actor)
            seen.push_back({other.identity, other.faction, other.motor->state(), living(other.id)});
    }
    list_members(UINT64_MAX);
    actor.perception->sense(tick, actor.motor->state(), living(actor.id), seen,
                            [this](MotorVec from, MotorVec to) { return sight(from, to); });
}

auto SceneAuthority::Impl::perceived(const Actor& actor, StableId identity) const -> const PerceivedActor* {
    for (const auto& other : actor.perception->actors())
        if (other.id == identity)
            return &other;
    return nullptr;
}

auto SceneAuthority::Impl::drive_controller(Actor& actor, std::uint64_t tick, MotorInput& command) -> bool {
    ControllerView view;
    view.tick = tick;
    view.self = actor.motor->state();
    view.alive = living(actor.id);
    view.faction = actor.faction;
    view.perceived = actor.perception->actors();
    auto snapshot = server.ability_snapshot(actor.owner);
    view.busy = bool(snapshot.active);
    const auto decision = actor.config->controller->decide(view);
    require(std::isfinite(decision.move_x) && std::isfinite(decision.move_z) &&
                decision.move_x * decision.move_x + decision.move_z * decision.move_z <= 1.00001 &&
                (!decision.face || std::isfinite(*decision.face)) &&
                (!decision.press || static_cast<unsigned>(*decision.press) < combat_slot_count),
            "Controller command bounds");
    const auto* aimed_at = decision.target ? perceived(actor, *decision.target) : nullptr;
    require(!decision.target || (aimed_at && aimed_at->detected), "Controller target is not an actor it detects");
    actor.chosen = decision.target;
    command.x = decision.move_x;
    command.z = decision.move_z;
    bool pressed = false;
    if (decision.press) {
        require(actor.operation != UINT64_MAX, "Controller operation identity exhausted");
        // An aimed ability is only loosed at a chosen target the actor has line of sight to; otherwise the press never becomes a request.
        bool withheld = false;
        if (const auto ability = actor_ability(actor, *decision.press); ability && ability->aim && aimed_at && !aimed_at->visible)
            withheld = true;
        else if (ability) {
            if (actor.operation > 1)
                server.retire_ability_operations(actor.owner, actor.operation - 1);
            const auto& slot = actor.config->kit->slots[static_cast<unsigned>(*decision.press)];
            actor.receipt =
                server.request_ability({actor.owner,
                                        actor.operation++,
                                        snapshot.grant_generation,
                                        *decision.press,
                                        {slot.input_action, ability->activate_on, tick * 16667, ability->minimum_held_us, 1, false},
                                        false});
        } else {
            AbilityReceipt refused;
            refused.failure = AbilityFailure::Unassigned;
            refused.tick = tick;
            actor.receipt = refused;
        }
        pressed = !withheld && actor.receipt->failure == AbilityFailure::None;
        // The target an accepted press was aimed at stays the action's soft-lock target.
        if (pressed)
            actor.locked = decision.target;
    }
    if (decision.face && (!decision.press || pressed))
        command.yaw = std::remainder(*decision.face, 6.283185307179586);
    actor.hold_x = decision.move_x;
    actor.hold_z = decision.move_z;
    actor.hold_face = decision.face && (!decision.press || pressed) ? decision.face : std::optional<double>{};
    validate_motor_input(command);
    return pressed;
}
} // namespace darkangel
