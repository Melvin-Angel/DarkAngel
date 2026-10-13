#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/actor_controller.hpp>
#include <darkangel/encounter.hpp>
#include <darkangel/interaction.hpp>
#include <darkangel/cue_player.hpp>
#include <darkangel/scene_travel.hpp>
#include <darkangel/world_ledger.hpp>
#include <functional>
#include <optional>

namespace darkangel {
enum class SceneTiming {Lockstep,Independent};
struct ReactionSample;
struct CharacterSceneInput {double x{},z{},yaw{};bool jump{},crouch{};std::vector<InputEvent> combat_events;
    // Aim elevation in radians; see CharacterSceneSession::aim.
    double pitch{};
    // The interactable the player asks to use on this step, if any. A request, resolved by authority.
    std::optional<StableId> interact;};
// Crosshair convergence: where a camera ray meets the world, and the yaw and pitch that point a shot from
// the owner's muzzle at that point. hit is false when the ray met nothing within range and the far point is used.
struct AimSolution {double yaw{},pitch{};MotorVec point{};bool hit{};};
// A server-driven actor of a combat scene: a spawned NPC or a training dummy. The scene
// object with this identity gives its transform and Health. Its abilities, effects and
// clips must be in the combat's prepared unions. Without a kit it is unequipped; without a
// controller it never acts. Name, faction and generation are presentation, hostility and
// fingerprint data. The player is not in this list: it alone is predicted by this client.
struct CharacterSceneActor {
    StableId id;
    std::shared_ptr<const CombatKitDefinition> kit;InputProfile input;
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    // Resolved starting attributes over the scene's attribute schema.
    std::vector<AttributeDefinition> attributes;
    std::string name,faction,generation;
    std::shared_ptr<ActorController> controller;
    // What the actor can notice. Without sensors it perceives nothing and its controller finds no target.
    SensesDefinition senses;
};
// An encounter of the scene: its definition, bounds and spawn points, a prepared actor for each NPC definition it
// spawns (kit, abilities, attributes, name, faction, senses; identity and controller are filled per enemy), and the
// host's source of a fresh controller for each enemy. Its enemies enter the session during play.
struct CharacterSceneEncounter {
    StableId id;EncounterDefinition definition;std::vector<EncounterVolume> volumes;std::vector<EncounterPoint> points;
    std::map<AssetId,CharacterSceneActor> npcs;
    std::function<std::shared_ptr<ActorController>(AssetId npc)> controller;
};
// A placed interactable: the scene object, its component and where it stands.
struct CharacterSceneInteractable {StableId id;InteractableDefinition definition;MotorVec position{};};
// What the owner's HUD offers, and how authority answered the owner's latest request. Presentation only.
struct InteractionPrompt {StableId id;std::string prompt;};
struct InteractionOutcome {StableId object;InteractionResult result{};std::uint64_t tick{};};
struct CharacterSceneTrigger {StableId id;TriggerDefinition definition;MotorVec center{};double yaw{};};
// A placed scene transition trigger: the scene object, its component and where its box stands.
struct CharacterSceneTransition {StableId id;TransitionDefinition definition;MotorVec center{};double yaw{};};
// Party travel that authority asked for on its latest tick: the trigger that raised it and where it leads. It names
// no player: the whole party travels. The owner of Play (the transition coordinator) carries it out.
struct SceneTransitionRequest {StableId source;AssetId scene;StableId entry;};
// Where the party enters a scene: the first member stands on the point, the others around it.
struct SceneArrival {MotorVec position{};double yaw{};};
// The session carries session_character_capacity characters; one of them is the player.
constexpr std::size_t character_scene_actor_limit=session_character_capacity-1;
struct CharacterSceneCombat {
    std::shared_ptr<const CombatKitDefinition> kit;InputProfile input;
    std::vector<AttributeDefinition> attributes;AttributeId health{},maximum_health{};
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips;
    std::uint32_t evaluator{};DamageEvaluator damage;
    std::shared_ptr<const TagDictionary> tags;
    CombatEvaluator combat_damage;
    std::map<std::uint32_t,EffectEvaluator> effect_evaluators;
    std::vector<std::shared_ptr<const EffectDefinition>> effects;
    std::string actor_generation;
    // Optional presentation-only death timeline from the frozen kit closure.
    std::shared_ptr<const ActionDefinition> death;
    // Optional equipped-mask loadouts (empty, or exactly four slots). abilities, effects
    // and clips above then hold the union so presentation stays prepared across switches.
    std::vector<std::optional<AbilityLoadout>> loadouts;unsigned active_loadout{};
    // Semantic input actions that select loadout slots 0-3 (zero = unbound).
    std::array<std::uint32_t,loadout_slot_count> loadout_actions{};
    // Server-driven actors, at most character_scene_actor_limit. Actors of different
    // factions are hostile to each other; the player's faction is below.
    std::vector<CharacterSceneActor> actors;std::string faction{"player"};
    // Encounters and trigger volumes. The actors above plus the most each encounter can have alive at once must fit
    // character_scene_actor_limit; an encounter's dead are removed after its corpse time, or sooner when a spawn needs the room.
    std::vector<CharacterSceneEncounter> encounters;std::vector<CharacterSceneTrigger> triggers;std::vector<CharacterSceneInteractable> interactables;
    // Scene transition triggers, at most 16, and the scene asset this content was prepared from: the scene half of the
    // persistent identity of its objects in the world ledger. A scene with no asset identity records nothing.
    std::vector<CharacterSceneTransition> transitions;AssetId scene;
    // Time slicing: per tick, at most this many controllers decide, this many actors sense and this many enemies enter.
    // The rest keep their latest decision (movement and facing) and perception until their turn, in ascending order
    // from where the previous tick stopped. An actor without a controller neither senses nor decides.
    unsigned decision_budget{8},sense_budget{8},spawn_budget{2};
};
// Local listen-host composition of the existing session, motor and graph.
// Resources are already validated/cooked. This adapter owns no transport codec.
// Optional combat uses the same serialized WorldSession path and frozen assets.
// Owner ability/action/cost prediction shares the native fixed-tick rules.
// Microseconds accumulated per phase of CharacterSceneSession::step over `ticks` fixed ticks. world: events, trigger volumes,
// encounters and spawning; sensing and decisions: the NPCs whose turn it was (decisions include their navigation queries);
// abilities: the authoritative ability advance and action motion; motors: authoritative motor steps and physics; hits: hit and
// projectile resolution; replication: publishing and both sessions' packet work; prediction: the owner's prediction and
// reconciliation; presentation: observers, effect views and poses.
struct CharacterSceneProfile {std::uint64_t ticks{};double world{},sensing{},decisions{},abilities{},motors{},hits{},replication{},prediction{},presentation{};};
// A party member beyond the scene's own player object: its identity and where it starts. Authority creates its
// character from the player object's data; every member uses the session's player definition.
struct ScenePlayer {StableId id;MotorVec position{};double yaw{};};
// How a session starts beyond its content. arrival: the whole party enters in formation around this point instead of
// at the player object and the companions' own places. ledger: the host's world ledger; authority restores the
// scene's persistent objects from it before the first tick and writes to it in the tick a persistent fact changes.
// It must outlive the session.
struct SceneStart {std::optional<SceneArrival> arrival;WorldLedger* ledger{};};
// The characters a scene's content needs places for beside the party: its actors and the most each encounter can have alive at once.
std::size_t scene_actor_demand(const CharacterSceneCombat&);
// The most players one scene session carries (WorldSession's peer limit).
constexpr std::size_t scene_party_limit=4;
}
