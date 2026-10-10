#include <darkangel/character_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
ObservedCharacterPose::ObservedCharacterPose(std::shared_ptr<const AnimationGraphPlan> graph,RigDefinition rig,std::string_view archive,
    std::span<const std::shared_ptr<const ActionDefinition>> actions,
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips,std::shared_ptr<const TagDictionary> tags)
    :graph_(std::move(graph)),rig_(std::move(rig),archive),clips_(std::move(clips)){
    require(actions.size()<=8&&clips_.size()<=8,"Observed pose action/clip budget");
    tags_=tags?std::make_shared<const TagDictionary>(*tags):std::make_shared<const TagDictionary>(std::vector<TagDefinition>{});
    if(auto dictionary=graph_.tag_dictionary())require(dictionary->registry()==tags_->registry()&&dictionary->generation()==tags_->generation(),"Observed graph/actor tag registry mismatch");
    ActorTagSnapshot empty{tags_->registry(),tags_->generation(),{}};GraphParameters parameters;parameters.tags=&empty;parameters.tag_audience=AttributeVisibility::Public;
    auto initial=graph_.evaluate(parameters);require(initial.count&&initial.layers[0].clip->definition().skeleton==rig_.definition().id&&initial.layers[0].clip->definition().signature==rig_.definition().signature&&initial.layers[0].clip->definition().joints==rig_.definition().joints.size(),"Observed graph/rig mismatch");
    for(const auto& action:actions){
        require(action&&action->motion&&!action->upper_body,"Observed action needs a prepared full-body clip");
        auto found=clips_.find(action->motion->clip.id);require(found!=clips_.end()&&found->second,"Observed action clip missing");const auto& clip=*found->second;const auto& definition=clip.definition();
        require(definition.skeleton==rig_.definition().id&&definition.signature==rig_.definition().signature&&definition.joints==rig_.definition().joints.size()&&definition.ticks*action_tick_units==action->duration&&clip.archive_generation()==action->motion->archive_generation,"Observed action clip rig/generation mismatch");
        ActionTimeline checked(action,1,0);
        auto [old,inserted]=actions_.emplace(action->id,std::make_shared<const ActionDefinition>(*action));require(inserted||old->second->generation==action->generation,"Conflicting observed action generations");
    }
    matrices_=rig_.rest_pose();
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
    try{matrices_=action?rig_.sample(*clips_.at(action->motion->clip.id),double(ability.action->clock%action->duration)/action_tick_units):rig_.blend(inputs.span());}
    catch(...){graph_.restore(origin);throw;}
    inputs_=inputs;network_=frame.network;session_epoch_=frame.session_epoch;motor_epoch_=motor.epoch;
}
}
