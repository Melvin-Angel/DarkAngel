#include <darkangel/melee.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
PhysicsMeleeQuery::PhysicsMeleeQuery(const PhysicsWorld& world):world_(world){
    if(world.mode()!=PhysicsWorld::Mode::Authoritative)throw std::runtime_error("Melee cannot use prediction/replay worlds");
}
PhysicsMeleeQuery::PhysicsMeleeQuery(const PhysicsWorld& world,RigDefinition rig,std::string_view archive,std::map<AssetId,std::shared_ptr<const AnimationClip>> clips)
    :world_(world),clips_(std::move(clips)),rig_(std::make_unique<RigPose>(std::move(rig),archive)){
    if(world.mode()!=PhysicsWorld::Mode::Authoritative)throw std::runtime_error("Melee cannot use prediction/replay worlds");
}
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
std::optional<unsigned> PhysicsMeleeQuery::socket_joint(const AbilityMelee& profile)const{
    if(profile.socket.empty())return std::nullopt;
    if(!rig_)throw std::runtime_error("Socket melee query has no prepared canonical rig");
    auto key=profile.socket;if(const auto socket=rig_->definition().sockets.find(key);socket!=rig_->definition().sockets.end())key=socket->second;
    const auto found=std::find_if(rig_->definition().joints.begin(),rig_->definition().joints.end(),[&](const auto& joint){return joint.key==key;});
    if(found==rig_->definition().joints.end())throw std::runtime_error("Socket melee joint is absent from the prepared rig");
    return static_cast<unsigned>(found-rig_->definition().joints.begin());
}
MotorVec PhysicsMeleeQuery::attached_origin(const MotorState& state,const AbilityMelee& profile,AssetId action,std::uint64_t clock)const{
    if(profile.socket.empty())return melee_origin(state,profile);
    const auto joint=socket_joint(profile);const auto clip=clips_.find(action);
    if(!joint||clip==clips_.end()||!clip->second)throw std::runtime_error("Socket melee query has no frozen action clip");
    const auto& pose=rig_->sample(*clip->second,double(clock)/action_tick_units);
    if(*joint>=pose.size())throw std::runtime_error("Socket melee pose joint range");
    const auto& m=pose[*joint].values;const auto& o=profile.offset;
    const double x=m[12]+o[0],y=m[13]+o[1],z=m[14]+o[2];
    const double c=std::cos(state.yaw),s=std::sin(state.yaw);
    return {state.position.x+c*x+s*z,state.position.y+y,state.position.z-s*x+c*z};
}
MotorVec PhysicsMeleeQuery::origin(std::uint64_t,const MotorState& state,const AbilityMelee& profile,AssetId action,std::uint64_t clock)const{
    return attached_origin(state,profile,action,clock);
}
MeleeCandidates PhysicsMeleeQuery::query(std::uint64_t source,const MotorState& state,const AbilityMelee& profile)const{
    return query(source,state,profile,{},0);
}
MeleeCandidates PhysicsMeleeQuery::query(std::uint64_t source,const MotorState& state,const AbilityMelee& profile,AssetId action,std::uint64_t clock)const{
    if(!matches(source,state))throw std::runtime_error("Melee source physics pose mismatch");
    const auto center=attached_origin(state,profile,action,clock);const auto c=std::cos(state.yaw),s=std::sin(state.yaw);
    // A swept profile travels from the attached socket or motor-relative origin along the validated aim direction.
    const double level=std::cos(state.pitch)*profile.sweep;
    auto candidates=profile.sweep>0?world_.sweep(center,{s*level,std::sin(state.pitch)*profile.sweep,c*level},profile.radius,16,{CharacterCollision,source}):world_.overlap(center,profile.radius,16,{CharacterCollision,source});MeleeCandidates result;result.overflow=candidates.overflow;
    if(profile.sweep>0&&candidates.hits.size()>1){auto first=std::min_element(candidates.hits.begin(),candidates.hits.end(),[](const auto& a,const auto& b){return a.fraction<b.fraction||(a.fraction==b.fraction&&a.identity<b.identity);});candidates.hits={*first};}
    for(const auto& hit:candidates.hits){
        if(!world_.valid_hit(hit)||!hit.character||hit.sensor)throw std::runtime_error("Invalid native melee candidate");
        MotorVec delta{hit.point.x-center.x,hit.point.y-center.y,hit.point.z-center.z};auto blockers=world_.query_ray(center,delta,{StaticCollision|KinematicCollision|DynamicCollision,source});
        if(blockers.overflow)result.overflow=true;if(blockers.hits.empty())result.targets.push_back({hit.identity,hit.epoch});
    }
    return result;
}
}
