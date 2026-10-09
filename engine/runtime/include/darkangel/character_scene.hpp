#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/world_session.hpp>

namespace darkangel {
struct CharacterSceneInput {double x{},z{},yaw{};bool jump{},crouch{};std::vector<InputEvent> combat_events;};
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
    const std::vector<JointMatrix>& pose() const;
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
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
