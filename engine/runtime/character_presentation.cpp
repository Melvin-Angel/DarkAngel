#include <darkangel/character_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
ActionPoseMixer::ActionPoseMixer(const AnimationGraphPlan& graph,const RigDefinition& rig,std::span<const std::shared_ptr<const ActionDefinition>> actions){
    for(const auto& action:actions){
        require(action&&action->motion,"Action pose needs a frozen clip");
        if(!action->upper_body){require(!action->mask,"Full-body action cannot carry a joint mask");continue;}
        require(graph.active_layer_bound()<=3,"Upper-body action needs one reserved pose layer; locomotion bound exceeds three");
        require(action->mask&&!action->motion->motor_root,"Upper-body action needs a frozen mask and cannot own root motion");const auto& mask=*action->mask;
        require(mask.id!=AssetId{}&&mask.skeleton==rig.id&&mask.signature==rig.signature&&mask.weights.size()==rig.joints.size()&&mask.generation.size()==64&&mask.generation.find_first_not_of("0123456789abcdef")==mask.generation.npos,"Action pose mask rig/generation mismatch");
        Weights weights{action->generation,mask.weights,{}};weights.locomotion.reserve(mask.weights.size());bool selected=false;
        for(unsigned joint=0;joint<mask.weights.size();++joint){auto weight=mask.weights[joint];require(std::isfinite(weight)&&weight>=0&&weight<=1&&(rig.joints[joint].parent>=0||weight==0),"Upper-body mask weight/root contract");selected|=weight>0;weights.locomotion.push_back(1-weight);}
        require(selected,"Upper-body mask must select joints");auto [old,inserted]=weights_.emplace(action->id,std::move(weights));require(inserted||old->second.generation==action->generation,"Action pose conflicting frozen generations");
    }
}
const std::vector<JointMatrix>& ActionPoseMixer::sample(RigPose& rig,const GraphPoseInputs& locomotion,const ActionDefinition& action,const AnimationClip& clip,double tick)const{
    if(!action.upper_body)return rig.sample(clip,tick);
    auto found=weights_.find(action.id);require(found!=weights_.end()&&found->second.generation==action.generation,"Action pose unprepared mask generation");require(locomotion.count&&locomotion.count<=3,"Upper-body total pose layer budget");
    auto layers=locomotion;for(unsigned index=0;index<layers.count;++index)layers.layers[index].mask=found->second.locomotion;
    layers.layers[layers.count++]={&clip,tick,1,found->second.action};return rig.blend(layers.span());
}
ObservedCharacterPose::ObservedCharacterPose(std::shared_ptr<const AnimationGraphPlan> graph,RigDefinition rig,std::string_view archive,
    std::span<const std::shared_ptr<const ActionDefinition>> actions,
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips,std::shared_ptr<const TagDictionary> tags)
    :graph_(graph),rig_(std::move(rig),archive),clips_(std::move(clips)){
    require(actions.size()<=8&&clips_.size()<=8,"Observed pose action/clip budget");
    tags_=tags?std::make_shared<const TagDictionary>(*tags):std::make_shared<const TagDictionary>(std::vector<TagDefinition>{});
    if(auto dictionary=graph_.tag_dictionary())require(dictionary->registry()==tags_->registry()&&dictionary->generation()==tags_->generation(),"Observed graph/actor tag registry mismatch");
    ActorTagSnapshot empty{tags_->registry(),tags_->generation(),{}};GraphParameters parameters;parameters.tags=&empty;parameters.tag_audience=AttributeVisibility::Public;
    auto initial=graph_.evaluate(parameters);require(initial.count&&initial.layers[0].clip->definition().skeleton==rig_.definition().id&&initial.layers[0].clip->definition().signature==rig_.definition().signature&&initial.layers[0].clip->definition().joints==rig_.definition().joints.size(),"Observed graph/rig mismatch");
    for(const auto& action:actions){
        require(action&&action->motion,"Observed action needs a prepared clip");
        auto found=clips_.find(action->motion->clip.id);require(found!=clips_.end()&&found->second,"Observed action clip missing");const auto& clip=*found->second;const auto& definition=clip.definition();
        require(definition.skeleton==rig_.definition().id&&definition.signature==rig_.definition().signature&&definition.joints==rig_.definition().joints.size()&&definition.ticks*action_tick_units==action->duration&&clip.archive_generation()==action->motion->archive_generation,"Observed action clip rig/generation mismatch");
        ActionTimeline checked(action,1,0);
        auto [old,inserted]=actions_.emplace(action->id,std::make_shared<const ActionDefinition>(*action));require(inserted||old->second->generation==action->generation,"Conflicting observed action generations");
    }
    mixer_=std::make_unique<ActionPoseMixer>(*graph,rig_.definition(),actions);matrices_=rig_.rest_pose();
}
void ObservedCharacterPose::sample(const AbilityPublicFrame& frame){
    const auto& ability=frame.ability;const auto& motor=frame.motor;
    require(frame.network&&frame.session_epoch&&ability.owner.network==frame.network&&ability.owner.session_epoch==frame.session_epoch&&ability.tick==motor.tick&&bool(ability.active)==bool(ability.action),"Observed pose actor/clock identity");
    require(!network_||(frame.network==network_&&frame.session_epoch==session_epoch_&&motor.epoch>=motor_epoch_),"Observed pose actor/lifecycle changed; prepare a fresh pose");
    validate_motor_state(motor);
    const auto& snapshot=ability.tags;require(snapshot.registry==tags_->registry()&&snapshot.generation==tags_->generation()&&snapshot.values.size()<=128,"Observed pose tag registry generation/bound");
    std::array<std::uint64_t,2> seen{};auto definitions=tags_->definitions();
    for(auto id:snapshot.values){auto tag=std::lower_bound(definitions.begin(),definitions.end(),id,[](const auto& value,TagId key){return value.id<key;});require(tag!=definitions.end()&&tag->id==id&&tag->visibility==AttributeVisibility::Public,"Observed pose private/unknown tag");auto index=static_cast<unsigned>(tag-definitions.begin());auto bit=std::uint64_t{1}<<(index%64);require(!(seen[index/64]&bit),"Observed pose duplicate tag");seen[index/64]|=bit;}
    const ActionDefinition* action=nullptr;
    if(ability.active){
        auto found=actions_.find(ability.action_definition);require(found!=actions_.end(),"Observed pose action generation unavailable");action=found->second.get();
        require(ability.active->owner==ability.owner&&ability.active->activation==ability.action->activation&&ability.action->tick==ability.tick&&ability.action->phase==ActionPhase::Active&&ability.action->entered,"Observed pose active action identity");
        ActionTimeline checked(found->second,ability.active->activation,ability.tick);checked.restore(*ability.action);
    }
    auto origin=graph_.state();const bool baseline=!network_||motor.epoch!=motor_epoch_;
    require(baseline||motor.tick==origin.tick||motor.tick==origin.tick+1,"Observed pose missing fixed tick; prepare a fresh baseline");
    GraphParameters parameters;const double x=motor.achieved.x*60-motor.support_velocity.x,z=motor.achieved.z*60-motor.support_velocity.z;
    parameters.speed=static_cast<float>(std::hypot(x,z));parameters.forward=static_cast<float>(std::sin(motor.yaw)*x+std::cos(motor.yaw)*z);parameters.lateral=static_cast<float>(std::cos(motor.yaw)*x-std::sin(motor.yaw)*z);parameters.tags=&snapshot;parameters.tag_audience=AttributeVisibility::Public;
    auto inputs=graph_.evaluate(parameters);double stride{};for(const auto& layer:inputs.span()){const auto& clip=layer.clip->definition();stride+=layer.weight*std::hypot(clip.root.back()[0],clip.root.back()[2])*60/clip.ticks;}
    if(parameters.speed>.01&&stride>.01)parameters.playback_rate=static_cast<float>(std::clamp(parameters.speed/stride,0.,4.));
    if(baseline){auto state=origin;state.tick=motor.tick;state.phase=0;graph_.restore(state);inputs=graph_.evaluate(parameters);}
    else if(motor.tick!=origin.tick)inputs=graph_.advance(motor.tick,parameters);
    try{matrices_=action?mixer_->sample(rig_,inputs,*action,*clips_.at(action->motion->clip.id),double(ability.action->clock%action->duration)/action_tick_units):rig_.blend(inputs.span());}
    catch(...){graph_.restore(origin);throw;}
    inputs_=inputs;network_=frame.network;session_epoch_=frame.session_epoch;motor_epoch_=motor.epoch;
}
}
