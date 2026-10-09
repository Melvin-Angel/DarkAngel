#include <darkangel/ability_prediction.hpp>
#include "ability_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace darkangel {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}bool same_owner(const AbilityOwnerHandle& a,const AbilityOwnerHandle& b){return a.network==b.network&&a.session_epoch==b.session_epoch;}}
struct OwnerAbilityPrediction::Impl {
    struct Operation {AbilityPredictionInput input;std::optional<AbilityOperationNotice> terminal;bool invalidated{};std::optional<bool> predicted_commit;};
    struct Frame {std::uint64_t tick{};unsigned rate{};std::vector<std::uint64_t> operations;};
    AbilityState state;AbilityOwnerSnapshot confirmed,current;
    std::vector<std::shared_ptr<const AbilityDefinition>> definitions;
    std::map<std::uint64_t,Operation> operations;
    std::vector<Frame> frames;std::vector<AbilityPredictionMotion> motion;
    std::uint64_t highest_submitted{},avatar_epoch{},confirmed_action_operation{};bool resync{};
    Impl(const AbilityOwnerSnapshot& baseline,std::vector<AttributeDefinition> schema,std::shared_ptr<const CombatKitDefinition> kit,const InputProfile& input,std::span<const std::shared_ptr<const AbilityDefinition>> catalogue,std::uint64_t epoch)
        :state(baseline.owner,baseline.tick,schema,baseline.health_attribute,baseline.maximum_health_attribute),confirmed(baseline),highest_submitted(baseline.highest_operation),avatar_epoch(epoch){
        require(epoch,"Ability prediction avatar epoch");
        state.equip(std::move(kit),input,catalogue);state.restore_prediction(baseline);
        AttributeSet validation(std::move(schema));
        for(const auto& definition:catalogue)definitions.push_back(freeze_ability_definition(*definition,validation));current=state.snapshot();
        if(baseline.active)for(const auto& operation:baseline.operations)if(operation.committed&&operation.activation==baseline.active->activation)confirmed_action_operation=operation.operation;
    }
    // Frozen definitions are resolved from the installed state's pinned identity.
    const ActionDefinition& action(const AbilityOwnerSnapshot& snapshot)const{
        for(const auto& definition:definitions)if(definition->id==snapshot.ability){require(definition->generation==snapshot.ability_generation&&definition->action->generation==snapshot.action->generation,"Prediction motion generation mismatch");return *definition->action;}
        throw std::runtime_error("Prediction action generation unavailable");
    }
    AbilityPredictionMotion run(const Frame& frame){
        auto before=state.snapshot();state.begin_tick(frame.tick);
        for(auto id:frame.operations){auto& operation=operations.at(id);std::optional<AbilityFailure> rejected;
            auto parent=operations.find(operation.input.parent_operation);
            if(operation.invalidated||(parent!=operations.end()&&parent->second.predicted_commit==false))rejected=AbilityFailure::InputIgnored;
            else if(operation.terminal&&operation.terminal->receipt.failure!=AbilityFailure::None)rejected=operation.terminal->receipt.failure;
            auto result=state.request_wire(operation.input.intent,{},rejected);
            require(result.first.failure!=AbilityFailure::HistoryFull&&result.first.failure!=AbilityFailure::OperationConflict&&result.first.failure!=AbilityFailure::StaleOperation,"Prediction operation history unavailable");
            operation.predicted_commit=result.first.committed;
        }
        auto updates=state.finish_tick(frame.rate,false);auto after=state.snapshot();AbilityPredictionMotion output;output.tick=frame.tick;
        if(after.active){const auto& definition=action(after);auto from=before.active==after.active?before.action->clock:0;output.activation=after.active->activation;output.local=action_motion_between(definition,from,after.action->clock);}
        else if(before.active)for(const auto& update:updates)if(update.handle==*before.active&&update.phase==ActionPhase::Completed){const auto& definition=action(before);output.activation=before.active->activation;output.local=action_motion_between(definition,before.action->clock,std::uint64_t(definition.duration)*definition.loops);}
        current=std::move(after);return output;
    }
    void invalidate(){
        for(auto& [id,operation]:operations){if(operation.terminal&&operation.terminal->receipt.failure!=AbilityFailure::None)operation.invalidated=true;
            auto parent=operations.find(operation.input.parent_operation);if(parent!=operations.end()&&parent->second.invalidated)operation.invalidated=true;}
    }
    void rebuild(){state.restore_prediction(confirmed);motion.clear();invalidate();for(const auto& frame:frames)motion.push_back(run(frame));current=state.snapshot();}
};
OwnerAbilityPrediction::OwnerAbilityPrediction(const AbilityOwnerSnapshot& baseline,std::vector<AttributeDefinition> schema,std::shared_ptr<const CombatKitDefinition> kit,const InputProfile& input,std::span<const std::shared_ptr<const AbilityDefinition>> definitions,std::uint64_t epoch)
    :impl_(std::make_unique<Impl>(baseline,std::move(schema),std::move(kit),input,definitions,epoch)){}
