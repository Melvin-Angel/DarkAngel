#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/tags.hpp>
#include <darkangel/effects.hpp>
#include <darkangel/character_motor.hpp>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
namespace darkangel {
struct MeleeTarget {std::uint64_t network{},epoch{};auto operator<=>(const MeleeTarget&)const=default;};
struct MeleeCandidates {std::vector<MeleeTarget> targets;bool overflow{};};
// One swept step of a projectile: the first character it meets, or static collision met sooner, or neither.
struct ProjectileTrace {std::optional<MeleeTarget> target;bool blocked{};MotorVec point{};};
// World position a hit profile starts from for an actor in this state: its offset turned with the yaw.
inline MotorVec melee_origin(const MotorState& state,const AbilityMelee& profile){const auto& offset=profile.offset;const auto c=std::cos(state.yaw),s=std::sin(state.yaw);return {state.position.x+c*offset[0]+s*offset[2],state.position.y+offset[1],state.position.z-s*offset[0]+c*offset[2]};}
// Native query boundary. No client hit/damage commands or predicted-world queries.
class MeleeQuery {
public:
    virtual ~MeleeQuery()=default;
    // A query without a physics world never meets anything.
    virtual ProjectileTrace trace(MotorVec,MotorVec,double,std::uint64_t)const{return {};}
    virtual bool matches(std::uint64_t,const MotorState&)const=0;
    virtual MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&)const=0;
    virtual MeleeCandidates query(std::uint64_t source,const MotorState& state,const AbilityMelee& profile,AssetId,std::uint64_t)const{return query(source,state,profile);}
    virtual MotorVec origin(std::uint64_t,const MotorState& state,const AbilityMelee& profile,AssetId,std::uint64_t)const{return melee_origin(state,profile);}
};
struct DamageContext {
    AbilityActivationHandle source;AbilityOwnerHandle target;
    std::uint64_t tick{};unsigned block{},loop{};std::uint32_t damage_type{};double power{};
    std::span<const AbilityAttributeValue> source_attributes,target_attributes;
    const OwnedTags* source_tags{};const OwnedTags* target_tags{};
    AssetId ability; // Authoritative frozen definition; never a client hit field.
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
    PhysicsMeleeQuery(const PhysicsWorld&,RigDefinition,std::string_view,std::map<AssetId,std::shared_ptr<const AnimationClip>>);
    bool matches(std::uint64_t,const MotorState&)const override;
    MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&)const override;
    MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&,AssetId,std::uint64_t)const override;
    MotorVec origin(std::uint64_t,const MotorState&,const AbilityMelee&,AssetId,std::uint64_t)const override;
    ProjectileTrace trace(MotorVec from,MotorVec delta,double radius,std::uint64_t source)const override;
private:const PhysicsWorld& world_;std::map<AssetId,std::shared_ptr<const AnimationClip>> clips_;mutable std::unique_ptr<RigPose> rig_;std::optional<unsigned> socket_joint(const AbilityMelee&)const;MotorVec attached_origin(const MotorState&,const AbilityMelee&,AssetId,std::uint64_t)const;
};
}
