#include "ability_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
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
std::shared_ptr<const AbilityDefinition> freeze(const AbilityDefinition& source,const AttributeSet& attributes,const TagDictionary* tags){
    const bool tagged=source.tag_registry!=AssetId{}||!source.tag_generation.empty()||!source.requirements.all.empty()||!source.requirements.any.empty()||!source.requirements.none.empty()||!source.action_tags.empty();
    if(tagged){require(tags&&source.tag_registry!=AssetId{}&&source.tag_registry==tags->registry()&&source.tag_generation==tags->generation(),"Ability tag registry generation mismatch");tags->validate(source.requirements);}
    require(source.id!=AssetId{}&&source.generation.size()==64&&source.generation.find_first_not_of("0123456789abcdef")==std::string::npos,"Ability identity/generation");
    require(source.action&&source.action->id!=AssetId{}&&source.costs.size()<=8&&source.cooldown_ticks<=36000&&
        (source.cooldown_ticks==0||source.cooldown_group!=0)&&static_cast<unsigned>(source.activate_on)<=static_cast<unsigned>(InputEdge::Tapped)&&
        source.minimum_held_us<=10000000&&!(source.cancel_on_release&&source.activate_on==InputEdge::Released),"Ability definition bounds/policy");
    require(source.melee.size()<=8,"Ability melee profile count");std::set<unsigned> hit_blocks;
    const ActionBlock* commit_block=nullptr;
    if(source.deferred_commit_block){auto found=std::find_if(source.action->blocks.begin(),source.action->blocks.end(),[&](const auto& b){return b.id==source.deferred_commit_block&&b.kind==ActionBlockKind::Commit;});require(source.action->loops==1&&found!=source.action->blocks.end(),"Deferred ability requires one finite commit marker");commit_block=&*found;}
    require(source.action_tags.size()<=16,"Ability action tag binding bound");std::set<std::pair<unsigned,TagId>> bindings;
    for(const auto& binding:source.action_tags){
        auto block=std::find_if(source.action->blocks.begin(),source.action->blocks.end(),[&](const auto& b){return b.id==binding.block;});
        require(block!=source.action->blocks.end()&&(block->kind==ActionBlockKind::Invulnerability||block->kind==ActionBlockKind::MovementLock)&&tags&&tags->contains(binding.tag)&&bindings.emplace(binding.block,binding.tag).second,"Ability action tag block/type/identity");
    }
    for(const auto& hit:source.melee){
        auto block=std::find_if(source.action->blocks.begin(),source.action->blocks.end(),[&](const auto& b){return b.id==hit.block&&b.kind==ActionBlockKind::HitWindow;});
        require(block!=source.action->blocks.end()&&hit_blocks.insert(hit.block).second&&hit.evaluator&&hit.damage_type&&std::isfinite(hit.power)&&hit.power>=0&&hit.power<=1e9&&std::isfinite(hit.radius)&&hit.radius>0&&hit.radius<=2,"Ability melee profile identity/bounds");
        require(!commit_block||block->begin>=commit_block->begin,"Deferred ability cannot damage before commitment");
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
std::vector<AbilityCost> reserved_costs(const AbilityDefinition& definition){std::map<AttributeId,double> totals;for(const auto& cost:definition.costs)totals[cost.attribute]+=cost.amount;std::vector<AbilityCost> result;for(auto [id,amount]:totals)if(amount)result.push_back({id,amount});return result;}
}
std::shared_ptr<const AbilityDefinition> freeze_ability_definition(const AbilityDefinition& source,const AttributeSet& attributes,const TagDictionary* tags){return freeze(source,attributes,tags);}
AbilityState::AbilityState(AbilityOwnerHandle owner,std::uint64_t tick,std::vector<AttributeDefinition> definitions,AttributeId health,AttributeId maximum_health):
    owner_(owner),tick_(tick),attributes_(std::move(definitions)),health_(health),maximum_health_(maximum_health),effects_(std::make_shared<const TagDictionary>(std::vector<TagDefinition>{}),tick){
    require(health_&&maximum_health_&&health_!=maximum_health_,"Ability health schema IDs");
    const auto schema=attributes_.definitions();
    auto find=[&](AttributeId id)->const AttributeDefinition&{auto it=std::find_if(schema.begin(),schema.end(),[&](const auto& d){return d.id==id;});require(it!=schema.end(),"Ability health schema missing");return *it;};
    const auto& h=find(health_);const auto& maximum=find(maximum_health_);
    require(h.visibility!=AttributeVisibility::Server&&maximum.visibility!=AttributeVisibility::Server,"Existing public Health cannot have server-only visibility");
    require(h.kind==AttributeKind::Resource&&h.maximum_attribute==maximum_health_&&h.minimum==0&&maximum.kind==AttributeKind::Statistic&&maximum.minimum>=0,"Ability health schema relationship");
}
Health AbilityState::health()const{return {attributes_.value(maximum_health_),attributes_.value(health_)};}
double AbilityState::available(AttributeId id)const{
    auto definitions=attributes_.definitions();auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.id==id;});require(definition!=definitions.end()&&definition->kind==AttributeKind::Resource,"Ability availability requires a resource");double amount=attributes_.value(id)-definition->minimum;
    if(active_&&active_->cost_phase==AbilityCommitPhase::Reserved)for(const auto& cost:active_->definition->costs)if(cost.attribute==id)amount-=cost.amount;return std::max(0.,amount);
}
void AbilityState::note_commitment(AbilityCommitPhase phase,AbilityFailure failure){
    require(active_&&active_->definition->deferred_commit_block&&commitments_.size()<128,"Ability commitment trace bound/identity");commitments_.push_back({{owner_,active_->timeline.state().activation},active_->operation,tick_,revision_+1,active_->definition->deferred_commit_block,phase,failure});
}
AbilityFailure AbilityState::commit_reserved(){
    require(active_&&active_->cost_phase==AbilityCommitPhase::Reserved,"Missing ability reservation");const auto& definition=*active_->definition;
    if(!kit_||active_->grant_generation!=kit_->grant_generation())return AbilityFailure::StaleGrant;
    if(health().current<=0)return AbilityFailure::Dead;
    if(!effects_.tags().matches(definition.requirements))return AbilityFailure::TagRequirements;
    auto cooldown=cooldowns_.find(definition.cooldown_group);if(cooldown!=cooldowns_.end()&&cooldown->second>tick_)return AbilityFailure::Cooldown;
    if(definition.cooldown_ticks&&!cooldowns_.contains(definition.cooldown_group)&&cooldowns_.size()>=32)return AbilityFailure::Busy;
    if(definition.cooldown_ticks>UINT64_MAX-tick_)return AbilityFailure::InvalidRequest;
    auto attributes=attributes_;try{attributes.transact(costs(definition),true);}catch(const std::invalid_argument&){return AbilityFailure::Resources;}
    if(definition.cooldown_ticks)cooldowns_[definition.cooldown_group]=tick_+definition.cooldown_ticks;
    attributes_=std::move(attributes);active_->cost_phase=AbilityCommitPhase::Committed;note_commitment(AbilityCommitPhase::Committed);return AbilityFailure::None;
}
namespace {
std::vector<TagId> action_tags(const AbilityDefinition& definition,const ActionState& state){
    std::set<TagId> tags;if(state.phase!=ActionPhase::Active)return {};
    auto local=state.clock%definition.action->duration;
    for(const auto& binding:definition.action_tags){const auto& blocks=definition.action->blocks;auto block=std::find_if(blocks.begin(),blocks.end(),[&](const auto& b){return b.id==binding.block;});if(block->begin<=local&&local<block->end)tags.insert(binding.tag);}
    return {tags.begin(),tags.end()};
}
}
void AbilityState::sync_action_tags(){effects_.set_action_tags(active_?action_tags(*active_->definition,active_->timeline.state()):std::vector<TagId>{},attributes_);}
std::vector<AbilityActionUpdate> AbilityState::stop(AbilityActionReason reason,bool release_tags){
    pending_hits.clear();
    if(reason==AbilityActionReason::Disconnected||reason==AbilityActionReason::InputLost||reason==AbilityActionReason::Death||reason==AbilityActionReason::Despawned||reason==AbilityActionReason::GrantRemoved)held_.fill({});
    if(!active_)return {};
    if(active_->cost_phase==AbilityCommitPhase::Reserved)note_commitment(AbilityCommitPhase::Released);
    AbilityActionUpdate update{{owner_,active_->timeline.state().activation},ActionPhase::Cancelled,reason,tick_,active_->timeline.cancel()};
    active_.reset();if(release_tags){sync_action_tags();if(health().current<=0)effects_.death(attributes_);}return {std::move(update)};
}
std::vector<AbilityActionUpdate> AbilityState::equip(std::shared_ptr<const CombatKitDefinition> definition,const InputProfile& input,std::span<const std::shared_ptr<const AbilityDefinition>> definitions){
    require(definition&&definitions.size()<=combat_slot_count,"Ability kit catalogue bound");
    validate_combat_kit(*definition,input);
    std::map<AssetId,std::shared_ptr<const AbilityDefinition>> prepared;
    for(const auto& ability:definitions){require(bool(ability),"Missing ability definition");auto frozen=freeze_ability_definition(*ability,attributes_,&effects_.tags().dictionary());require(prepared.emplace(frozen->id,std::move(frozen)).second,"Duplicate ability definition");}
    std::array<std::shared_ptr<const AbilityDefinition>,combat_slot_count> grants;
    for(unsigned i=0;i<combat_slot_count;++i){const auto& slot=definition->slots[i];if(slot.ability!=AssetId{}){auto it=prepared.find(slot.ability);require(it!=prepared.end(),"Missing assigned ability");grants[i]=it->second;}}
    std::optional<CombatKitInstance> candidate=kit_;
    const bool replacing=bool(candidate);
    if(candidate)candidate->replace(definition,input);else candidate.emplace(definition,input);
    auto outgoing=stop(AbilityActionReason::GrantRemoved);
    kit_=std::move(candidate);grants_=std::move(grants);rearm_.fill(replacing);held_.fill({});held_generation_=kit_->grant_generation();
    for(unsigned i=0;i<combat_slot_count;++i)input_[i]=*std::find_if(input.actions.begin(),input.actions.end(),[&](const auto& a){return a.id==kit_->definition().slots[i].input_action;});++revision_;return outgoing;
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
    if(!effects_.tags().matches(definition->requirements))return AbilityFailure::TagRequirements;
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
            const bool deferred=definition->deferred_commit_block&&std::none_of(entry.events.begin(),entry.events.end(),[&](const auto& event){return event.kind==ActionBlockKind::Commit&&event.block==definition->deferred_commit_block;});
            auto attributes=attributes_;if(!deferred)attributes.transact(costs(*definition),true);
            // All allocations and action validation occur in a session-owned candidate.
            attributes_=std::move(attributes);
            // Replace the action contributor once, after the new execution is
            // prepared. A remove/re-add could transiently clamp resources.
            updates=stop(AbilityActionReason::Replaced,false);
            hits_.clear();
            active_.emplace(Execution{request.slot,request.grant_generation,definition,std::move(prepared),request.operation,deferred?AbilityCommitPhase::Reserved:AbilityCommitPhase::Committed});
            sync_action_tags();
            if(!deferred&&definition->cooldown_ticks)cooldowns_[definition->cooldown_group]=tick_+definition->cooldown_ticks;
            if(definition->deferred_commit_block)note_commitment(deferred?AbilityCommitPhase::Reserved:AbilityCommitPhase::Committed);
            receipt.handle={owner_,next_activation_++};receipt.committed=true;
            updates.push_back({receipt.handle,ActionPhase::Active,AbilityActionReason::Started,tick_,std::move(entry)});
            if(health().current<=0){effects_.death(attributes_);auto death=stop(AbilityActionReason::Death);updates.insert(updates.end(),std::make_move_iterator(death.begin()),std::make_move_iterator(death.end()));}
        }
    }
    receipt.operation=request.operation;receipt.tick=tick_;receipt.inclusion_revision=revision_+1;
    records_.emplace(request.operation,Record{request,receipt,{}});highest_operation_=request.operation;++revision_;
    return {receipt,std::move(updates)};
}
void AbilityState::begin_tick(std::uint64_t tick){
    require(tick_!=std::numeric_limits<std::uint64_t>::max()&&tick==tick_+1,"Ability fixed tick/rate");
    require(pending_hits.empty(),"Unresolved ability hit work; resolve or cancel before advancing");
    tick_=tick;std::erase_if(cooldowns_,[&](const auto& cooldown){return cooldown.second<=tick_;});
}
std::vector<AbilityActionUpdate> AbilityState::finish_tick(unsigned rate,bool gameplay_hits){
    require(rate<=4*action_tick_units,"Ability action rate bound");
    std::vector<AbilityActionUpdate> updates;
    const bool entered_now=active_&&active_->timeline.state().tick==tick_;
    if(active_&&active_->timeline.state().tick<tick_){auto previous=active_->timeline;auto batch=active_->timeline.advance(tick_,rate);auto phase=active_->timeline.state().phase;
        auto marker=std::find_if(batch.events.begin(),batch.events.end(),[&](const auto& event){return event.kind==ActionBlockKind::Commit&&event.block==active_->definition->deferred_commit_block;});
        if(active_->cost_phase==AbilityCommitPhase::Reserved&&marker!=batch.events.end()){
            // Fast action rates may cross later tag/maximum boundaries too.
            // Validate and consume at the selected marker before final cleanup.
            auto at_marker=active_->timeline.state();at_marker.clock=marker->time;at_marker.phase=ActionPhase::Active;effects_.set_action_tags(action_tags(*active_->definition,at_marker),attributes_);
            auto failure=commit_reserved();if(failure!=AbilityFailure::None){note_commitment(AbilityCommitPhase::Rejected,failure);active_->cost_phase=AbilityCommitPhase::Rejected;active_->timeline=std::move(previous);auto rejected=stop(failure==AbilityFailure::Dead?AbilityActionReason::Death:AbilityActionReason::CommitRejected);updates.insert(updates.end(),std::make_move_iterator(rejected.begin()),std::make_move_iterator(rejected.end()));}
            else if(health().current<=0){active_->timeline=std::move(previous);effects_.death(attributes_);auto death=stop(AbilityActionReason::Death);updates.insert(updates.end(),std::make_move_iterator(death.begin()),std::make_move_iterator(death.end()));}
        }
        if(active_){
        for(const auto& interval:batch.traversed)if(gameplay_hits&&interval.kind==ActionBlockKind::HitWindow)
            for(const auto& profile:active_->definition->melee)if(profile.block==interval.block)
                pending_hits.push_back({{owner_,active_->timeline.state().activation},active_->definition,profile,interval,tick_});
        require(pending_hits.size()<=64,"Ability pending hit interval work bound");
        updates.push_back({{owner_,active_->timeline.state().activation},phase,phase==ActionPhase::Completed?AbilityActionReason::Completed:AbilityActionReason::Advanced,tick_,std::move(batch)});
        if(phase==ActionPhase::Completed)active_.reset();
        sync_action_tags();
        if(health().current<=0){effects_.death(attributes_);auto death=stop(AbilityActionReason::Death);updates.insert(updates.end(),std::make_move_iterator(death.begin()),std::make_move_iterator(death.end()));}
        }
    }
    if(gameplay_hits&&entered_now&&active_&&active_->timeline.state().clock==0){
        // An action entered in this phase exposes its time-zero hit windows once.
        // Existing rate-zero actions enter the branch above and never repeat this.
        for(const auto& block:active_->definition->action->blocks)if(block.kind==ActionBlockKind::HitWindow&&block.begin==0)
            for(const auto& profile:active_->definition->melee)if(profile.block==block.id)
                pending_hits.push_back({{owner_,active_->timeline.state().activation},active_->definition,profile,{block.id,0,0,0,ActionBlockKind::HitWindow},tick_});
    }
    ++revision_;return updates;
}
std::vector<AbilityActionUpdate> AbilityState::advance(std::uint64_t tick,unsigned rate){begin_tick(tick);return finish_tick(rate);}
std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> AbilityState::request_wire(const AbilityIntent& intent,std::optional<AbilityFailure> forced,std::optional<AbilityFailure> commitment_failure){
    auto previous=records_.find(intent.operation);
    if(previous!=records_.end()){
        if(!previous->second.intent||*previous->second.intent!=intent)return {{AbilityFailure::OperationConflict},{}};
        auto receipt=previous->second.receipt;receipt.duplicate=true;return {receipt,{}};
    }
    if(intent.operation<=highest_operation_)return {{AbilityFailure::StaleOperation,{},false,false,intent.operation,tick_,0},{}};
    if(records_.size()>=128)return {{AbilityFailure::HistoryFull,{},false,false,intent.operation,tick_,0},{}};
    require(intent.operation&&intent.network==owner_.network&&static_cast<unsigned>(intent.slot)<combat_slot_count,"Wire ability request identity");
    const auto i=static_cast<unsigned>(intent.slot);InputEvent input;input.action=kit_?kit_->definition().slots[i].input_action:0;input.edge=intent.edge;input.cancelled=intent.cancelled;input.value=(intent.edge==InputEdge::Pressed||intent.edge==InputEdge::Hold)?1.f:0.f;
    require(tick_<=std::numeric_limits<std::uint64_t>::max()/1000000,"Wire input clock range");input.time_us=tick_*1000000/60;
    auto held=held_;auto failure=forced;
    if(!failure&&(!kit_||intent.grant_generation!=kit_->grant_generation()))failure=AbilityFailure::StaleGrant;
    if(!failure){auto& key=held[i];auto held_us=[&]{return std::min<std::uint64_t>(10000000,(tick_-key.pressed)*1000000/60);};
        if(intent.edge==InputEdge::Pressed){if(key.active||intent.cancelled)failure=AbilityFailure::InputIgnored;else key={true,tick_,0,0};}
        else if(intent.edge==InputEdge::Hold){if(!key.active||key.hold_sent||intent.cancelled)failure=AbilityFailure::InputIgnored;else {input.held_us=held_us();auto threshold=input_[i].hold_us;if(grants_[i]&&grants_[i]->activate_on==InputEdge::Hold)threshold=std::max(threshold,grants_[i]->minimum_held_us);if(input.held_us<threshold)failure=AbilityFailure::InputIgnored;else key.hold_sent=true;}}
        else if(intent.edge==InputEdge::Released){if(!key.active)failure=AbilityFailure::InputIgnored;else {input.held_us=held_us();key.active=false;key.released=intent.cancelled?0:tick_;key.duration=input.held_us;}}
        else if(intent.edge==InputEdge::Tapped){if(key.active||key.released!=tick_||intent.cancelled||key.duration>input_[i].tap_us)failure=AbilityFailure::InputIgnored;else {input.held_us=key.duration;key.released=0;}}
        else failure=AbilityFailure::InvalidRequest;
    }
    if(!failure&&commitment_failure){failure=commitment_failure;if(intent.edge==InputEdge::Pressed&&!intent.cancelled)rearm_[i]=false;}
    AbilityRequest request{owner_,intent.operation,intent.grant_generation,intent.slot,input,intent.replace_active};
    std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> result;
    if(failure){std::vector<AbilityActionUpdate> cleanup;
        if(*failure==AbilityFailure::InputExpired&&intent.edge==InputEdge::Released&&held[i].active){held[i]={};if(active_&&active_->slot==intent.slot&&active_->definition->cancel_on_release)cleanup=stop(AbilityActionReason::InputLost);}
        AbilityReceipt receipt{*failure,{},false,false,intent.operation,tick_,revision_+1};records_.emplace(intent.operation,Record{request,receipt,intent});highest_operation_=intent.operation;++revision_;result={receipt,std::move(cleanup)};}
    else {result=this->request(request);if(records_.contains(intent.operation))records_.at(intent.operation).intent=intent;}
    if(records_.contains(intent.operation)){if(health().current>0)held_=std::move(held);else held_.fill({});}return result;
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
    if(health().current<=0){effects_.death(attributes_);return stop(AbilityActionReason::Death);}return {};
}
std::vector<AbilityActionUpdate> AbilityState::disconnect(){auto result=stop(AbilityActionReason::Disconnected);held_.fill({});rearm_.fill(true);++revision_;return result;}
void AbilityState::retire(std::uint64_t through){
    require(through>=retired_through_&&through<=highest_operation_,"Ability operation retirement range");
    std::erase_if(records_,[&](const auto& item){return item.first<=through;});retired_through_=through;++revision_;
}
std::vector<AbilityAttributeValue> AbilityState::attribute_values()const{std::vector<AbilityAttributeValue> result;result.reserve(attributes_.definitions().size());for(const auto& definition:attributes_.definitions())result.push_back({definition.id,attributes_.value(definition.id)});return result;}
AbilityOwnerSnapshot AbilityState::snapshot()const{
    AbilityOwnerSnapshot result;result.owner=owner_;result.tick=tick_;result.grant_generation=kit_?kit_->grant_generation():0;result.revision=revision_;
    result.attributes=attribute_values();result.tags=effects_.tags().snapshot(AttributeVisibility::Server);
    auto external=effects_.external_tags(AttributeVisibility::Server);std::set_difference(result.tags.values.begin(),result.tags.values.end(),external.begin(),external.end(),std::back_inserter(result.action_only_tags));
    if(active_){result.active=AbilityActivationHandle{owner_,active_->timeline.state().activation};result.action=active_->timeline.state();result.ability=active_->definition->id;result.ability_generation=active_->definition->generation;result.action_definition=active_->definition->action->id;result.active_slot=active_->slot;}
    if(active_){result.active_operation=active_->operation;if(active_->cost_phase==AbilityCommitPhase::Reserved)result.reservation=AbilityReservationSnapshot{active_->definition->deferred_commit_block,reserved_costs(*active_->definition),active_->definition->cooldown_group,active_->definition->cooldown_ticks};}
    for(const auto& cooldown:cooldowns_)result.cooldowns.push_back(cooldown);
    result.health_attribute=health_;result.maximum_health_attribute=maximum_health_;result.retained_operations=records_.size();result.highest_operation=highest_operation_;result.retired_through=retired_through_;
    result.next_activation=next_activation_;
    for(unsigned i=0;i<combat_slot_count;++i)result.input[i]={held_[i].active,held_[i].hold_sent,rearm_[i],held_[i].pressed,held_[i].released,held_[i].duration};
    for(const auto& [operation,record]:records_)result.operations.push_back({operation,record.receipt.failure,record.receipt.handle.activation,record.receipt.committed});return result;
}
AbilityOwnerSnapshot AbilityState::owner_snapshot()const{
    auto result=snapshot();result.tags=effects_.tags().snapshot(AttributeVisibility::Owner);auto external=effects_.external_tags(AttributeVisibility::Owner);result.action_only_tags.clear();std::set_difference(result.tags.values.begin(),result.tags.values.end(),external.begin(),external.end(),std::back_inserter(result.action_only_tags));std::erase_if(result.attributes,[&](const auto& value){auto schema=attributes_.definitions();return std::find_if(schema.begin(),schema.end(),[&](const auto& definition){return definition.id==value.id;})->visibility==AttributeVisibility::Server;});if(result.reservation)std::erase_if(result.reservation->costs,[&](const auto& cost){auto schema=attributes_.definitions();return std::find_if(schema.begin(),schema.end(),[&](const auto& d){return d.id==cost.attribute;})->visibility==AttributeVisibility::Server;});return result;
}
AbilityPublicSnapshot AbilityState::public_snapshot()const{
    AbilityPublicSnapshot result;result.owner=owner_;result.tick=tick_;result.revision=revision_;result.health_attribute=health_;result.maximum_health_attribute=maximum_health_;
    result.tags=effects_.tags().snapshot(AttributeVisibility::Public);
    for(const auto& definition:attributes_.definitions())if(definition.visibility==AttributeVisibility::Public||definition.id==health_||definition.id==maximum_health_)result.attributes.push_back({definition.id,attributes_.value(definition.id)});
    if(active_){result.active=AbilityActivationHandle{owner_,active_->timeline.state().activation};result.action=active_->timeline.state();result.action_definition=active_->definition->action->id;}return result;
}
void AbilityState::restore_prediction(const AbilityOwnerSnapshot& source){
    auto effects=effects_;auto external=source.tags;effects.tags().dictionary().validate_snapshot(source.tags,AttributeVisibility::Owner);
    require(source.action_only_tags.size()<=16,"Prediction action tag provenance bound");std::set<TagId> only;
    for(auto tag:source.action_only_tags)require(only.insert(tag).second&&std::find(source.tags.values.begin(),source.tags.values.end(),tag)!=source.tags.values.end(),"Prediction action tag provenance identity");
    std::erase_if(external.values,[&](auto tag){return only.contains(tag);});effects.restore_owner_tags(external);
    require(source.owner.network==owner_.network&&source.owner.session_epoch==owner_.session_epoch&&source.grant_generation&&kit_&&source.next_activation&&source.health_attribute==health_&&source.maximum_health_attribute==maximum_health_,"Prediction owner/schema identity");
    require(source.attributes.size()==attributes_.definitions().size()&&source.cooldowns.size()<=32&&source.operations.size()<=128&&source.retired_through<=source.highest_operation,"Prediction baseline bounds");
    std::uint64_t previous=source.retired_through;for(const auto& operation:source.operations){require(operation.operation>previous&&operation.operation<=source.highest_operation&&operation.activation<source.next_activation&&static_cast<unsigned>(operation.failure)<=static_cast<unsigned>(AbilityFailure::TagRequirements)&&(!operation.committed||(operation.failure==AbilityFailure::None&&operation.activation)),"Prediction exact operation baseline");previous=operation.operation;}
    auto schema=std::vector<AttributeDefinition>(attributes_.definitions().begin(),attributes_.definitions().end());std::set<AttributeId> ids;
    for(const auto& value:source.attributes){auto it=std::find_if(schema.begin(),schema.end(),[&](const auto& d){return d.id==value.id;});require(it!=schema.end()&&ids.insert(value.id).second&&std::isfinite(value.value)&&value.value>=it->minimum&&value.value<=it->maximum,"Prediction attribute schema/value");it->base=value.value;}
    AttributeSet attributes(std::move(schema));for(const auto& value:source.attributes)require(attributes.value(value.id)==value.value,"Prediction resource bound mismatch");
    std::map<std::uint32_t,std::uint64_t> cooldowns;for(auto [group,until]:source.cooldowns)require(group&&until>source.tick&&cooldowns.emplace(group,until).second,"Prediction cooldown baseline");
    require(bool(source.active)==bool(source.action)&&bool(source.active)==bool(source.active_operation)&&(!source.reservation||source.active)&&source.active_operation<=source.highest_operation,"Prediction action/operation/reservation pairing");std::optional<Execution> execution;
    if(source.active){require(source.active->owner==source.owner&&source.active->activation<source.next_activation&&static_cast<unsigned>(source.active_slot)<combat_slot_count,"Prediction active identity");auto definition=grants_[static_cast<unsigned>(source.active_slot)];require(definition&&definition->id==source.ability&&definition->generation==source.ability_generation&&definition->action->id==source.action_definition&&definition->action->generation==source.action->generation,"Prediction frozen generation mismatch");ActionTimeline timeline(definition->action,source.active->activation,source.tick);timeline.restore(*source.action);require(source.action->tick==source.tick&&source.action->phase==ActionPhase::Active,"Prediction action clock");execution.emplace(Execution{source.active_slot,source.grant_generation,definition,std::move(timeline),source.active_operation,source.reservation?AbilityCommitPhase::Reserved:AbilityCommitPhase::Committed});
        bool awaiting=false;if(definition->deferred_commit_block){auto marker=std::find_if(definition->action->blocks.begin(),definition->action->blocks.end(),[&](const auto& b){return b.id==definition->deferred_commit_block;});awaiting=source.action->clock<marker->begin;}require(awaiting==bool(source.reservation),"Prediction commit-marker phase mismatch");
        if(source.reservation){const auto& reservation=*source.reservation;auto expected=reserved_costs(*definition);require(reservation.block==definition->deferred_commit_block&&reservation.cooldown_group==definition->cooldown_group&&reservation.cooldown_ticks==definition->cooldown_ticks&&reservation.costs.size()==expected.size(),"Prediction reservation definition mismatch");for(std::size_t i=0;i<expected.size();++i)require(reservation.costs[i].attribute==expected[i].attribute&&reservation.costs[i].amount==expected[i].amount,"Prediction reserved resource mismatch");}
    }
    auto rebuilt=execution?action_tags(*execution->definition,execution->timeline.state()):std::vector<TagId>{};
    std::erase_if(rebuilt,[&](auto tag){auto definitions=effects.tags().dictionary().definitions();return std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.id==tag;})->visibility==AttributeVisibility::Server;});
    for(auto tag:only)require(std::find(rebuilt.begin(),rebuilt.end(),tag)!=rebuilt.end(),"Prediction provenance does not belong to active action");
    effects.set_action_tags(rebuilt,attributes);auto actual=effects.tags().values(AttributeVisibility::Owner);auto expected=source.tags.values;std::sort(expected.begin(),expected.end());require(actual==expected,"Prediction action/aggregate tag mismatch");
    std::array<HeldInput,combat_slot_count> held;std::array<bool,combat_slot_count> rearm;
    for(unsigned i=0;i<combat_slot_count;++i){const auto& input=source.input[i];require(input.pressed<=source.tick&&input.released<=source.tick&&input.duration<=10000000&&(!input.active||!input.released),"Prediction input baseline");held[i]={input.active,input.pressed,input.released,input.duration,input.hold_sent};rearm[i]=input.rearm;}
    attributes_=std::move(attributes);effects_=std::move(effects);active_=std::move(execution);cooldowns_=std::move(cooldowns);held_=held;rearm_=rearm;kit_->generation_=source.grant_generation;held_generation_=source.grant_generation;
    owner_=source.owner;tick_=source.tick;revision_=source.revision;highest_operation_=source.highest_operation;retired_through_=source.retired_through;next_activation_=source.next_activation;records_.clear();pending_hits.clear();hits_.clear();commitments_.clear();
}
}
