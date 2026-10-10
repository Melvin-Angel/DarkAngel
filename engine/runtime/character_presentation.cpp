#include <darkangel/character_presentation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

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
ReactionPresentation::ReactionPresentation(const RigDefinition& rig,std::span<const std::shared_ptr<const EffectDefinition>> effects,const std::map<AssetId,std::shared_ptr<const AnimationClip>>& clips){
    for(const auto& effect:effects){
        require(bool(effect),"Missing prepared reaction effect");if(!effect->reaction)continue;const auto& action=*effect->reaction;
        require(reactions_.size()<8,"Reaction presentation budget");
        require(effect->lifetime!=EffectLifetime::Instant&&effect->visibility==AttributeVisibility::Public&&!action.upper_body&&!action.mask&&action.loops==1&&action.motion&&!action.motion->motor_root,"Reaction needs a public persistent effect and a one-shot full-body action without root ownership");
        for(const auto& block:action.blocks)require(block.kind==ActionBlockKind::Cue,"Reaction action cannot carry gameplay blocks");
        auto found=clips.find(action.motion->clip.id);require(found!=clips.end()&&found->second,"Reaction clip missing");const auto& clip=found->second->definition();
        require(clip.skeleton==rig.id&&clip.signature==rig.signature&&clip.joints==rig.joints.size()&&clip.ticks*action_tick_units==action.duration&&found->second->archive_generation()==action.motion->archive_generation,"Reaction clip rig/generation mismatch");
        ActionTimeline checked(effect->reaction,1,0);
        require(reactions_.emplace(effect->id,Prepared{effect->generation,std::make_shared<const ActionDefinition>(action),found->second}).second,"Duplicate prepared reaction effect");
    }
}
std::optional<ReactionSample> ReactionPresentation::select(const EffectFrame* frame,std::uint64_t network,std::uint64_t session_epoch,std::uint64_t avatar_epoch,std::uint64_t tick,bool alive,bool acting)const{
    if(!frame||reactions_.empty())return {};
    require(network&&session_epoch&&frame->owner.network==network&&frame->owner.session_epoch==session_epoch&&frame->audience==AttributeVisibility::Public&&frame->effects.size()<=64,"Reaction effect frame actor/audience mismatch");
    // A frame from another avatar lifecycle is never a reason to show a reaction.
    if(frame->avatar_epoch!=avatar_epoch)return {};
    std::optional<ReactionSample> best;
    for(const auto& effect:frame->effects){
        auto found=reactions_.find(effect.definition);if(found==reactions_.end()||effect.suppressed)continue;const auto& prepared=found->second;
        require(prepared.generation==effect.generation,"Reaction effect frozen generation unavailable");
        // Not started on this pose clock, already expired, or a finished one-shot.
        if(effect.start>tick||(effect.end&&tick>=effect.end))continue;auto elapsed=tick-effect.start;if(elapsed>=prepared.action->duration/action_tick_units)continue;
        if(!best||std::tuple(prepared.action->priority,effect.start,effect.handle)>std::tuple(best->action->priority,best->start,best->key.handle))
            best=ReactionSample{{session_epoch,network,avatar_epoch,effect.handle},effect.definition,prepared.action.get(),prepared.clip.get(),effect.start,double(elapsed),{}};
    }
    if(best)best->suppressed=!alive?ReactionSuppression::Dead:acting?ReactionSuppression::ActiveAction:ReactionSuppression::None;
    return best;
}
DeathPresentation::DeathPresentation(const RigDefinition& rig,std::shared_ptr<const ActionDefinition> action,const std::map<AssetId,std::shared_ptr<const AnimationClip>>& clips){
    require(action&&presentation_only_action(*action),"Death timeline must be a presentation-only one-shot");auto found=clips.find(action->motion->clip.id);require(found!=clips.end()&&found->second,"Death clip missing");const auto& clip=found->second->definition();
    require(clip.skeleton==rig.id&&clip.signature==rig.signature&&clip.joints==rig.joints.size()&&clip.ticks*action_tick_units==action->duration&&found->second->archive_generation()==action->motion->archive_generation,"Death clip rig/generation mismatch");
    ActionTimeline checked(action,1,0);action_=std::make_shared<const ActionDefinition>(*action);clip_=found->second;
}
std::optional<double> DeathPresentation::update(bool alive,std::uint64_t tick,std::uint64_t avatar_epoch){
    if(avatar_epoch!=epoch_){epoch_=avatar_epoch;seen_=dead_=false;onset_.reset();}
    if(alive){seen_=true;dead_=false;onset_.reset();return {};}
    // Only an observed transition has an onset; otherwise hold the final pose.
    if(!dead_){dead_=true;if(seen_)onset_=tick;}seen_=true;
    const double last=double(clip_->definition().ticks-1);return onset_&&tick>=*onset_?std::min(double(tick-*onset_),last):last;
}
ObservedCharacterPose::ObservedCharacterPose(std::shared_ptr<const AnimationGraphPlan> graph,RigDefinition rig,std::string_view archive,
    std::span<const std::shared_ptr<const ActionDefinition>> actions,
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips,std::shared_ptr<const TagDictionary> tags,std::span<const std::shared_ptr<const EffectDefinition>> effects,std::shared_ptr<const ActionDefinition> death)
    :graph_(graph),rig_(std::move(rig),archive),clips_(std::move(clips)){
    require(actions.size()<=36&&clips_.size()<=48,"Observed pose action/clip budget");
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
    mixer_=std::make_unique<ActionPoseMixer>(*graph,rig_.definition(),actions);reactions_=std::make_unique<ReactionPresentation>(rig_.definition(),effects,clips_);if(death)death_=std::make_unique<DeathPresentation>(rig_.definition(),std::move(death),clips_);matrices_=rig_.rest_pose();
}
void ObservedCharacterPose::sample(const AbilityPublicFrame& frame,const EffectFrame* effects){
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
    std::optional<ReactionSample> reaction;std::optional<double> death_tick;auto death_origin=death_?std::optional<DeathPresentation>(*death_):std::nullopt;
    try{
        bool alive=true;for(const auto& attribute:ability.attributes)if(attribute.id==ability.health_attribute)alive=attribute.value>0;
        // Locomotion keeps advancing underneath; completion resolves current state.
        reaction=reactions_->select(effects,frame.network,frame.session_epoch,motor.epoch,ability.tick,alive,action!=nullptr);
        // Terminal precedence: public Health 0 outranks any remaining action or reaction.
        if(death_)death_tick=death_->update(alive,ability.tick,motor.epoch);
        matrices_=death_tick?rig_.sample(death_->clip(),*death_tick):action?mixer_->sample(rig_,inputs,*action,*clips_.at(action->motion->clip.id),double(ability.action->clock%action->duration)/action_tick_units):reaction&&reaction->suppressed==ReactionSuppression::None?rig_.sample(*reaction->clip,reaction->tick):rig_.blend(inputs.span());}
    catch(...){graph_.restore(origin);if(death_origin)*death_=std::move(*death_origin);throw;}
    reaction_=reaction;death_tick_=death_tick;inputs_=inputs;network_=frame.network;session_epoch_=frame.session_epoch;motor_epoch_=motor.epoch;
}
}
