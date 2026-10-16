#include "scene_authority_impl.hpp"

namespace darkangel {
SceneAuthority::Impl::Impl(SessionHandshake hello, std::span<const ObjectData> objects, std::span<const SceneSeat> party,
                           const CollisionDefinition& collision, std::shared_ptr<const CharacterSceneCombat> configured,
                           const SceneStart& start, SceneTiming mode)
    : server(SessionRole::Server, hello, scene_detail::session_limits(mode)), timing(mode), combat(std::move(configured)) {
    require((hello.protocol == 2 || hello.protocol == 3) && (!combat || hello.protocol == 3),
            "Character scene requires wire2 and the initial human profile");
    require(!objects.empty() && objects.size() <= 128 && !party.empty() && party.size() <= scene_party_limit && party.front().id,
            "Character scene object/player bounds");
    if (combat && combat->scene != AssetId{})
        ledger = start.ledger;
    if (combat && combat->inventory) {
        inventory_store = start.inventory;
        if (!inventory_store) {
            owned_inventory = std::make_unique<PartyInventory>(combat->inventory->catalog);
            inventory_store = owned_inventory.get();
        }
        inventory_session = std::make_unique<InventoryHostSession>(
            server, *inventory_store, [this](std::uint64_t network, StableId profile, const InventoryRequest& request) {
                return inventory_store->request(
                    profile, request,
                    [&](const InventorySnapshot& held, const InventorySnapshot& after) {
                        auto found = std::find_if(members.begin(), members.end(), [&](const auto& m) { return m.id == network; });
                        require(found != members.end(), "Inventory member missing");
                        // Using a consumable: only a living member, only an item that does something; its Health comes back now.
                        if (request.mutation == InventoryMutation::Consume) {
                            if (server.ability_available(found->owner, combat->health) <= 0)
                                return InventoryFailure::Unavailable;
                            auto entry = std::find_if(held.entries.begin(), held.entries.end(),
                                                      [&](const auto& e) { return e.instance == request.instance; });
                            if (entry == held.entries.end())
                                return InventoryFailure::Missing;
                            const auto& item = inventory_store->catalog().get(entry->item);
                            if (item.restore_health <= 0)
                                return InventoryFailure::NotUsable;
                            server.heal(found->owner, item.restore_health * request.quantity);
                            return InventoryFailure::None;
                        }
                        const auto before = server.ability_snapshot(found->owner);
                        if (before.active || server.ability_available(found->owner, combat->health) <= 0)
                            return InventoryFailure::Unavailable;
                        const auto masks = inventory_masks(after, inventory_store->catalog());
                        if (masks == before.equipped_masks)
                            return InventoryFailure::None;
                        auto active = before.loadout;
                        auto loadouts = inventory_loadouts(*combat->inventory, masks, active);
                        server.configure_ability_loadouts(found->owner, loadouts, active);
                        return InventoryFailure::None;
                    },
                    false);
            });
    }
    // The party's world view travels on its own owner channel; altar menus answer on it.
    if (combat)
        server.register_owner_channel(world_state_channel);

    for (const auto& seat : party) {
        require(seat.transport && bool(seat.id) &&
                    std::count_if(party.begin(), party.end(), [&](const auto& other) { return other.id == seat.id; }) == 1,
                "Scene party seat");
        server.attach(*seat.transport, seat.peer);
        Member member;
        member.identity = seat.id;
        member.profile = seat.profile ? seat.profile : start.profiles.contains(seat.id) ? start.profiles.at(seat.id) : seat.id;
        member.peer = seat.peer;
        member.foot = seat.position;
        member.yaw = seat.yaw;
        members.push_back(std::move(member));
        member_identities.push_back(seat.id);
    }
    for (const auto& box : collision.boxes)
        require(!box.moving && !box.dynamic && !box.sensor, "Character scene initial profile requires static collision");
    authoritative.load_scene(collision);
    const auto identity = party.front().id;
    const ObjectData* first = nullptr;
    if (combat)
        require(combat->actors.size() <= actor_limit() && !combat->faction.empty(), "Combat scene actor/faction bounds");
    // A scene actor the ledger holds as defeated is not in this session: neither its object nor its character.
    std::set<StableId> absent;
    if (ledger)
        for (const auto& config : combat->actors) {
            ActorState state;
            ledger->restore(persistent(config.id), state);
            if (state.defeated)
                absent.insert(config.id);
        }
    // With an arrival point the whole party stands in formation around it; otherwise the first member stands where
    // the scene's player object is and the others where their seats say.
    if (start.arrival) {
        require(std::isfinite(start.arrival->position.x) && std::isfinite(start.arrival->position.y) &&
                    std::isfinite(start.arrival->position.z) && std::isfinite(start.arrival->yaw),
                "Scene arrival place");
        std::vector<MotorVec> taken;
        if (combat)
            for (const auto& object : objects)
                if (!absent.contains(object.id) &&
                    std::any_of(combat->actors.begin(), combat->actors.end(), [&](const auto& config) { return config.id == object.id; }))
                    taken.push_back({object.transform.x, object.transform.y, object.transform.z});
        for (std::size_t seat = 0; seat < members.size(); ++seat) {
            members[seat].foot = formation(*start.arrival, seat, taken);
            members[seat].yaw = start.arrival->yaw;
            taken.push_back(members[seat].foot);
        }
    }
    for (const auto& source : objects) {
        if (absent.contains(source.id))
            continue;
        auto object = source;
        if (object.id == identity && start.arrival) {
            object.transform.x = members.front().foot.x;
            object.transform.y = members.front().foot.y;
            object.transform.z = members.front().foot.z;
            object.transform.yaw = members.front().yaw;
        }
        auto id = server.create(object);
        if (object.id == identity) {
            require(!members.front().id, "Duplicate character scene player");
            require(object.transform.scale == 1 && object.transform.pitch == 0 && object.transform.roll == 0 && object.health.current > 0,
                    "Character scene requires an upright, unscaled live player");
            members.front().id = id;
            members.front().foot = {object.transform.x, object.transform.y, object.transform.z};
            members.front().yaw = object.transform.yaw;
            first = &source;
            member_template = source;
        }
        for (std::size_t seat = 1; seat < party.size(); ++seat)
            require(object.id != party[seat].id, "A later party member's identity is a scene object's");
        if (combat)
            for (const auto& config : combat->actors)
                if (config.id == object.id) {
                    require(
                        object.id != identity &&
                            std::none_of(actors.begin(), actors.end(), [&](const auto& known) { return known.identity == object.id; }) &&
                            object.transform.scale == 1 && object.health.current > 0,
                        "Combat actor profile");
                    Actor actor;
                    actor.identity = object.id;
                    actor.id = id;
                    actor.config = &config;
                    actor.foot = {object.transform.x, object.transform.y, object.transform.z};
                    actor.yaw = object.transform.yaw;
                    actors.push_back(std::move(actor));
                }
    }
    require(first && members.front().id, "Character scene player missing");
    // A later member's character is the player object's data at the member's own place.
    for (std::size_t seat = 1; seat < members.size(); ++seat) {
        auto& member = members[seat];
        require(std::isfinite(member.foot.x) && std::isfinite(member.foot.y) && std::isfinite(member.foot.z) && std::isfinite(member.yaw),
                "Scene party seat place");
        ObjectData object;
        object.id = member.identity;
        object.transform = first->transform;
        object.transform.x = member.foot.x;
        object.transform.y = member.foot.y;
        object.transform.z = member.foot.z;
        object.transform.yaw = member.yaw;
        object.health = first->health;
        member.id = server.create(object);
        require(members[seat - 1].id < member.id, "A party member's network identity is out of order");
    }
    if (combat) {
        require(actors.size() + absent.size() == combat->actors.size(), "Combat actor has no scene object");
        std::sort(actors.begin(), actors.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        // Factions become session-local numbers in simulation order; the party's is 1.
        factions = {{combat->faction, player_faction}};
        for (auto& actor : actors) {
            actor.faction = factions.emplace(actor.config->faction, std::uint32_t(factions.size() + 1)).first->second;
            actor_identities.push_back(actor.identity);
        }
        for (const auto& member : members)
            server.assign_team(member.id, player_faction);
        for (const auto& actor : actors)
            server.assign_team(actor.id, actor.faction);
        actor_commands.resize(actors.size());
        actor_motions.resize(actors.size());
        actor_before.resize(actors.size());
        actor_requested.resize(actors.size());
        seen.reserve(actors.size() + members.size());
    }
    for (auto& member : members)
        member.motor = std::make_unique<CharacterMotor>(authoritative, member.foot, member.id, false, combat ? combat->movement : MotorParameters{});
    if (combat) {
        require(combat->kit && combat->evaluator && (bool(combat->damage) || bool(combat->combat_damage)),
                "Combat scene requires frozen kit/game evaluator");
        validate_combat_kit(*combat->kit, combat->input);
        for (const auto& ability : combat->abilities)
            require(ability && ability->action->motion, "Scene action requires a frozen clip");
        server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(authoritative));
        if (combat->combat_damage)
            server.register_combat_evaluator(combat->evaluator, combat->combat_damage);
        else
            server.register_damage_evaluator(combat->evaluator, combat->damage);
        for (const auto& [id, evaluator] : combat->effect_evaluators)
            server.register_effect_evaluator(id, evaluator);
        if (!combat->loadouts.empty())
            require(combat->loadouts.size() == loadout_slot_count && combat->active_loadout < loadout_slot_count &&
                        combat->loadouts[combat->active_loadout],
                    "Scene loadouts need four slots and an equipped active slot");
        // Every member is equipped from the session's player definition.
        for (auto& member : members) {
            member.owner = server.configure_abilities(member.id, combat->attributes, combat->health, combat->maximum_health);
            if (combat->tags)
                server.configure_ability_tags(member.owner, combat->tags);
            if (inventory_session) {
                inventory_store->join(member.profile, combat->inventory->starting, combat->inventory->equipped);
                auto active = combat->inventory->active;
                auto loadouts = inventory_loadouts(
                    *combat->inventory, inventory_masks(inventory_store->state(member.profile).inventory, inventory_store->catalog()),
                    active);
                server.configure_ability_loadouts(member.owner, loadouts, active);
                inventory_session->bind(member.id, member.profile);
            } else if (combat->loadouts.empty())
                server.equip_combat_kit(member.owner, combat->kit, combat->input, combat->abilities);
            else
                server.configure_ability_loadouts(member.owner, combat->loadouts, combat->active_loadout);
        }
        // Every actor gets its own motor and ability owner; one with a kit is equipped with it.
        for (auto& actor : actors)
            arm(actor);
        {
            require(scene_actor_demand(*combat) <= actor_limit(),
                    "The scene's spawn points and the most its encounters can have alive at once are more actors than one session holds");
            require(combat->decision_budget >= 1 && combat->sense_budget >= 1 && combat->spawn_budget >= 1,
                    "Time slicing budgets are at least 1");
        }
        for (const auto& encounter : combat->encounters) {
            for (auto npc : encounter.definition.npcs()) {
                auto found = encounter.npcs.find(npc);
                require(found != encounter.npcs.end() && !found->second.attributes.empty() && !found->second.faction.empty(),
                        "An encounter's NPC definition is not prepared");
                for (const auto& ability : found->second.abilities)
                    require(ability && std::any_of(combat->abilities.begin(), combat->abilities.end(),
                                                   [&](const auto& known) {
                                                       return known->id == ability->id && known->generation == ability->generation;
                                                   }),
                            "An encounter NPC's ability is not in the prepared scene abilities");
            }
            require(bool(encounter.id) &&
                        std::find(encounter_identities.begin(), encounter_identities.end(), encounter.id) == encounter_identities.end(),
                    "Encounter identity");
            directors.emplace_back(encounter.definition, encounter.volumes, encounter.points, encounter.id.high ^ encounter.id.low);
            encounter_identities.push_back(encounter.id);
        }
        for (const auto& trigger : combat->triggers)
            triggers.emplace_back(trigger.definition, trigger.center, trigger.yaw);
        require(combat->interactables.size() <= 64, "Scene interactable limit");
        for (const auto& item : combat->interactables) {
            require(bool(item.id) && std::count_if(combat->interactables.begin(), combat->interactables.end(),
                                                   [&](const auto& other) { return other.id == item.id; }) == 1,
                    "Interactable identity");
            interactables.emplace_back(item.definition, item.position);
        }
        require(combat->transitions.size() <= 16, "Scene transition limit");
        for (const auto& item : combat->transitions) {
            validate_transition(item.definition);
            require(bool(item.id) &&
                        std::count_if(combat->transitions.begin(), combat->transitions.end(),
                                      [&](const auto& other) { return other.id == item.id; }) == 1 &&
                        std::none_of(combat->interactables.begin(), combat->interactables.end(),
                                     [&](const auto& other) { return other.id == item.id; }),
                    "Scene transition identity");
            portals.push_back({EncounterVolume{item.center, item.yaw, item.definition.half}, false});
        }
        world_objects = std::make_unique<scene_detail::SceneWorldObjects>(combat, authoritative, ledger, inventory_store);
        lifecycle = std::make_unique<PartyLifecycleCoordinator>(combat->party);
        // Persistent objects take their state from the ledger before the first tick.
        if (ledger) {
            for (std::size_t index = 0; index < interactables.size(); ++index) {
                InteractableState state(interactables[index]);
                ledger->restore(persistent(combat->interactables[index].id), state);
            }
            for (std::size_t index = 0; index < triggers.size(); ++index) {
                TriggerState state(triggers[index]);
                ledger->restore(persistent(combat->triggers[index].id), state);
            }
            for (std::size_t index = 0; index < directors.size(); ++index) {
                EncounterState state(directors[index]);
                ledger->restore(persistent(encounter_identities[index]), state);
            }
        }
        for (const auto& director : directors)
            director_phases.push_back(director.status().phase);
        world.emit("play.started");
        server.drain_ability_actions();
    }
    for (auto& member : members) {
        auto state = member.motor->state();
        state.topology = authoritative.topology();
        state.yaw = member.yaw;
        member.motor->restore(state);
    }
    for (const auto& member : members) {
        server.own_motor(member.id, member.peer);
        server.publish_motor(member.id, member.motor->state());
    }
    // Clients are told which single-use interactables are already spent.
    for (std::size_t index = 0; index < interactables.size(); ++index)
        if (interactables[index].definition().once && interactables[index].used())
            for (const auto& member : members)
                server.answer_interaction(member.id, combat->interactables[index].id, std::uint8_t(InteractionResult::UsedUp));
    server.publish_collision(collision_stream(authoritative.capture()));
}
} // namespace darkangel
