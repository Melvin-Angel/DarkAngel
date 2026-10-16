#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/melee.hpp>
#include <darkangel/effects.hpp>
#include <darkangel/input.hpp>
#include <darkangel/perception.hpp>
#include <darkangel/encounter.hpp>
#include <darkangel/interaction.hpp>
#include <darkangel/scene_travel.hpp>
#include <darkangel/world_ledger.hpp>
#include <functional>
#include <optional>
#include <darkangel/world_objects.hpp>
#include <darkangel/party_lifecycle.hpp>

#include <darkangel/scene_definitions.hpp>
#include <darkangel/motor_parameters.hpp>

namespace darkangel {
// A server-driven actor of a combat scene: a spawned NPC or a training dummy. The scene
// object with this identity gives its transform and Health. Its abilities, effects and
// clips must be in the combat's prepared unions. Without a kit it is unequipped; without a
// controller it never acts. Name, faction and generation are presentation, hostility and
// fingerprint data. The player is not in this list: it alone is predicted by this client.
struct CharacterSceneActor {
    StableId id;
    std::shared_ptr<const CombatKitDefinition> kit;
    InputProfile input;
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    // Resolved starting attributes over the scene's attribute schema.
    std::vector<AttributeDefinition> attributes;
    std::string name, faction, generation;
    std::shared_ptr<ActorController> controller;
    // What the actor can notice. Without sensors it perceives nothing and its controller finds no target.
    SensesDefinition senses;
    // The NPC's frozen Movement Profile; the default is the legacy built-in motor.
    MotorParameters movement;
};
// An encounter of the scene: its definition, bounds and spawn points, a prepared actor for each NPC definition it
// spawns (kit, abilities, attributes, name, faction, senses; identity and controller are filled per enemy), and the
// host's source of a fresh controller for each enemy. Its enemies enter the session during play.
struct CharacterSceneEncounter {
    StableId id;
    EncounterDefinition definition;
    std::vector<EncounterVolume> volumes;
    std::vector<EncounterPoint> points;
    std::map<AssetId, CharacterSceneActor> npcs;
    std::function<std::shared_ptr<ActorController>(AssetId npc)> controller;
};
// A placed interactable: the scene object, its component and where it stands.
struct CharacterSceneInteractable {
    StableId id;
    InteractableDefinition definition;
    MotorVec position{};
};
struct CharacterSceneTrigger {
    StableId id;
    TriggerDefinition definition;
    MotorVec center{};
    double yaw{};
};
// A placed scene transition trigger: the scene object, its component and where its box stands.
struct CharacterSceneTransition {
    StableId id;
    TransitionDefinition definition;
    MotorVec center{};
    double yaw{};
};
// Placed M8 world objects. A mover's start is its box centre at the start end, its definition's offset already turned
// into the scene's axes; yaw turns the box. An altar's arrival is where the party stands when it arrives or respawns there.
struct CharacterScenePickup {
    StableId id;
    PickupDefinition definition;
    MotorVec position{};
};
struct CharacterSceneAltar {
    StableId id;
    AltarDefinition definition;
    MotorVec position{};
    double yaw{};
    MotorVec arrival{};
};
struct CharacterSceneMover {
    StableId id;
    MoverDefinition definition;
    MotorVec start{};
    double yaw{};
};
struct CharacterSceneCombat {
    std::shared_ptr<const SceneInventoryDefinition> inventory;
    std::shared_ptr<const CombatKitDefinition> kit;
    InputProfile input;
    std::vector<AttributeDefinition> attributes;
    AttributeId health{}, maximum_health{};
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    std::map<AssetId, std::shared_ptr<const AnimationClip>> clips;
    std::uint32_t evaluator{};
    DamageEvaluator damage;
    std::shared_ptr<const TagDictionary> tags;
    CombatEvaluator combat_damage;
    std::map<std::uint32_t, EffectEvaluator> effect_evaluators;
    std::vector<std::shared_ptr<const EffectDefinition>> effects;
    std::string actor_generation;
    // The Player's frozen Movement Profile: every party member's host motor and the owner's prediction and replay use it.
    MotorParameters movement;
    // Optional presentation-only death timeline from the frozen kit closure.
    std::shared_ptr<const ActionDefinition> death;
    // Optional equipped-mask loadouts (empty, or exactly four slots). abilities, effects
    // and clips above then hold the union so presentation stays prepared across switches.
    std::vector<std::optional<AbilityLoadout>> loadouts;
    unsigned active_loadout{};
    // Semantic input actions that select loadout slots 0-3 (zero = unbound).
    std::array<std::uint32_t, loadout_slot_count> loadout_actions{};
    // Server-driven actors, at most character_scene_actor_limit. Actors of different
    // factions are hostile to each other; the player's faction is below.
    std::vector<CharacterSceneActor> actors;
    std::string faction{"player"};
    // Encounters and trigger volumes. The actors above plus the most each encounter can have alive at once must fit
    // character_scene_actor_limit; an encounter's dead are removed after its corpse time, or sooner when a spawn needs the room.
    std::vector<CharacterSceneEncounter> encounters;
    std::vector<CharacterSceneTrigger> triggers;
    std::vector<CharacterSceneInteractable> interactables;
    // Scene transition triggers, at most 16, and the scene asset this content was prepared from: the scene half of the
    // persistent identity of its objects in the world ledger. A scene with no asset identity records nothing.
    std::vector<CharacterSceneTransition> transitions;
    AssetId scene;
    // Pickups (at most 64), altars and movers (at most 16 each), and the party's downed/revive/wipe rules.
    std::vector<CharacterScenePickup> pickups;
    std::vector<CharacterSceneAltar> altars;
    std::vector<CharacterSceneMover> movers;
    PartyRules party;
    // Time slicing: per tick, at most this many controllers decide, this many actors sense and this many enemies enter.
    // The rest keep their latest decision (movement and facing) and perception until their turn, in ascending order
    // from where the previous tick stopped. An actor without a controller neither senses nor decides.
    unsigned decision_budget{8}, sense_budget{8}, spawn_budget{2};
};
} // namespace darkangel
