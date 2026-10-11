#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/actor_controller.hpp>

namespace darkangel {
struct ReactionSample;
struct CharacterSceneInput {double x{},z{},yaw{};bool jump{},crouch{};std::vector<InputEvent> combat_events;
    // Aim elevation in radians; see CharacterSceneSession::aim.
    double pitch{};};
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
};
// Local listen-host composition of the existing session, motor and graph.
// Resources are already validated/cooked. This adapter owns no transport codec.
// Optional combat uses the same serialized WorldSession path and frozen assets.
// Owner ability/action/cost prediction shares the native fixed-tick rules.
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
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
