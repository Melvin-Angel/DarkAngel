#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/effect_replication.hpp>
#include <algorithm>
namespace darkangel::editor_app {
inline const char* ability_failure_text(AbilityFailure failure){
 switch(failure){
 case AbilityFailure::None:return "Accepted";case AbilityFailure::InvalidRequest:return "Invalid request";
 case AbilityFailure::StaleGrant:return "Stale grant generation";case AbilityFailure::Unassigned:return "Slot has no ability";
 case AbilityFailure::InputIgnored:return "Input edge or hold policy did not activate";case AbilityFailure::Dead:return "Actor is dead";
 case AbilityFailure::Busy:return "Another action prevents activation";case AbilityFailure::Cooldown:return "Cooldown is active";
 case AbilityFailure::Resources:return "Insufficient available resource";case AbilityFailure::OperationConflict:return "Operation identity conflicts";
 case AbilityFailure::StaleOperation:return "Operation retired";case AbilityFailure::HistoryFull:return "Operation history capacity reached";
 case AbilityFailure::CancelDenied:return "Current action cannot be cancelled";case AbilityFailure::InputExpired:return "Input expired";
 case AbilityFailure::AvatarMismatch:return "Avatar lifecycle changed";case AbilityFailure::TagRequirements:return "Gameplay tag requirements failed";
 }return "Unknown failure";
}
struct TagDebugSource {std::uint64_t effect{},source_network{},activation{};bool action{};unsigned block{};std::uint64_t session_epoch{};};
// Explain only contributors supported by the received state and frozen definitions.
// Aggregate snapshots intentionally cannot prove a complete server source ledger.
inline std::vector<TagDebugSource> tag_debug_sources(TagId tag,const TagDictionary& tags,const AbilityOwnerSnapshot* owner,const EffectFrame* frame,std::span<const std::shared_ptr<const AbilityDefinition>> abilities,std::span<const std::shared_ptr<const EffectDefinition>> definitions){
 std::vector<TagDebugSource> result;
 if(owner&&owner->active&&owner->action&&owner->action->phase==ActionPhase::Active){
  for(const auto& ability:abilities)if(ability->id==owner->ability&&ability->generation==owner->ability_generation&&ability->action->duration){
   auto clock=owner->action->clock%ability->action->duration;
   for(const auto& binding:ability->action_tags)if(tags.descends(binding.tag,tag))for(const auto& block:ability->action->blocks)if(block.id==binding.block&&clock>=block.begin&&clock<block.end){result.push_back({0,owner->owner.network,owner->active->activation,true,block.id,owner->owner.session_epoch});break;}
  }
 }
 if(frame)for(const auto& effect:frame->effects){
  if(effect.suppressed||(effect.end&&frame->tick>=effect.end))continue;
  for(const auto& definition:definitions)if(definition->id==effect.definition&&definition->generation==effect.generation&&std::any_of(definition->tags.begin(),definition->tags.end(),[&](auto granted){return tags.descends(granted,tag);})){result.push_back({effect.handle,effect.credit.source_network,effect.credit.activation,false,0,effect.credit.session_epoch});break;}
 }
 return result;
}
inline std::optional<double> debug_attribute(const AbilityOwnerSnapshot* state,AttributeId id){if(state)for(const auto& value:state->attributes)if(value.id==id)return value.value;return std::nullopt;}
}
