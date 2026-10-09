#include <darkangel/effects.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
namespace darkangel {
namespace {
constexpr std::uint64_t action_tag_token=UINT64_MAX-16;
void checked(bool b,const char* message){if(!b)throw std::invalid_argument(message);}
std::vector<AbilityAttributeValue> values(const AttributeSet& attributes){std::vector<AbilityAttributeValue> result;for(const auto& d:attributes.definitions())result.push_back({d.id,attributes.value(d.id)});return result;}
std::shared_ptr<const EffectDefinition> freeze(const EffectDefinition& d,const AttributeSet& attributes,const TagDictionary& dictionary){
 checked(d.id!=AssetId{}&&d.generation.size()==64&&d.generation.find_first_not_of("0123456789abcdef")==std::string::npos,"Effect identity/generation");
 checked(static_cast<unsigned>(d.lifetime)<=2&&static_cast<unsigned>(d.stacking)<=1&&static_cast<unsigned>(d.ongoing_policy)<=1&&d.duration_ticks<=36000&&d.period_ticks<=36000&&d.modifiers.size()<=16&&d.tags.size()<=16,"Effect policy/work bounds");
 checked(static_cast<unsigned>(d.visibility)<=2,"Effect replication visibility");
 checked(d.cues.size()<=4&&(d.lifetime!=EffectLifetime::Instant||d.cues.empty()),"Persistent effect cue lifetime/bound");std::set<AssetId> cues;for(const auto& cue:d.cues)checked(cue.id!=AssetId{}&&cue.id!=d.id&&cues.insert(cue.id).second&&!cue.key.empty()&&cue.key.size()<=64&&cue.key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==cue.key.npos,"Effect cue identity/key");
 checked((d.lifetime==EffectLifetime::Finite)==(d.duration_ticks>0),"Effect finite duration");
 checked(d.lifetime!=EffectLifetime::Instant||(!d.period_ticks&&d.modifiers.empty()&&d.tags.empty()&&d.execute_on_apply&&d.stacking==EffectStack::Independent),"Instant effect policy");
 checked((d.period_ticks||d.execute_on_apply)==(d.evaluator!=0),"Effect execution evaluator policy");
 dictionary.validate(d.application);dictionary.validate(d.ongoing);std::set<TagId> ids;for(auto tag:d.tags)checked(dictionary.contains(tag)&&ids.insert(tag).second,"Effect granted tag identity");
 AttributeSet validation(std::vector<AttributeDefinition>(attributes.definitions().begin(),attributes.definitions().end()));auto modifiers=d.modifiers;std::uint64_t sequence=1;for(auto& m:modifiers){checked(!m.owner&&!m.sequence,"Effect modifier ownership is runtime-assigned");m.owner=std::numeric_limits<std::uint64_t>::max();m.sequence=sequence++;}validation.add(modifiers);
 return std::make_shared<const EffectDefinition>(d);
}
void credit_valid(const EffectCredit& c){
 checked(c.session_epoch&&c.source_network&&std::isfinite(c.power)&&c.power>=0&&c.power<=1e9&&c.source_attributes.size()<=64,"Effect captured credit bounds");std::set<AttributeId> ids;for(const auto& a:c.source_attributes)checked(a.id&&std::isfinite(a.value)&&ids.insert(a.id).second,"Effect captured attribute identity");
}
}
OwnedEffects::OwnedEffects(std::shared_ptr<const TagDictionary> dictionary,std::uint64_t tick):tags_(std::move(dictionary)),tick_(tick){}
void OwnedEffects::set_action_tags(std::span<const TagId> tags,AttributeSet& attributes){
 if(std::equal(tags.begin(),tags.end(),action_tags_.begin(),action_tags_.end()))return;
 auto candidate=*this;auto prepared=attributes;candidate.tags_.remove(action_tag_token);
 if(!tags.empty())candidate.tags_.add(action_tag_token,tags);
 candidate.action_tags_={tags.begin(),tags.end()};
 candidate.contributions(prepared);*this=std::move(candidate);attributes=std::move(prepared);
}
std::vector<TagId> OwnedEffects::external_tags(AttributeVisibility audience)const{return tags_.values(audience,action_tag_token);}
std::shared_ptr<const EffectDefinition> freeze_effect_definition(const EffectDefinition& source,const AttributeSet& attributes,const TagDictionary& tags){return freeze(source,attributes,tags);}
void OwnedEffects::erase(EffectHandle handle,AttributeSet&){tags_.remove(handle.value);std::erase_if(active_,[&](const auto& a){return a.state.handle==handle;});}
void OwnedEffects::contributions(AttributeSet& attributes){
 // Evaluate all policies against the same current tag view, excluding each
 // effect's own contribution. No order-dependent recursive reevaluation.
 std::vector<bool> enabled;for(const auto& a:active_)enabled.push_back(tags_.matches(a.definition->ongoing,a.state.handle.value));
 for(const auto& a:active_)tags_.remove(a.state.handle.value);
 std::vector<AttributeModifier> additions;
 std::vector<EffectHandle> removed;
 for(std::size_t i=0;i<active_.size();++i){auto& a=active_[i];a.state.suppressed=!enabled[i];if(!enabled[i]){if(a.definition->ongoing_policy==EffectOngoingPolicy::Remove)removed.push_back(a.state.handle);continue;}tags_.add(a.state.handle.value,a.definition->tags);
  additions.insert(additions.end(),a.modifiers.begin(),a.modifiers.end());}
 attributes.replace_owned(modifier_owners_,additions);for(auto handle:removed)erase(handle,attributes);modifier_owners_.clear();for(const auto& a:active_)modifier_owners_.push_back(a.state.handle.value|(std::uint64_t{1}<<63));
}
EffectExecution OwnedEffects::execute(const Active& a,std::uint64_t tick,AttributeSet& attributes,const EffectEvaluator& evaluator)const{
 checked(bool(evaluator),"Missing native effect evaluator");auto target=values(attributes);auto deltas=evaluator({a.state.credit,tick,target,tags_});checked(deltas.size()<=16,"Effect execution work bound");
 std::set<AttributeId> ids;for(const auto& d:deltas)checked(d.attribute&&std::isfinite(d.delta)&&std::abs(d.delta)<=1e9&&ids.insert(d.attribute).second,"Effect evaluator result bounds");
 std::vector<ResourceDelta> applied;for(const auto& d:deltas)applied.push_back({d.attribute,attributes.value(d.attribute)});attributes.transact(deltas,false);for(auto& d:applied)d.delta=attributes.value(d.attribute)-d.delta;
 return {a.state.handle,a.state.credit,tick,std::move(applied)};
}
std::pair<EffectHandle,std::vector<EffectExecution>> OwnedEffects::apply(const EffectDefinition& source,EffectCredit credit,std::uint64_t tick,AttributeSet& attributes,const EffectEvaluator& evaluator){
 auto definition=freeze(source,attributes,tags_.dictionary());credit_valid(credit);checked(tick==tick_&&definition->duration_ticks<=std::numeric_limits<std::uint64_t>::max()-tick&&definition->period_ticks<=std::numeric_limits<std::uint64_t>::max()-tick,"Effect application clock");
 checked(tags_.matches(definition->application),"Effect application tag requirement");auto candidate=*this;auto prepared=attributes;candidate.tick_=tick;
 auto refreshed=std::find_if(candidate.active_.begin(),candidate.active_.end(),[&](const auto& a){return definition->stacking==EffectStack::RefreshPerSource&&a.definition->id==definition->id&&a.state.credit.session_epoch==credit.session_epoch&&a.state.credit.source_network==credit.source_network;});
 Active active;
 if(refreshed!=candidate.active_.end()){
  checked(refreshed->definition->generation==definition->generation,"Effect refresh generation mismatch");active=*refreshed;active.state.credit=std::move(credit);active.state.end=definition->lifetime==EffectLifetime::Finite?tick+definition->duration_ticks:0;*refreshed=active;
 }else{
  checked(candidate.next_<(std::uint64_t{1}<<63)&&(definition->lifetime==EffectLifetime::Instant||candidate.active_.size()<64),"Effect capacity/identity exhausted");
  active={{EffectHandle{candidate.next_++},definition->id,definition->generation,std::move(credit),tick,definition->lifetime==EffectLifetime::Finite?tick+definition->duration_ticks:0,definition->period_ticks?tick+definition->period_ticks:0,false},definition};
  for(auto m:definition->modifiers){checked(candidate.modifier_sequence_!=std::numeric_limits<std::uint64_t>::max(),"Effect modifier sequence exhausted");m.owner=active.state.handle.value|(std::uint64_t{1}<<63);m.sequence=candidate.modifier_sequence_++;active.modifiers.push_back(std::move(m));}
  if(definition->lifetime!=EffectLifetime::Instant)candidate.active_.push_back(active);
 }
 candidate.contributions(prepared);std::vector<EffectExecution> result;
 auto current=std::find_if(candidate.active_.begin(),candidate.active_.end(),[&](const auto& a){return a.state.handle==active.state.handle;});if(current!=candidate.active_.end())active=*current;
 else active.state.suppressed=definition->lifetime!=EffectLifetime::Instant||!candidate.tags_.matches(definition->ongoing);
 if(definition->execute_on_apply&&!active.state.suppressed)result.push_back(candidate.execute(active,tick,prepared,evaluator));
 *this=std::move(candidate);attributes=std::move(prepared);return {active.state.handle,std::move(result)};
}
std::vector<EffectExecution> OwnedEffects::advance(std::uint64_t tick,AttributeSet& attributes,const std::function<EffectEvaluator(std::uint32_t)>& evaluator,AttributeId health){
 checked(tick_!=std::numeric_limits<std::uint64_t>::max()&&tick==tick_+1,"Effect fixed simulation tick");auto candidate=*this;auto prepared=attributes;candidate.tick_=tick;
 std::vector<EffectHandle> expired;for(const auto& a:candidate.active_)if(a.state.end&&tick>=a.state.end)expired.push_back(a.state.handle);for(auto handle:expired)candidate.erase(handle,prepared);
 candidate.contributions(prepared);std::vector<EffectExecution> result;
 for(auto& a:candidate.active_)if(a.state.next_period&&tick==a.state.next_period){
  checked(a.definition->period_ticks<=std::numeric_limits<std::uint64_t>::max()-tick,"Effect period clock exhausted");a.state.next_period=tick+a.definition->period_ticks;
  if(!a.state.suppressed&&(!health||prepared.value(health)>0))result.push_back(candidate.execute(a,tick,prepared,evaluator(a.definition->evaluator)));
 }
 if(health&&prepared.value(health)<=0)candidate.death(prepared);
 *this=std::move(candidate);attributes=std::move(prepared);return result;
}
void OwnedEffects::remove(EffectHandle handle,AttributeSet& attributes){checked(handle.value&&handle.value<next_,"Invalid effect handle");auto candidate=*this;auto prepared=attributes;candidate.erase(handle,prepared);candidate.contributions(prepared);*this=std::move(candidate);attributes=std::move(prepared);}
void OwnedEffects::cleanse(const TagRequirement& requirement,AttributeSet& attributes){
 tags_.dictionary().validate(requirement);auto candidate=*this;auto prepared=attributes;std::vector<EffectHandle> removed;
 for(const auto& a:active_){OwnedTags own(std::make_shared<const TagDictionary>(tags_.dictionary()));own.add(a.state.handle.value,a.definition->tags);if(own.matches(requirement))removed.push_back(a.state.handle);}for(auto handle:removed)candidate.erase(handle,prepared);candidate.contributions(prepared);*this=std::move(candidate);attributes=std::move(prepared);
}
void OwnedEffects::death(AttributeSet& attributes){auto candidate=*this;auto prepared=attributes;std::vector<EffectHandle> removed;for(const auto& a:active_)if(a.definition->remove_on_death)removed.push_back(a.state.handle);for(auto handle:removed)candidate.erase(handle,prepared);candidate.contributions(prepared);*this=std::move(candidate);attributes=std::move(prepared);}
void OwnedEffects::source_destroyed(std::uint64_t session,std::uint64_t network,AttributeSet& attributes){auto candidate=*this;auto prepared=attributes;std::vector<EffectHandle> removed;for(const auto& a:active_)if(a.definition->remove_with_source&&a.state.credit.session_epoch==session&&a.state.credit.source_network==network)removed.push_back(a.state.handle);for(auto handle:removed)candidate.erase(handle,prepared);candidate.contributions(prepared);*this=std::move(candidate);attributes=std::move(prepared);}
std::vector<EffectSnapshot> OwnedEffects::snapshot()const{std::vector<EffectSnapshot> result;for(const auto& a:active_)result.push_back(a.state);return result;}
std::vector<EffectSnapshot> OwnedEffects::snapshot(AttributeVisibility audience)const{
 checked(audience==AttributeVisibility::Owner||audience==AttributeVisibility::Public,"Effect presentation audience");std::vector<EffectSnapshot> result;
 for(const auto& active:active_)if(active.definition->visibility==AttributeVisibility::Public||(audience==AttributeVisibility::Owner&&active.definition->visibility==AttributeVisibility::Owner)){const auto& s=active.state;result.push_back({s.handle,s.definition,s.generation,{s.credit.session_epoch,s.credit.source_network,s.credit.activation,0,{}},s.start,s.end,s.next_period,s.suppressed});}return result;
}
}
