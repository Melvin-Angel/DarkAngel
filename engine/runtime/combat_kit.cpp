#include <darkangel/combat_kit.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <set>
#include <cmath>
#include <map>
#include <tuple>
namespace darkangel {
namespace {bool empty(const AssetId& id){return id==AssetId{};}}
void validate_combat_kit(const CombatKitDefinition& kit,const InputProfile& profile){
 validate_input_profile(profile);
 if(empty(kit.id)||empty(kit.locomotion_stance)||kit.generation.empty()||kit.generation.size()>128)throw std::invalid_argument("Combat kit requires identity, stance and bounded generation");
 if(kit.effects.size()>16)throw std::invalid_argument("Combat kit effect catalogue bound");std::set<AssetId> effects;for(auto id:kit.effects)if(empty(id)||id==kit.id||id==kit.locomotion_stance||!effects.insert(id).second)throw std::invalid_argument("Combat kit effect identity");
 for(unsigned i=0;i<combat_slot_count;++i){const auto& slot=kit.slots[i];
  if(static_cast<unsigned>(slot.slot)!=i)throw std::invalid_argument("Combat kit slots must have canonical order");
  auto action=std::find_if(profile.actions.begin(),profile.actions.end(),[&](const auto& a){return a.id==slot.input_action;});
  if(action==profile.actions.end()||action->kind!=InputActionKind::Button)throw std::invalid_argument("Combat slot requires a declared button action");
  // Distinct slots can share an ability but never ambiguously share an input action.
  for(unsigned j=0;j<i;++j)if(kit.slots[j].input_action==slot.input_action)throw std::invalid_argument("Duplicate combat input action");
 }
 if(kit.effect_bindings.size()>32)throw std::invalid_argument("Combat kit effect binding bound");std::set<std::tuple<AssetId,unsigned,AssetId>> bindings;std::map<std::pair<AssetId,unsigned>,unsigned> counts;
 for(const auto& binding:kit.effect_bindings)if(empty(binding.ability)||!binding.hit_block||!effects.contains(binding.effect)||!std::isfinite(binding.power)||binding.power<0||binding.power>1e9||std::none_of(kit.slots.begin(),kit.slots.end(),[&](const auto& slot){return slot.ability==binding.ability;})||!bindings.emplace(binding.ability,binding.hit_block,binding.effect).second||++counts[{binding.ability,binding.hit_block}]>8)throw std::invalid_argument("Combat kit effect binding reference/magnitude");
}
CombatKitInstance::CombatKitInstance(std::shared_ptr<const CombatKitDefinition> kit,const InputProfile& profile){
 if(!kit)throw std::invalid_argument("Missing combat kit");validate_combat_kit(*kit,profile);
 // Copy at the boundary so an external mutable alias cannot alter the frozen grant.
 definition_=std::make_shared<const CombatKitDefinition>(*kit);
}
std::vector<CombatKitInput> CombatKitInstance::route(std::uint64_t expected_grant_generation,std::span<const InputEvent> events){
 if(expected_grant_generation!=generation_)return {};
 if(events.size()>4096)throw std::invalid_argument("Combat input work limit");
 auto suppressed=suppressed_;std::vector<CombatKitInput> result;result.reserve(events.size());
 for(const auto& event:events){
  if(static_cast<unsigned>(event.edge)>static_cast<unsigned>(InputEdge::Tapped))throw std::invalid_argument("Invalid combat input edge");
  for(unsigned i=0;i<combat_slot_count;++i){const auto& slot=definition_->slots[i];if(slot.input_action!=event.action)continue;
   if(suppressed[i]){if(event.edge==InputEdge::Pressed&&!event.cancelled)suppressed[i]=false;else continue;}
   if(!empty(slot.ability))result.push_back({slot.ability,slot.slot,event,generation_});
  }
 }
 suppressed_=suppressed;return result;
}
CombatKitReplacement CombatKitInstance::replace(std::shared_ptr<const CombatKitDefinition> candidate,const InputProfile& profile){
 if(!candidate)throw std::invalid_argument("Missing combat kit");validate_combat_kit(*candidate,profile);
 if(generation_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Combat grant generation exhausted");
 auto frozen=std::make_shared<const CombatKitDefinition>(*candidate);CombatKitReplacement result;result.grant_generation=generation_+1;
 // All outgoing grants retire, including equal asset IDs. Active execution cleanup
 // belongs to the ability service; no cost refunds or effect removal are inferred.
 for(const auto& slot:definition_->slots)if(!empty(slot.ability)&&std::find(result.removed.begin(),result.removed.end(),slot.ability)==result.removed.end())result.removed.push_back(slot.ability);
 definition_=std::move(frozen);generation_=result.grant_generation;suppressed_.fill(true);return result;
}
}
