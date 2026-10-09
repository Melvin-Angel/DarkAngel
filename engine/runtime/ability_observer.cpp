#include <darkangel/ability_observer.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}MotorVec blend(MotorVec a,MotorVec b,double t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}}
ObserverAbility::ObserverAbility(std::vector<AttributeDefinition> schema,AttributeId health,AttributeId maximum,std::span<const std::shared_ptr<const ActionDefinition>> actions,std::shared_ptr<const TagDictionary> tags)
    :schema_(std::move(schema)),health_(health),maximum_(maximum),tags_(tags?std::make_shared<const TagDictionary>(*tags):std::make_shared<const TagDictionary>(std::vector<TagDefinition>{})){
    AttributeSet checked(schema_);require(health_&&maximum_&&health_!=maximum_&&actions.size()<=32,"Observer preparation bounds");checked.value(health_);checked.value(maximum_);
    for(const auto& action:actions){require(bool(action),"Observer action resource missing");ActionTimeline prepared(action,1,0);prepared.enter();auto frozen=std::make_shared<const ActionDefinition>(prepared.definition());auto [entry,inserted]=actions_.emplace(frozen->id,frozen);require(inserted||entry->second->generation==frozen->generation,"Observer conflicting action generation");}
}
void ObserverAbility::push(const AbilityPublicFrame& frame){
    const auto& state=frame.ability;validate_motor_state(frame.motor);require(frame.network&&frame.session_epoch&&state.owner.network==frame.network&&state.owner.session_epoch==frame.session_epoch&&state.tick==frame.motor.tick&&state.health_attribute==health_&&state.maximum_health_attribute==maximum_&&bool(state.active)==bool(state.action),"Observer frame identity/clock/schema");
    tags_->validate_snapshot(state.tags,AttributeVisibility::Public);
    std::set<AttributeId> ids;require(state.attributes.size()<=64,"Observer public attribute bound");double health{},maximum{};
    for(auto value:state.attributes){auto definition=std::find_if(schema_.begin(),schema_.end(),[&](const auto& d){return d.id==value.id;});require(definition!=schema_.end()&&(definition->visibility==AttributeVisibility::Public||value.id==health_||value.id==maximum_)&&ids.insert(value.id).second&&std::isfinite(value.value)&&value.value>=definition->minimum&&value.value<=definition->maximum,"Observer private/unknown/invalid attribute");if(value.id==health_)health=value.value;if(value.id==maximum_)maximum=value.value;}
    require(ids.contains(health_)&&ids.contains(maximum_)&&health>=0&&health<=maximum,"Observer Health bounds");
    if(state.active){auto definition=actions_.find(state.action_definition);require(definition!=actions_.end()&&state.active->owner==state.owner&&state.active->activation==state.action->activation&&state.action->tick==state.tick&&state.action->phase==ActionPhase::Active&&state.action->entered,"Observer action identity/preparation");ActionTimeline prepared(definition->second,state.active->activation,state.tick);prepared.restore(*state.action);}
    auto candidate=frames_;if(!candidate.empty()){const auto& previous=candidate.back();require(frame.network==previous.network&&frame.session_epoch==previous.session_epoch,"Observer actor/session changed without fresh buffer");if(frame.world_revision<previous.world_revision||frame.motor.epoch<previous.motor.epoch)return;
        if(frame.world_revision!=previous.world_revision||frame.motor.epoch!=previous.motor.epoch||frame.motor.topology!=previous.motor.topology)candidate.clear();else if(state.tick<previous.ability.tick||(state.tick==previous.ability.tick&&state.revision<=previous.ability.revision))return;
        else if(state.tick==previous.ability.tick)candidate.pop_back();
    }
    candidate.push_back(frame);while(candidate.size()>8)candidate.pop_front();frames_=std::move(candidate);
}
double ObserverAbility::limit(const AbilityPublicFrame& frame)const{const auto& action=*actions_.at(frame.ability.action_definition);return double(action.duration)*action.loops-1.;}
ObserverAbilitySample ObserverAbility::sample(double tick)const{
    require(std::isfinite(tick)&&tick>=0&&!frames_.empty(),"Observer render clock/state");ObserverAbilitySample result;result.render_tick=tick;result.frame=frames_.front();if(result.frame.ability.action)result.action_clock=double(result.frame.ability.action->clock);
    if(tick<double(frames_.front().ability.tick)){result.held=true;return result;}
    for(std::size_t i=1;i<frames_.size();++i)if(tick<double(frames_[i].ability.tick)){const auto& a=frames_[i-1];const auto& b=frames_[i];auto t=std::clamp((tick-double(a.ability.tick))/double(b.ability.tick-a.ability.tick),0.,1.);result.frame=a;result.frame.motor.position=blend(a.motor.position,b.motor.position,t);result.frame.motor.yaw=a.motor.yaw+std::remainder(b.motor.yaw-a.motor.yaw,6.283185307179586)*t;
        if(a.ability.action){result.action_clock=double(a.ability.action->clock);if(a.ability.active==b.ability.active&&a.ability.action_definition==b.ability.action_definition&&b.ability.action&&a.ability.action->generation==b.ability.action->generation)result.action_clock+=(double(b.ability.action->clock)-result.action_clock)*t;else result.action_clock=std::min(limit(a),result.action_clock+(tick-double(a.ability.tick))*a.ability.action->rate);}else result.action_clock=0;return result;
    }
    result.frame=frames_.back();auto elapsed=tick-double(result.frame.ability.tick);auto lead=std::clamp(elapsed,0.,2.);result.extrapolated=lead>0;result.held=elapsed>2;
    const auto& achieved=result.frame.motor.achieved;result.frame.motor.position.x+=achieved.x*lead;result.frame.motor.position.y+=achieved.y*lead;result.frame.motor.position.z+=achieved.z*lead;
    if(result.frame.ability.action)result.action_clock=std::min(limit(result.frame),double(result.frame.ability.action->clock)+lead*result.frame.ability.action->rate);return result;
}
}
