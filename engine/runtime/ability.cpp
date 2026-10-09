#include "ability_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace darkangel {
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
bool same(const AbilityRequest& a,const AbilityRequest& b){
    return a.owner==b.owner&&a.operation==b.operation&&a.grant_generation==b.grant_generation&&a.slot==b.slot&&a.replace_active==b.replace_active&&
        std::tie(a.input.action,a.input.edge,a.input.time_us,a.input.held_us,a.input.value,a.input.cancelled)==
        std::tie(b.input.action,b.input.edge,b.input.time_us,b.input.held_us,b.input.value,b.input.cancelled);
}
std::shared_ptr<const AbilityDefinition> freeze(const AbilityDefinition& source,const AttributeSet& attributes){
    require(source.id!=AssetId{}&&source.generation.size()==64&&source.generation.find_first_not_of("0123456789abcdef")==std::string::npos,"Ability identity/generation");
    require(source.action&&source.action->id!=AssetId{}&&source.costs.size()<=8&&source.cooldown_ticks<=36000&&
        (source.cooldown_ticks==0||source.cooldown_group!=0)&&static_cast<unsigned>(source.activate_on)<=static_cast<unsigned>(InputEdge::Tapped)&&
        source.minimum_held_us<=10000000&&!(source.cancel_on_release&&source.activate_on==InputEdge::Released),"Ability definition bounds/policy");
    require(source.melee.size()<=8,"Ability melee profile count");std::set<unsigned> hit_blocks;
    for(const auto& hit:source.melee){
        auto block=std::find_if(source.action->blocks.begin(),source.action->blocks.end(),[&](const auto& b){return b.id==hit.block&&b.kind==ActionBlockKind::HitWindow;});
        require(block!=source.action->blocks.end()&&hit_blocks.insert(hit.block).second&&hit.evaluator&&hit.damage_type&&std::isfinite(hit.power)&&hit.power>=0&&hit.power<=1e9&&std::isfinite(hit.radius)&&hit.radius>0&&hit.radius<=2,"Ability melee profile identity/bounds");
        for(auto value:hit.offset)require(std::isfinite(value)&&std::abs(value)<=3,"Ability melee offset bounds");
    }
    auto frozen=std::make_shared<AbilityDefinition>(source);
    frozen->action=std::make_shared<const ActionDefinition>(*source.action);
    // Validate action preparation now; active executions pin this complete generation.
    ActionTimeline prepared(frozen->action,1,0);prepared.enter();
    std::vector<ResourceDelta> zero;
    std::map<AttributeId,double> total;
    for(const auto& cost:source.costs){require(cost.attribute&&std::isfinite(cost.amount)&&cost.amount>=0,"Ability cost magnitude");total[cost.attribute]+=cost.amount;require(std::isfinite(total[cost.attribute]),"Ability aggregate cost overflow");zero.push_back({cost.attribute,0});}
    auto checked=attributes;checked.transact(zero,false); // unknown/statistic costs fail at installation
    return frozen;
}
std::vector<ResourceDelta> costs(const AbilityDefinition& definition){
    std::vector<ResourceDelta> result;for(const auto& cost:definition.costs)result.push_back({cost.attribute,-cost.amount});return result;
}
}
std::shared_ptr<const AbilityDefinition> freeze_ability_definition(const AbilityDefinition& source,const AttributeSet& attributes){return freeze(source,attributes);}
AbilityState::AbilityState(AbilityOwnerHandle owner,std::uint64_t tick,std::vector<AttributeDefinition> definitions,AttributeId health,AttributeId maximum_health):
    owner_(owner),tick_(tick),attributes_(std::move(definitions)),health_(health),maximum_health_(maximum_health){
    require(health_&&maximum_health_&&health_!=maximum_health_,"Ability health schema IDs");
    const auto schema=attributes_.definitions();
    auto find=[&](AttributeId id)->const AttributeDefinition&{auto it=std::find_if(schema.begin(),schema.end(),[&](const auto& d){return d.id==id;});require(it!=schema.end(),"Ability health schema missing");return *it;};
    const auto& h=find(health_);const auto& maximum=find(maximum_health_);
    require(h.kind==AttributeKind::Resource&&h.maximum_attribute==maximum_health_&&h.minimum==0&&maximum.kind==AttributeKind::Statistic&&maximum.minimum>=0,"Ability health schema relationship");
}
Health AbilityState::health()const{return {attributes_.value(maximum_health_),attributes_.value(health_)};}
std::vector<AbilityActionUpdate> AbilityState::stop(AbilityActionReason reason){
    pending_hits.clear();
    if(!active_)return {};
    AbilityActionUpdate update{{owner_,active_->timeline.state().activation},ActionPhase::Cancelled,reason,tick_,active_->timeline.cancel()};
    active_.reset();return {std::move(update)};
}
std::vector<AbilityActionUpdate> AbilityState::equip(std::shared_ptr<const CombatKitDefinition> definition,const InputProfile& input,std::span<const std::shared_ptr<const AbilityDefinition>> definitions){
    require(definition&&definitions.size()<=combat_slot_count,"Ability kit catalogue bound");
    validate_combat_kit(*definition,input);
    std::map<AssetId,std::shared_ptr<const AbilityDefinition>> prepared;
    for(const auto& ability:definitions){require(bool(ability),"Missing ability definition");auto frozen=freeze_ability_definition(*ability,attributes_);require(prepared.emplace(frozen->id,std::move(frozen)).second,"Duplicate ability definition");}
    std::array<std::shared_ptr<const AbilityDefinition>,combat_slot_count> grants;
    for(unsigned i=0;i<combat_slot_count;++i){const auto& slot=definition->slots[i];if(slot.ability!=AssetId{}){auto it=prepared.find(slot.ability);require(it!=prepared.end(),"Missing assigned ability");grants[i]=it->second;}}
    std::optional<CombatKitInstance> candidate=kit_;
    const bool replacing=bool(candidate);
    if(candidate)candidate->replace(definition,input);else candidate.emplace(definition,input);
    auto outgoing=stop(AbilityActionReason::GrantRemoved);
    kit_=std::move(candidate);grants_=std::move(grants);rearm_.fill(replacing);++revision_;return outgoing;
}
AbilityFailure AbilityState::validate(const AbilityRequest& request)const{
    if(request.owner!=owner_||!request.operation||static_cast<unsigned>(request.slot)>=combat_slot_count||
        static_cast<unsigned>(request.input.edge)>static_cast<unsigned>(InputEdge::Tapped)||!std::isfinite(request.input.value)||std::abs(request.input.value)>1)return AbilityFailure::InvalidRequest;
    if(!kit_||request.grant_generation!=kit_->grant_generation())return AbilityFailure::StaleGrant;
    const auto i=static_cast<unsigned>(request.slot);const auto& definition=grants_[i];
    if(!definition)return AbilityFailure::Unassigned;
    if(request.input.action!=kit_->definition().slots[i].input_action)return AbilityFailure::InvalidRequest;
    if(rearm_[i]&&(request.input.edge!=InputEdge::Pressed||request.input.cancelled))return AbilityFailure::InputIgnored;
    if(request.input.edge==InputEdge::Released&&definition->cancel_on_release&&active_&&active_->slot==request.slot&&active_->grant_generation==request.grant_generation)
        return active_->definition->interruptible?AbilityFailure::None:AbilityFailure::CancelDenied;
    if(request.input.cancelled||request.input.edge!=definition->activate_on||request.input.held_us<definition->minimum_held_us)return AbilityFailure::InputIgnored;
    if(health().current<=0)return AbilityFailure::Dead;
    if(!pending_hits.empty())return AbilityFailure::Busy;
    if(active_&&(!request.replace_active||!active_->definition->interruptible))return AbilityFailure::Busy;
    auto cooldown=cooldowns_.find(definition->cooldown_group);
    if(cooldown!=cooldowns_.end()&&cooldown->second>tick_)return AbilityFailure::Cooldown;
    if(next_activation_==std::numeric_limits<std::uint64_t>::max()||tick_==std::numeric_limits<std::uint64_t>::max()||definition->cooldown_ticks>std::numeric_limits<std::uint64_t>::max()-tick_)return AbilityFailure::InvalidRequest;
    if(definition->cooldown_ticks&&!cooldowns_.contains(definition->cooldown_group)&&cooldowns_.size()>=32)return AbilityFailure::Busy;
    try{auto candidate=attributes_;candidate.transact(costs(*definition),true);}catch(const std::invalid_argument&){return AbilityFailure::Resources;}
    return AbilityFailure::None;
}
AbilityFailure AbilityState::can_activate(const AbilityRequest& request)const{
    auto found=records_.find(request.operation);if(found!=records_.end())return same(found->second.request,request)?found->second.receipt.failure:AbilityFailure::OperationConflict;
    if(request.operation&&request.operation<=highest_operation_)return AbilityFailure::StaleOperation;
    if(records_.size()>=128)return AbilityFailure::HistoryFull;
    return validate(request);
}
std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> AbilityState::request(const AbilityRequest& request){
    auto found=records_.find(request.operation);
    if(found!=records_.end()){auto receipt=found->second.receipt;if(!same(found->second.request,request))return {{AbilityFailure::OperationConflict}, {}};receipt.duplicate=true;return {receipt,{}};}
    auto failure=can_activate(request);
    if(failure==AbilityFailure::StaleOperation||failure==AbilityFailure::HistoryFull||!request.operation||request.owner!=owner_)return {{failure},{}};
    AbilityReceipt receipt{failure};std::vector<AbilityActionUpdate> updates;
    if(failure!=AbilityFailure::InvalidRequest&&kit_&&request.grant_generation==kit_->grant_generation()&&static_cast<unsigned>(request.slot)<combat_slot_count){
        auto i=static_cast<unsigned>(request.slot);
        if(request.input.action==kit_->definition().slots[i].input_action&&request.input.edge==InputEdge::Pressed&&!request.input.cancelled)rearm_[i]=false;
    }
    if(failure==AbilityFailure::None){
        auto definition=grants_[static_cast<unsigned>(request.slot)];
        if(request.input.edge==InputEdge::Released&&definition->cancel_on_release&&active_){
            receipt.handle={owner_,active_->timeline.state().activation};updates=stop(AbilityActionReason::Cancelled);
        }else{
            ActionTimeline prepared(definition->action,next_activation_,tick_);auto entry=prepared.enter();
            auto attributes=attributes_;attributes.transact(costs(*definition),true);
            // All allocations and action validation occur in a session-owned candidate.
            updates=stop(AbilityActionReason::Replaced);
            hits_.clear();
            active_.emplace(Execution{request.slot,request.grant_generation,definition,std::move(prepared)});
            attributes_=std::move(attributes);
            if(definition->cooldown_ticks)cooldowns_[definition->cooldown_group]=tick_+definition->cooldown_ticks;
            receipt.handle={owner_,next_activation_++};receipt.committed=true;
            updates.push_back({receipt.handle,ActionPhase::Active,AbilityActionReason::Started,tick_,std::move(entry)});
            if(health().current<=0){auto death=stop(AbilityActionReason::Death);updates.insert(updates.end(),std::make_move_iterator(death.begin()),std::make_move_iterator(death.end()));}
        }
    }
    records_.emplace(request.operation,Record{request,receipt});highest_operation_=request.operation;++revision_;
    return {receipt,std::move(updates)};
}
std::vector<AbilityActionUpdate> AbilityState::advance(std::uint64_t tick,unsigned rate){
    require(tick_!=std::numeric_limits<std::uint64_t>::max()&&tick==tick_+1&&rate<=4*action_tick_units,"Ability fixed tick/rate");
    require(pending_hits.empty(),"Unresolved ability hit work; resolve or cancel before advancing");
    tick_=tick;std::erase_if(cooldowns_,[&](const auto& cooldown){return cooldown.second<=tick_;});
    std::vector<AbilityActionUpdate> updates;
    if(active_){auto batch=active_->timeline.advance(tick,rate);auto phase=active_->timeline.state().phase;
        for(const auto& interval:batch.traversed)if(interval.kind==ActionBlockKind::HitWindow)
            for(const auto& profile:active_->definition->melee)if(profile.block==interval.block)
                pending_hits.push_back({{owner_,active_->timeline.state().activation},active_->definition,profile,interval,tick});
        require(pending_hits.size()<=64,"Ability pending hit interval work bound");
        updates.push_back({{owner_,active_->timeline.state().activation},phase,phase==ActionPhase::Completed?AbilityActionReason::Completed:AbilityActionReason::Advanced,tick_,std::move(batch)});
        if(phase==ActionPhase::Completed)active_.reset();
    }
    ++revision_;return updates;
}
std::pair<AbilityFailure,std::vector<AbilityActionUpdate>> AbilityState::cancel(AbilityActivationHandle handle,AbilityActionReason reason){
    if(handle.owner!=owner_||!handle.activation||handle.activation>=next_activation_)return {AbilityFailure::InvalidRequest,{}};
    // Completed/previously cancelled handles are idempotent and cannot stop a new action.
    if(!active_||active_->timeline.state().activation!=handle.activation){auto removed=std::erase_if(pending_hits,[&](const auto& hit){return hit.handle==handle;});if(removed)++revision_;return {AbilityFailure::None,{}};}
    if(reason==AbilityActionReason::Cancelled&&!active_->definition->interruptible)return {AbilityFailure::CancelDenied,{}};
    auto updates=stop(reason);++revision_;return {AbilityFailure::None,std::move(updates)};
}
bool AbilityState::remember_hit(const PendingHit& hit,std::uint64_t target,std::uint64_t epoch){
    auto key=std::make_tuple(hit.handle.activation,hit.interval.block,hit.interval.loop,target,epoch);
    if(hits_.contains(key))return false;require(hits_.size()<256,"Ability hit history exhausted");hits_.insert(key);return true;
}
std::vector<AbilityActionUpdate> AbilityState::damage(double amount){
    require(std::isfinite(amount)&&amount>=0,"Invalid prepared damage magnitude");
    attributes_.transact(std::array<ResourceDelta,1>{{{health_,-amount}}},false);++revision_;
    return health().current<=0?stop(AbilityActionReason::Death):std::vector<AbilityActionUpdate>{};
}
void AbilityState::retire(std::uint64_t through){
    require(through>=retired_through_&&through<=highest_operation_,"Ability operation retirement range");
    std::erase_if(records_,[&](const auto& item){return item.first<=through;});retired_through_=through;++revision_;
}
std::vector<AbilityAttributeValue> AbilityState::attribute_values()const{std::vector<AbilityAttributeValue> result;result.reserve(attributes_.definitions().size());for(const auto& definition:attributes_.definitions())result.push_back({definition.id,attributes_.value(definition.id)});return result;}
AbilityOwnerSnapshot AbilityState::snapshot()const{
    AbilityOwnerSnapshot result;result.owner=owner_;result.tick=tick_;result.grant_generation=kit_?kit_->grant_generation():0;result.revision=revision_;
    result.attributes=attribute_values();
    if(active_){result.active=AbilityActivationHandle{owner_,active_->timeline.state().activation};result.action=active_->timeline.state();result.ability=active_->definition->id;result.ability_generation=active_->definition->generation;result.action_definition=active_->definition->action->id;result.active_slot=active_->slot;}
    for(const auto& cooldown:cooldowns_)result.cooldowns.push_back(cooldown);
    result.health_attribute=health_;result.maximum_health_attribute=maximum_health_;result.retained_operations=records_.size();result.highest_operation=highest_operation_;result.retired_through=retired_through_;
    for(const auto& [operation,record]:records_)result.operations.push_back({operation,record.receipt.failure,record.receipt.handle.activation,record.receipt.committed});return result;
}
}
