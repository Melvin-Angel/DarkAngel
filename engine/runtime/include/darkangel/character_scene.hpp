#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/world_session.hpp>

namespace darkangel {
struct ReactionSample;
struct CharacterSceneInput {double x{},z{},yaw{};bool jump{},crouch{};std::vector<InputEvent> combat_events;};
// Scripted training attacker for the combat target: the target is equipped with the scene
// kit and presses one slot on a fixed schedule through the server's checked request path
// while the player is alive and within range. Shared target behaviour for exercising
// incoming hits; not an NPC controller or AI.
struct CharacterSceneAttacker {CombatSlot slot{CombatSlot::Light};std::uint64_t first_tick{60},interval{150};double range{2.5};bool face_player{true};};
struct CharacterSceneCombat {
    std::shared_ptr<const CombatKitDefinition> kit;InputProfile input;
    std::vector<AttributeDefinition> attributes;AttributeId health{},maximum_health{};
    std::vector<std::shared_ptr<const AbilityDefinition>> abilities;
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips;
    StableId target;std::uint32_t evaluator{};DamageEvaluator damage;
    std::shared_ptr<const TagDictionary> tags;
    CombatEvaluator combat_damage;
    std::map<std::uint32_t,EffectEvaluator> effect_evaluators;
    std::vector<std::shared_ptr<const EffectDefinition>> effects;
    std::vector<AttributeDefinition> target_attributes;std::string actor_generation;
    // Optional presentation-only death timeline from the frozen kit closure.
    std::shared_ptr<const ActionDefinition> death;
    // Optional equipped-mask loadouts (empty, or exactly four slots). abilities, effects
    // and clips above then hold the union so presentation stays prepared across switches.
    std::vector<std::optional<AbilityLoadout>> loadouts;unsigned active_loadout{};
    // Semantic input actions that select loadout slots 0-3 (zero = unbound).
    std::array<std::uint32_t,loadout_slot_count> loadout_actions{};
    std::optional<CharacterSceneAttacker> attacker;
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
    // Authoritative receipt of the scripted attacker's latest request, if it has made one.
    const AbilityReceipt* attacker_receipt()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
