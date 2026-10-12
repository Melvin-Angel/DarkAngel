#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/actor_controller.hpp>
#include <darkangel/encounter.hpp>
#include <darkangel/interaction.hpp>
#include <darkangel/cue_player.hpp>
#include <functional>

namespace darkangel {
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
class CharacterSceneSession {
public:
    CharacterSceneSession(SessionHandshake,std::span<const ObjectData>,StableId player,
        const CollisionDefinition&,std::shared_ptr<const AnimationGraphPlan>,RigDefinition,std::string_view rig_archive,std::optional<CharacterSceneCombat> = {});
    ~CharacterSceneSession();
    CharacterSceneSession(const CharacterSceneSession&)=delete;
    unsigned advance(double seconds,CharacterSceneInput);
    void step(CharacterSceneInput);
    void remove_collision(std::uint64_t);
    const World& presentation() const;
    const MotorState& motor() const;
    const MotorState& predicted_motor() const;
    const GraphState& graph() const;
    const GraphPoseInputs& graph_inputs() const;
    const std::vector<JointMatrix>& pose() const;
    const std::vector<JointMatrix>* pose(StableId actor) const;
    const GraphPoseInputs* graph_inputs(StableId actor) const;
    std::size_t pending_prediction() const;
    unsigned resynchronizations() const;
    double debt() const;
    const AbilityOwnerSnapshot* ability()const;
    const AbilityOwnerSnapshot* predicted_ability()const;
    std::size_t pending_abilities()const;
    MotorVec prediction_visual_offset()const;
    ObserverAbilitySample observer(StableId,double render_tick)const;
    std::span<const AbilityCommitUpdate> commitments()const;
    const EffectPresentation* effects(StableId)const;
    std::span<const EffectCueUpdate> effect_cues()const;
    // Cue blocks the actions of the player and the actors crossed on the latest tick, and actions that ended before
    // their time. Derived on the client side from the action clocks it already holds (the owner's prediction and
    // correction, the other actors' public frames); no server timeline is read.
    std::span<const ActionCueUpdate> action_cues()const;
    std::span<const ActionCueCut> action_cue_cuts()const;
    // Steps the player and the actors took on the latest tick: the player from its predicted motor, the others from their public frames.
    std::span<const FootstepUpdate> footsteps()const;
    // A cue player to drive from every tick of this session: effect cue edges, action cues, cuts, actors leaving
    // and the tick itself. It must outlive the session or be detached with a null pointer first.
    void present_cues(CuePlayer*);
    // The scene identity behind a network identity the cue records name: the player or an actor.
    std::optional<StableId> identity(std::uint64_t network)const;
    // Current selected reaction for a prepared actor, including a suppressed candidate.
    const ReactionSample* reaction(StableId)const;
    // Death timeline tick while a prepared actor's checked Health is 0.
    std::optional<double> death(StableId)const;
    // Confirmed and predicted active loadout (equipped mask slot) of the player.
    unsigned loadout()const;unsigned predicted_loadout()const;
    // Soft-lock target the owner's predicted motor warped toward on the latest tick, if any.
    // Presentation only (HUD indicator); authority resolves its own warp.
    std::optional<StableId> soft_lock()const;
    // Owner-side aim intent from the camera. The ray is cast against the owner's predicted collision and the confirmed
    // public positions of the other actors, starting level with the owner so nothing between camera and owner counts.
    // muzzle is the shot's origin relative to the owner (right, up, forward). The result only shapes the owner's
    // input; authority sweeps its own world from its own state.
    AimSolution aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const;
    // Where the session's fixed ticks have spent their time since it was built. A measurement aid; nothing reads it for gameplay.
    const CharacterSceneProfile& profile()const;
    // Packets the server has sent this client so far, for budget measurements.
    std::uint64_t server_packets()const;
    // The same count split by wire kind number.
    std::vector<std::uint64_t> server_packet_kinds()const;
    // Projectiles in flight as the client last received them. Presentation only.
    const std::vector<ProjectileView>& projectiles()const;
    // Server-driven actors in simulation order (ascending network identity).
    std::span<const StableId> actors()const;
    // Authoritative receipt of an actor's latest controller press, if it has made one.
    const AbilityReceipt* controller_receipt(StableId)const;
    // An actor's controller's description of its latest decision; empty without a controller.
    std::string controller_status(StableId)const;
    // The actor an actor's controller named on its latest decision, if any.
    std::optional<StableId> controller_target(StableId)const;
    // An actor's senses and what it perceived on the latest tick, as authority holds them. For
    // debug views in the host process only; nothing here is replicated.
    const Perception* perception(StableId)const;
    // World events and tags, as authority holds them. Emitting queues the event for the next tick, like any other.
    void emit_world_event(std::string name);
    // Fault injection for tests and the editor's gate: authority refuses the player's next pressed ability (see
    // WorldSession::refuse_next_ability). The client predicts the press as usual and is then corrected.
    void refuse_next_activation();
    const WorldEvents& world_events()const;
    // Encounters in scene order, the state of one, and for an actor that an encounter spawned, which one.
    std::span<const StableId> encounters()const;
    std::optional<EncounterStatus> encounter(StableId)const;
    std::optional<StableId> actor_encounter(StableId actor)const;
    // Trigger volumes: how often one has fired, if it exists.
    std::optional<unsigned> trigger_fired(StableId)const;
    // Interaction. The prompt is the nearest interactable in reach of the owner's predicted position that this client has
    // not seen used up; the outcome is authority's answer to the owner's latest request. interactable_used is the
    // server's count of successful uses, for debug views.
    std::optional<InteractionPrompt> interaction_prompt()const;
    const InteractionOutcome* last_interaction()const;
    std::optional<unsigned> interactable_used(StableId)const;
    // Name and faction of any server-driven actor, spawned enemies included.
    const CharacterSceneActor* actor(StableId)const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
