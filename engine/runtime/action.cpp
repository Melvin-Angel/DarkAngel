#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <tuple>
#include <stdexcept>
namespace darkangel {
namespace {
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
bool interval(ActionBlockKind kind){return kind!=ActionBlockKind::Cue;}
ActionEvent event(const ActionState& state,const ActionBlock& block,unsigned loop,unsigned local,ActionEdge edge,unsigned duration){return {state.activation,std::uint64_t(loop)*duration+local,block.id,block.track,loop,edge,block.kind,block.key};}
void order(ActionBatch& batch){require(batch.events.size()<=512&&batch.traversed.size()<=256,"Action boundary work limit");std::sort(batch.events.begin(),batch.events.end(),[](const auto& a,const auto& b){return std::tie(a.time,a.loop,a.track,a.block,a.edge)<std::tie(b.time,b.loop,b.track,b.block,b.edge);});}
}
ActionDefinition decode_action_source(std::string_view bytes){
    using Json=nlohmann::json;require(bytes.size()<=65536,"Action source byte limit");unsigned work{};std::vector<std::set<std::string>> keys;
    auto source=Json::parse(bytes,[&](int depth,Json::parse_event_t e,Json& v){require(depth<=8&&++work<=4096,"Action source work limit");if(e==Json::parse_event_t::object_start)keys.emplace_back();if(e==Json::parse_event_t::key)require(keys.back().insert(v.get<std::string>()).second,"Duplicate action field");if(e==Json::parse_event_t::object_end)keys.pop_back();return true;});
    require(source.is_object()&&source.size()==8&&source.at("schema")==1&&source.at("kind")=="action","Action source schema");ActionDefinition definition;definition.id=AssetId::parse(source.at("asset").get<std::string>());
    auto integer=[&](const Json& value,unsigned maximum){require(value.is_number_unsigned()&&value.get<std::uint64_t>()<=maximum,"Action integer range");return value.get<unsigned>();};
    definition.duration=integer(source.at("duration"),600*action_tick_units);require(definition.duration>=action_tick_units,"Action duration/loop work bound");definition.loops=integer(source.at("loops"),8);require(definition.loops>0,"Action finite loop count");definition.priority=integer(source.at("priority"),255);
    auto slot=source.at("slot").get<std::string>();require(slot=="full-body"||slot=="upper-body","Action pose slot");definition.upper_body=slot=="upper-body";
    require(source.at("blocks").is_array()&&source.at("blocks").size()<=32,"Action block bound");std::set<unsigned> ids;
    for(const auto& record:source.at("blocks")){
        require(record.is_object()&&record.size()==6,"Action block schema");ActionBlock block;block.id=integer(record.at("id"),UINT32_MAX);require(block.id&&ids.insert(block.id).second,"Action block identity");block.track=integer(record.at("track"),31);block.begin=integer(record.at("begin"),definition.duration);block.end=integer(record.at("end"),definition.duration);block.key=record.at("key").get<std::string>();require(!block.key.empty()&&block.key.size()<=64&&block.key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==block.key.npos,"Action typed key");
        auto kind=record.at("kind").get<std::string>();if(kind=="cue")block.kind=ActionBlockKind::Cue;else if(kind=="hit")block.kind=ActionBlockKind::HitWindow;else if(kind=="invulnerability")block.kind=ActionBlockKind::Invulnerability;else if(kind=="movement-lock")block.kind=ActionBlockKind::MovementLock;else if(kind=="combo")block.kind=ActionBlockKind::ComboWindow;else throw std::runtime_error("Unsupported action block kind");
        require(interval(block.kind)?block.begin<block.end:block.begin==block.end&&block.begin<definition.duration,"Action marker/interval range");definition.blocks.push_back(std::move(block));
    }
    std::sort(definition.blocks.begin(),definition.blocks.end(),[](const auto& a,const auto& b){return std::tie(a.track,a.id)<std::tie(b.track,b.id);});definition.generation=sha256(source.dump());return definition;
}
ActionTimeline::ActionTimeline(std::shared_ptr<const ActionDefinition> definition,std::uint64_t activation,std::uint64_t tick){
    require(definition&&activation&&definition->duration>=action_tick_units&&definition->duration<=600*action_tick_units&&definition->loops&&definition->loops<=8&&definition->blocks.size()<=32&&definition->generation.size()==64,"Action activation definition bounds");
    require(definition->priority<=255&&definition->generation.find_first_not_of("0123456789abcdef")==definition->generation.npos,"Action definition metadata");
    std::set<unsigned> ids;for(const auto& block:definition->blocks)require(block.id&&ids.insert(block.id).second&&block.track<=31&&block.end<=definition->duration&&block.begin<=definition->duration&&static_cast<unsigned>(block.kind)<=static_cast<unsigned>(ActionBlockKind::ComboWindow)&&(interval(block.kind)?block.begin<block.end:block.begin==block.end&&block.begin<definition->duration)&&!block.key.empty()&&block.key.size()<=64&&block.key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==block.key.npos,"Action typed definition block");
    // Definitions are copied so mutable aliases cannot change an active generation.
    definition_=std::make_shared<const ActionDefinition>(*definition);state_.activation=activation;state_.tick=tick;state_.generation=definition_->generation;
}
ActionBatch ActionTimeline::enter(){require(!state_.entered&&state_.phase==ActionPhase::Active&&state_.clock==0,"Action time-zero entry once");ActionBatch batch;for(const auto& block:definition_->blocks)if(!block.begin)batch.events.push_back(event(state_,block,0,0,interval(block.kind)?ActionEdge::Begin:ActionEdge::Marker,definition_->duration));order(batch);state_.entered=true;return batch;}
ActionBatch ActionTimeline::advance(std::uint64_t tick,unsigned rate){
    require(state_.entered&&state_.phase==ActionPhase::Active&&state_.tick<UINT64_MAX&&tick==state_.tick+1&&rate<=4*action_tick_units,"Action fixed tick/rate contract");const auto& definition=*definition_;const auto total=std::uint64_t(definition.duration)*definition.loops;const auto from=state_.clock,to=std::min(total,from+rate);ActionBatch batch;
    for(unsigned loop=0;loop<definition.loops;++loop){auto origin=std::uint64_t(loop)*definition.duration;if(origin>to||origin+definition.duration<from)continue;
        for(const auto& block:definition.blocks){auto begin=origin+block.begin,end=origin+block.end;
            if(begin>from&&begin<=to)batch.events.push_back(event(state_,block,loop,block.begin,interval(block.kind)?ActionEdge::Begin:ActionEdge::Marker,definition.duration));
            if(interval(block.kind)&&end>from&&end<=to)batch.events.push_back(event(state_,block,loop,block.end,ActionEdge::End,definition.duration));
            if(interval(block.kind)&&rate&&std::max(from,begin)<std::min(to,end))batch.traversed.push_back({block.id,loop,static_cast<unsigned>(std::max(from,begin)-origin),static_cast<unsigned>(std::min(to,end)-origin),block.kind});
        }
    }
    order(batch);state_.tick=tick;state_.clock=to;state_.rate=rate;if(to==total)state_.phase=ActionPhase::Completed;return batch;
}
ActionBatch ActionTimeline::cancel(){require(state_.entered&&state_.phase==ActionPhase::Active,"Action cancellation state");ActionBatch batch;auto loop=static_cast<unsigned>(state_.clock/definition_->duration),local=static_cast<unsigned>(state_.clock%definition_->duration);for(const auto& block:definition_->blocks)if(interval(block.kind)&&block.begin<=local&&local<block.end)batch.events.push_back(event(state_,block,loop,local,ActionEdge::End,definition_->duration));order(batch);state_.phase=ActionPhase::Cancelled;return batch;}
void ActionTimeline::restore(const ActionState& candidate){auto total=std::uint64_t(definition_->duration)*definition_->loops;require(candidate.activation==state_.activation&&candidate.generation==definition_->generation&&candidate.rate<=4*action_tick_units&&candidate.clock<=total&&(candidate.entered||candidate.clock==0)&&((candidate.phase==ActionPhase::Active&&candidate.clock<total)||(candidate.phase==ActionPhase::Completed&&candidate.clock==total)||(candidate.phase==ActionPhase::Cancelled&&candidate.entered&&candidate.clock<total)),"Action correction generation/clock/phase");state_=candidate;}
bool ActionCueLedger::accept(const ActionEvent& cue){require(cue.activation&&cue.kind==ActionBlockKind::Cue&&cue.edge==ActionEdge::Marker,"Presentation ledger accepts only cue markers");auto found=std::find_if(seen_.begin(),seen_.end(),[&](const auto& value){return std::tie(value.activation,value.block,value.loop)==std::tie(cue.activation,cue.block,cue.loop);});if(found!=seen_.end())return false;require(seen_.size()<256,"Presentation ledger budget exhausted");seen_.push_back(cue);return true;}
void ActionCueLedger::retire(std::uint64_t activation){std::erase_if(seen_,[&](const auto& event){return event.activation==activation;});}
}
