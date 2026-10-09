#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/tags.hpp>
#include <darkangel/effects.hpp>
#include <darkangel/character_motor.hpp>
#include <functional>
namespace darkangel {
struct MeleeTarget {std::uint64_t network{},epoch{};auto operator<=>(const MeleeTarget&)const=default;};
struct MeleeCandidates {std::vector<MeleeTarget> targets;bool overflow{};};
// Native query boundary. No client hit/damage commands or predicted-world queries.
class MeleeQuery {
public:
    virtual ~MeleeQuery()=default;
    virtual bool matches(std::uint64_t,const MotorState&)const=0;
    virtual MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&)const=0;
};
struct DamageContext {
    AbilityActivationHandle source;AbilityOwnerHandle target;
    std::uint64_t tick{};unsigned block{},loop{};std::uint32_t damage_type{};double power{};
    std::span<const AbilityAttributeValue> source_attributes,target_attributes;
    const OwnedTags* source_tags{};const OwnedTags* target_tags{};
};
// Game-owned pure native evaluator: immutable context, finite nonnegative damage.
// No world mutations, ambient random state or side effects during preparation.
using DamageEvaluator=std::function<double(const DamageContext&)>;
struct DamageEffectRequest {std::shared_ptr<const EffectDefinition> definition;double power{};};
struct CombatEvaluation {double damage{};std::vector<DamageEffectRequest> effects;};
using CombatEvaluator=std::function<CombatEvaluation(const DamageContext&)>;
struct DamageResult {
    AbilityActivationHandle source;AbilityOwnerHandle target;std::uint64_t tick{};
    unsigned block{},loop{};std::uint32_t damage_type{};double before{},after{},applied{};bool killed{};
    // applied is direct damage; after/killed include subsequent typed effects.
};
class PhysicsMeleeQuery final:public MeleeQuery {
public:
    explicit PhysicsMeleeQuery(const PhysicsWorld&);
    bool matches(std::uint64_t,const MotorState&)const override;
    MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&)const override;
private:const PhysicsWorld& world_;
};
}
