#include "ability_state.hpp"
#include <algorithm>
#include <stdexcept>
namespace darkangel {
void AbilityState::configure_tags(std::shared_ptr<const TagDictionary> dictionary){if(tags_configured_||!effects_.snapshot().empty())throw std::invalid_argument("Ability tag dictionary already installed");effects_=OwnedEffects(std::move(dictionary),tick_);tags_configured_=true;++revision_;}
std::pair<EffectHandle,std::vector<EffectExecution>> AbilityState::apply_effect(const EffectDefinition& definition,EffectCredit credit,const EffectEvaluator& evaluator,std::vector<AbilityActionUpdate>& updates){
 if(health().current<=0)throw std::invalid_argument("Cannot apply effect to dead actor");auto result=effects_.apply(definition,std::move(credit),tick_,attributes_,evaluator);
 auto states=effects_.snapshot();auto applied=std::find_if(states.begin(),states.end(),[&](const auto& e){return e.handle==result.first;});
 if(health().current<=0){effects_.death(attributes_);updates=stop(AbilityActionReason::Death);}
 else if(definition.interrupt_action&&(applied!=states.end()?!applied->suppressed:effects_.tags().matches(definition.ongoing)))updates=stop(AbilityActionReason::Cancelled);
 result.first.owner=owner_;for(auto& execution:result.second)execution.handle.owner=owner_;++revision_;return result;
}
std::vector<EffectExecution> AbilityState::advance_effects(const std::function<EffectEvaluator(std::uint32_t)>& evaluator,std::vector<AbilityActionUpdate>& updates){auto result=effects_.advance(tick_,attributes_,evaluator,health_);if(health().current<=0){auto death=stop(AbilityActionReason::Death);updates.insert(updates.end(),death.begin(),death.end());}for(auto& execution:result)execution.handle.owner=owner_;return result;}
void AbilityState::remove_effect(EffectHandle handle){if(handle.owner!=owner_)throw std::invalid_argument("Wrong-domain effect handle");effects_.remove({handle.value},attributes_);++revision_;}
void AbilityState::cleanse_effects(const TagRequirement& tags){effects_.cleanse(tags,attributes_);++revision_;}
void AbilityState::source_destroyed(std::uint64_t session,std::uint64_t network){effects_.source_destroyed(session,network,attributes_);++revision_;}
}
