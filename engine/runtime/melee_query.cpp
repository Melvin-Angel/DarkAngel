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
MeleeCandidates PhysicsMeleeQuery::query(std::uint64_t source,const MotorState& state,const AbilityMelee& profile)const{
    if(!matches(source,state))throw std::runtime_error("Melee source physics pose mismatch");
    const auto& offset=profile.offset;auto c=std::cos(state.yaw),s=std::sin(state.yaw);
    MotorVec center{state.position.x+c*offset[0]+s*offset[2],state.position.y+offset[1],state.position.z-s*offset[0]+c*offset[2]};
    // A swept profile is a ranged shot along the facing; only the first actor it meets is a candidate.
    auto candidates=profile.sweep>0?world_.sweep(center,{s*profile.sweep,0,c*profile.sweep},profile.radius,16,{CharacterCollision,source}):world_.overlap(center,profile.radius,16,{CharacterCollision,source});MeleeCandidates result;result.overflow=candidates.overflow;
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