OwnerAbilityPrediction::~OwnerAbilityPrediction()=default;
OwnerAbilityPrediction::OwnerAbilityPrediction(const OwnerAbilityPrediction& other):impl_(std::make_unique<Impl>(*other.impl_)){}
OwnerAbilityPrediction& OwnerAbilityPrediction::operator=(const OwnerAbilityPrediction& other){if(this!=&other)impl_=std::make_unique<Impl>(*other.impl_);return *this;}
OwnerAbilityPrediction::OwnerAbilityPrediction(OwnerAbilityPrediction&&)noexcept=default;
OwnerAbilityPrediction& OwnerAbilityPrediction::operator=(OwnerAbilityPrediction&&)noexcept=default;
AbilityPredictionMotion OwnerAbilityPrediction::advance(std::uint64_t tick,std::span<const AbilityPredictionInput> input,unsigned rate){
    require(!impl_->resync,"Ability prediction requires resynchronization");try{auto candidate=*impl_;require(candidate.frames.size()<30&&candidate.operations.size()+input.size()<=128&&input.size()<=32,"Ability prediction history overflow");require(tick==candidate.current.tick+1&&rate<=4*action_tick_units,"Ability prediction fixed tick/rate");Impl::Frame frame{tick,rate,{}};
        for(const auto& value:input){const auto& intent=value.intent;require(intent.tick==tick&&intent.network==candidate.confirmed.owner.network&&intent.avatar_epoch==candidate.avatar_epoch&&intent.grant_generation==candidate.confirmed.grant_generation&&intent.operation>candidate.highest_submitted&&static_cast<unsigned>(intent.slot)<combat_slot_count&&static_cast<unsigned>(intent.edge)<=static_cast<unsigned>(InputEdge::Tapped),"Ability prediction input identity/order");require(!value.parent_operation||(value.parent_operation<intent.operation&&(candidate.operations.contains(value.parent_operation)||value.parent_operation==candidate.confirmed_action_operation)),"Ability prediction parent history unavailable");candidate.highest_submitted=intent.operation;candidate.operations.emplace(intent.operation,Impl::Operation{value});frame.operations.push_back(intent.operation);}
        candidate.invalidate();auto result=candidate.run(frame);candidate.frames.push_back(std::move(frame));candidate.motion.push_back(result);*impl_=std::move(candidate);return result;
    }catch(...){impl_->resync=true;throw;}
}
void OwnerAbilityPrediction::receipt(const AbilityOperationNotice& notice){
    require(!impl_->resync,"Ability prediction requires resynchronization");try{auto candidate=*impl_;require(notice.network==candidate.confirmed.owner.network,"Ability prediction receipt owner");auto found=candidate.operations.find(notice.receipt.operation);if(found==candidate.operations.end())return;
        if(!notice.terminal){require(notice.receipt.failure==AbilityFailure::HistoryFull,"Ability prediction nonterminal receipt");throw std::runtime_error("Ability prediction server history overflow");}
        require(notice.receipt.inclusion_revision&&notice.receipt.tick>=found->second.input.intent.tick&&(!notice.receipt.committed||(notice.receipt.failure==AbilityFailure::None&&notice.receipt.handle.activation)),"Ability prediction receipt identity");
        require(!notice.receipt.handle.activation||same_owner(notice.receipt.handle.owner,candidate.confirmed.owner),"Ability prediction receipt lifecycle identity");
        if(found->second.terminal){const auto& old=found->second.terminal->receipt;require(old.failure==notice.receipt.failure&&old.committed==notice.receipt.committed&&old.handle.activation==notice.receipt.handle.activation&&old.tick==notice.receipt.tick&&old.inclusion_revision==notice.receipt.inclusion_revision,"Ability prediction conflicting terminal receipt");return;}
        // Tick displacement requires a new coherent baseline; never backdate a server decision.
        require(notice.receipt.tick==found->second.input.intent.tick,"Ability prediction execution tick changed; resynchronize");found->second.terminal=notice;candidate.rebuild();*impl_=std::move(candidate);
    }catch(...){impl_->resync=true;throw;}
}
bool OwnerAbilityPrediction::reconcile(const AbilityOwnerSnapshot& baseline){
    require(!impl_->resync,"Ability prediction requires resynchronization");if(same_owner(baseline.owner,impl_->confirmed.owner)&&baseline.revision<impl_->confirmed.revision)return false;
    try{auto candidate=*impl_;require(same_owner(baseline.owner,candidate.confirmed.owner)&&baseline.grant_generation==candidate.confirmed.grant_generation&&baseline.tick>=candidate.confirmed.tick&&baseline.tick<=candidate.current.tick,"Ability prediction baseline continuity");
        if(baseline.revision==candidate.confirmed.revision){require(baseline.tick==candidate.confirmed.tick,"Ability prediction conflicting revision");return false;}
        for(const auto& terminal:baseline.operations){auto found=candidate.operations.find(terminal.operation);if(found==candidate.operations.end())continue;const auto& receipt=found->second.terminal;
            require(found->second.input.intent.tick<=baseline.tick&&(!receipt||(baseline.revision>=receipt->receipt.inclusion_revision&&terminal.failure==receipt->receipt.failure&&terminal.committed==receipt->receipt.committed&&terminal.activation==receipt->receipt.handle.activation)),"Ability prediction conflicting exact inclusion");
            if(terminal.failure!=AbilityFailure::None)found->second.invalidated=true;
        }
        candidate.invalidate();std::set<std::uint64_t> included;for(const auto& terminal:baseline.operations)included.insert(terminal.operation);
        for(const auto& [id,operation]:candidate.operations)require(included.contains(id)||operation.input.intent.tick>baseline.tick,"Ability prediction missing exact operation inclusion");
        std::erase_if(candidate.operations,[&](const auto& item){return included.contains(item.first);});std::erase_if(candidate.frames,[&](const auto& frame){return frame.tick<=baseline.tick;});
        require(candidate.frames.empty()||candidate.frames.front().tick==baseline.tick+1,"Ability prediction missing tick history");
        if(!baseline.active||!candidate.confirmed.active||baseline.active->activation!=candidate.confirmed.active->activation)candidate.confirmed_action_operation=0;
        if(baseline.active)for(const auto& operation:baseline.operations)if(operation.committed&&operation.activation==baseline.active->activation)candidate.confirmed_action_operation=operation.operation;
        candidate.confirmed=baseline;candidate.highest_submitted=std::max(candidate.highest_submitted,baseline.highest_operation);candidate.rebuild();*impl_=std::move(candidate);return true;
    }catch(...){impl_->resync=true;throw;}
}
const AbilityOwnerSnapshot& OwnerAbilityPrediction::baseline()const{return impl_->confirmed;}
const AbilityOwnerSnapshot& OwnerAbilityPrediction::view()const{return impl_->current;}
std::span<const AbilityPredictionMotion> OwnerAbilityPrediction::replay_motion()const{return impl_->motion;}
std::size_t OwnerAbilityPrediction::pending()const{return impl_->operations.size();}
bool OwnerAbilityPrediction::needs_resync()const{return impl_->resync;}
}
