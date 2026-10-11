#include <darkangel/melee.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {
PhysicsMeleeQuery::PhysicsMeleeQuery(const PhysicsWorld& world):world_(world){if(world.mode()!=PhysicsWorld::Mode::Authoritative)throw std::runtime_error("Melee cannot use prediction/replay worlds");}
bool PhysicsMeleeQuery::matches(std::uint64_t id,const MotorState& state)const{
    if(!world_.gameplay_queries_ready()||world_.tick()!=state.tick||world_.topology()!=state.topology)return false;
    auto frame=world_.capture();for(const auto& actor:frame.actors)if(actor.id==id)return actor.epoch==state.epoch&&actor.foot==state.position&&actor.yaw==state.yaw&&actor.crouched==state.crouched;return false;
}
ProjectileTrace PhysicsMeleeQuery::trace(MotorVec from,MotorVec delta,double radius,std::uint64_t source)const{
    ProjectileTrace result;if(!world_.gameplay_queries_ready())throw std::runtime_error("Projectile trace outside the authoritative query phase");
    if(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z<1e-12)return result;
    double nearest=2;const auto actors=world_.sweep(from,delta,radius,16,{CharacterCollision,source});
    for(const auto& hit:actors.hits){if(!world_.valid_hit(hit)||!hit.character||hit.sensor)continue;if(hit.fraction<nearest||(hit.fraction==nearest&&result.target&&hit.identity<result.target->network)){nearest=hit.fraction;result.target=MeleeTarget{hit.identity,hit.epoch};result.point=hit.point;}}
    const auto walls=world_.query_ray(from,delta,{StaticCollision|KinematicCollision|DynamicCollision,source});
    for(const auto& hit:walls.hits)if(hit.fraction<nearest){nearest=hit.fraction;result.target.reset();result.blocked=true;result.point=hit.point;}
    return result;
}
MeleeCandidates PhysicsMeleeQuery::query(std::uint64_t source,const MotorState& state,const AbilityMelee& profile)const{
    if(!matches(source,state))throw std::runtime_error("Melee source physics pose mismatch");
    const auto& offset=profile.offset;auto c=std::cos(state.yaw),s=std::sin(state.yaw);
    MotorVec center{state.position.x+c*offset[0]+s*offset[2],state.position.y+offset[1],state.position.z-s*offset[0]+c*offset[2]};
    // A swept profile is a ranged shot from its offset along the facing and the aim elevation; only the first actor it meets is a candidate.
    const double level=std::cos(state.pitch)*profile.sweep;
    auto candidates=profile.sweep>0?world_.sweep(center,{s*level,std::sin(state.pitch)*profile.sweep,c*level},profile.radius,16,{CharacterCollision,source}):world_.overlap(center,profile.radius,16,{CharacterCollision,source});MeleeCandidates result;result.overflow=candidates.overflow;
    if(profile.sweep>0&&candidates.hits.size()>1){auto first=std::min_element(candidates.hits.begin(),candidates.hits.end(),[](const auto& a,const auto& b){return a.fraction<b.fraction||(a.fraction==b.fraction&&a.identity<b.identity);});candidates.hits={*first};}
    MotorVec origin{state.position.x,state.position.y+offset[1],state.position.z};
    for(const auto& hit:candidates.hits){
        if(!world_.valid_hit(hit)||!hit.character||hit.sensor)throw std::runtime_error("Invalid native melee candidate");
        MotorVec delta{hit.point.x-origin.x,hit.point.y-origin.y,hit.point.z-origin.z};
        auto blockers=world_.query_ray(origin,delta,{StaticCollision|KinematicCollision|DynamicCollision,source});
        if(blockers.overflow)result.overflow=true;if(blockers.hits.empty())result.targets.push_back({hit.identity,hit.epoch});
    }
    return result;
}
}
