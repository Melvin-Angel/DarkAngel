#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/input.hpp>
#include <darkangel/interaction.hpp>
#include <map>
#include <optional>
#include <vector>

namespace darkangel {
class ActorController;
class WorldLedger;
class Perception;
struct WorldStateView;
struct WorldRequest;
struct EncounterStatus;
struct CharacterSceneActor;
struct CharacterSceneCombat;
class AnimationClip;
class PartyInventory;
struct CollisionDefinition;
struct SceneInventoryDefinition;
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
// What the owner's HUD offers, and how authority answered the owner's latest request. Presentation only.
// hold: the prompt is used by holding Interact (a revive), not by one press.
struct InteractionPrompt {StableId id;std::string prompt;bool hold{};};
struct InteractionOutcome {StableId object;InteractionResult result{};std::uint64_t tick{};};
// Party travel that authority asked for on its latest tick: the trigger that raised it and where it leads. It names
// no player: the whole party travels. The owner of Play (the transition coordinator) carries it out.
// reset: ledger reset triggers (LedgerReset bits) the coordinator runs over every scene after capturing the source, as a
// rest or a party wipe does; save: the owner of Play saves once the party has arrived. A nil scene is the party's own
// start (the scene and entry Play began with): a wipe with no checkpoint respawns there.
struct SceneTransitionRequest {StableId source;AssetId scene;StableId entry;std::uint8_t reset{};bool save{};};
// Where the party enters a scene: the first member stands on the point, the others around it.
struct SceneArrival {MotorVec position{};double yaw{};};
// The session carries session_character_capacity characters; one of them is the player.
constexpr std::size_t character_scene_actor_limit=session_character_capacity-1;
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
struct SceneStart {std::optional<SceneArrival> arrival;WorldLedger* ledger{};PartyInventory* inventory{};std::map<StableId,StableId> profiles;};
// The characters a scene's content needs places for beside the party: its actors and the most each encounter can have alive at once.
std::size_t scene_actor_demand(const CharacterSceneCombat&);
// The most players one scene session carries (WorldSession's peer limit).
constexpr std::size_t scene_party_limit=4;
}
