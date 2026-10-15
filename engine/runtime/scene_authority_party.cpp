#include "scene_authority_impl.hpp"

namespace darkangel {
auto SceneAuthority::Impl::formation(const SceneArrival& arrival, std::size_t seat, std::span<const MotorVec> taken) const -> MotorVec {
    MotorVec fallback = arrival.position;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        MotorVec at = arrival.position;
        if (seat) {
            const double angle = arrival.yaw + 3.141592653589793 + 2.0943951023931953 * double(seat - 1), reach = 1.2 + .6 * attempt;
            at.x += std::sin(angle) * reach;
            at.z += std::cos(angle) * reach;
        } else if (attempt) {
            const double angle = 2.399963229728653 * attempt, reach = .45 * attempt;
            at.x += std::sin(angle) * reach;
            at.z += std::cos(angle) * reach;
        }
        const auto found = authoritative.query_ray({at.x, arrival.position.y + 2, at.z}, {0, -6, 0}, {StaticCollision, 0});
        const CollisionHit* nearest = nullptr;
        for (const auto& hit : found.hits)
            if (!nearest || hit.fraction < nearest->fraction)
                nearest = &hit;
        if (!nearest)
            continue;
        at.y = nearest->point.y;
        if (!attempt)
            fallback = at;
        if (std::none_of(taken.begin(), taken.end(), [&](const MotorVec& other) {
                return std::hypot(other.x - at.x, other.z - at.z) < 1. && std::abs(other.y - at.y) < 2.5;
            }))
            return at;
    }
    return fallback;
}

auto SceneAuthority::Impl::member(StableId identity) const -> const Member* {
    for (const auto& known : members)
        if (known.identity == identity)
            return &known;
    return nullptr;
}

auto SceneAuthority::Impl::member(std::uint64_t network) const -> const Member* {
    for (const auto& known : members)
        if (known.id == network)
            return &known;
    return nullptr;
}

auto SceneAuthority::Impl::retire_departed() -> void {
    for (auto& gone : departed) {
        gone.motor.reset();
        server.destroy(gone.network);
    }
    departed.clear();
}

auto SceneAuthority::Impl::add_member(const SceneSeat& seat) -> void {
    require(timing == SceneTiming::Independent && bool(combat), "Member admission requires an independent combat scene");
    require(seat.transport && bool(seat.id) && seat.transport->valid(seat.peer), "Scene party seat");
    require(!member(seat.id) &&
                std::none_of(arriving.begin(), arriving.end(),
                             [&](const auto& other) { return other.id == seat.id || other.transport == seat.transport; }) &&
                !find(seat.id),
            "The member is already in the scene");
    require(members.size() + arriving.size() < scene_party_limit, "The party is full");
    require(scene_actor_demand(*combat) + members.size() + arriving.size() + 1 <= session_character_capacity,
            "The party does not fit the scene's character capacity");
    arriving.push_back(seat);
}

auto SceneAuthority::Impl::enter(const SceneSeat& seat) -> void {
    const auto& lead = members.front().motor->state();
    std::vector<MotorVec> taken;
    for (const auto& known : members)
        taken.push_back(known.motor->state().position);
    for (const auto& actor : actors)
        taken.push_back(actor.motor->state().position);
    Member entering;
    entering.identity = seat.id;
    entering.profile = seat.profile ? seat.profile : seat.id;
    entering.peer = seat.peer;
    entering.foot = formation(SceneArrival{lead.position, lead.yaw}, members.size(), taken);
    entering.yaw = lead.yaw;
    ObjectData object;
    object.id = seat.id;
    object.transform = member_template.transform;
    object.transform.x = entering.foot.x;
    object.transform.y = entering.foot.y;
    object.transform.z = entering.foot.z;
    object.transform.yaw = entering.yaw;
    object.health = member_template.health;
    entering.id = server.create(object);
    try {
        require(members.back().id < entering.id, "A party member's network identity is out of order");
        server.assign_team(entering.id, player_faction);
        entering.motor = std::make_unique<CharacterMotor>(authoritative, entering.foot, entering.id);
        auto state = entering.motor->state();
        state.yaw = entering.yaw;
        state.tick = state.sequence = authoritative.tick();
        entering.motor->restore(state);
        entering.owner = server.configure_abilities(entering.id, combat->attributes, combat->health, combat->maximum_health);
        if (combat->tags)
            server.configure_ability_tags(entering.owner, combat->tags);
        if (inventory_session) {
            inventory_store->join(entering.profile, combat->inventory->starting, combat->inventory->equipped);
            auto active = combat->inventory->active;
            auto loadouts =
                inventory_loadouts(*combat->inventory,
                                   inventory_masks(inventory_store->state(entering.profile).inventory, inventory_store->catalog()), active);
            server.configure_ability_loadouts(entering.owner, loadouts, active);
            inventory_session->bind(entering.id, entering.profile);
        } else if (combat->loadouts.empty())
            server.equip_combat_kit(entering.owner, combat->kit, combat->input, combat->abilities);
        else
            server.configure_ability_loadouts(entering.owner, combat->loadouts, combat->active_loadout);
        server.attach(*seat.transport, seat.peer);
        server.own_motor(entering.id, entering.peer, authoritative.tick());
        server.publish_motor(entering.id, entering.motor->state());
        for (std::size_t index = 0; index < interactables.size(); ++index)
            if (interactables[index].definition().once && interactables[index].used())
                server.answer_interaction(entering.id, combat->interactables[index].id, std::uint8_t(InteractionResult::UsedUp));
    } catch (...) {
        if (inventory_session)
            inventory_session->unbind(entering.id);
        entering.motor.reset();
        if (server.attached(seat.peer))
            server.detach(seat.peer);
        server.destroy(entering.id);
        throw;
    }
    members.push_back(std::move(entering));
    member_identities.push_back(seat.id);
}

auto SceneAuthority::Impl::admit_arrivals() -> void {
    for (const auto& seat : arriving)
        try {
            enter(seat);
        } catch (const std::exception& error) {
            refused.push_back({seat.id, error.what()});
        }
    arriving.clear();
}

auto SceneAuthority::Impl::remove_member(StableId id) -> bool {
    require(timing == SceneTiming::Independent, "Member removal requires independent scene timing");
    for (std::size_t index = 1; index < members.size(); ++index)
        if (members[index].identity == id) {
            const auto peer = members[index].peer;
            const auto network = members[index].id;
            if (inventory_session)
                inventory_session->unbind(network);
            if (server.attached(peer))
                server.detach(peer);
            departed.push_back({network, std::move(members[index].motor)});
            members.erase(members.begin() + std::ptrdiff_t(index));
            member_identities.erase(member_identities.begin() + std::ptrdiff_t(index));
            return true;
        }
    return false;
}

auto SceneAuthority::Impl::pump() -> void {
    try {
        server.tick();
        if (inventory_session)
            inventory_session->pump();
    } catch (...) {
        if (timing != SceneTiming::Independent)
            throw;
        std::vector<StableId> lost;
        for (std::size_t index = 1; index < members.size(); ++index)
            if (!server.attached(members[index].peer))
                lost.push_back(members[index].identity);
        if (lost.empty())
            throw;
        for (auto member : lost)
            remove_member(member);
    }
}
} // namespace darkangel
