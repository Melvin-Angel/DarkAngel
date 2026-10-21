#include <darkangel/cue_player.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace darkangel {
namespace {void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}}
CuePlayer::CuePlayer(Resolver resolver,CueSink* visual,CueSink* audio,CueLimits limits):resolver_(std::move(resolver)),visual_(visual),audio_(audio),limits_(limits){
    require(bool(resolver_)&&limits_.live>=1&&limits_.live<=4096&&limits_.tail_ticks<=3600&&limits_.rejected_tail_ticks<=limits_.tail_ticks,"Cue player resolver/limits");
}
CuePlayer::~CuePlayer(){while(!instances_.empty())release(instances_.begin());}
void CuePlayer::release(std::map<Source,Instance>::iterator found){
    if(found->second.visual&&visual_)visual_->release(found->second.id);if(found->second.audio&&audio_)audio_->release(found->second.id);instances_.erase(found);
}
void CuePlayer::stop(std::map<Source,Instance>::iterator found,unsigned tail,std::uint64_t tick){
    auto& instance=found->second;if(instance.stopping)return;tail=std::min(tail,limits_.tail_ticks);instance.stopping=true;instance.until=tick+tail;
    if(instance.visual&&visual_)visual_->stop(instance.id,tail);if(instance.audio&&audio_)audio_->stop(instance.id,tail);if(!tail)release(found);
}
bool CuePlayer::start(const Source& source,std::string_view key,AssetId cue,std::uint64_t tick){
    // The same source starting again while it is live is one cue, not two.
    if(auto held=instances_.find(source);held!=instances_.end()){if(!held->second.stopping)return false;release(held);}
    const auto definition=resolver_(key,cue);if(!definition||(!definition->visible()&&definition->audio.empty())){++unresolved_;return false;}
    if(instances_.size()>=limits_.live){
        // Room is taken from the fading instance that would end first; a player full of live cues drops the new one.
        auto oldest=instances_.end();for(auto at=instances_.begin();at!=instances_.end();++at)if(at->second.stopping&&(oldest==instances_.end()||at->second.until<oldest->second.until))oldest=at;
        if(oldest==instances_.end()){++dropped_;return false;}release(oldest);}
    Instance instance;instance.id=next_++;instance.started=tick;instance.key=std::string(key);instance.visual=definition->visible();instance.audio=!definition->audio.empty();
    const CueStart begun{instance.id,source.network,tick,instance.key,*definition};instances_.emplace(source,instance);
    if(instance.visual&&visual_)visual_->start(begun);if(instance.audio&&audio_)audio_->start(begun);return true;
}
void CuePlayer::effect(const EffectCueUpdate& update,std::uint64_t tick){
    const auto& id=update.state.id;Source source;source.session_epoch=id.session_epoch;source.network=id.network;source.avatar_epoch=id.avatar_epoch;source.handle=id.handle;source.cue=id.cue;
    if(update.edge==EffectCueEdge::Begin)start(source,update.state.key,id.cue,tick);
    else if(update.edge==EffectCueEdge::End){if(auto found=instances_.find(source);found!=instances_.end()){const auto definition=resolver_(found->second.key,id.cue);stop(found,definition?definition->tail_ticks:0,tick);}}
}
void CuePlayer::action(std::uint64_t network,std::uint64_t avatar_epoch,const ActionEvent& event,std::uint64_t tick,CueTiming timing){
    if(event.kind!=ActionBlockKind::Cue&&event.kind!=ActionBlockKind::Vfx&&event.kind!=ActionBlockKind::Audio)return;require(network&&event.activation,"An action cue needs its actor and activation");
    // The owner's action is fed twice; a cue belongs to exactly one of the two streams.
    if(timing!=CueTiming::Observed){const auto definition=resolver_(event.key,AssetId{});if((timing==CueTiming::OwnerPredicted)!=(definition&&definition->predictable))return;}
    Source source;source.network=network;source.avatar_epoch=avatar_epoch;source.activation=event.activation;source.block=event.block;source.loop=event.loop;source.action=true;
    const auto tail=[&]{const auto definition=resolver_(event.key,AssetId{});return definition?definition->tail_ticks:0u;};
    if(event.edge==ActionEdge::Begin)start(source,event.key,AssetId{},tick);
    else if(event.edge==ActionEdge::End){if(auto found=instances_.find(source);found!=instances_.end())stop(found,tail(),tick);}
    // A marker has no length: it plays once and fades by its own tail.
    else if(start(source,event.key,AssetId{},tick))stop(instances_.find(source),tail(),tick);
}
void CuePlayer::moment(std::uint64_t network,std::uint64_t avatar_epoch,std::string_view key,std::uint64_t serial,std::uint64_t tick){
    require(network&&serial&&!key.empty(),"A cue moment needs its actor, its serial and a key");Source source;source.session_epoch=~std::uint64_t{};source.network=network;source.avatar_epoch=avatar_epoch;source.handle=serial;
    // The same moment again, even while it fades, is not a second one.
    if(instances_.contains(source))return;
    if(start(source,key,AssetId{},tick)){const auto definition=resolver_(key,AssetId{});stop(instances_.find(source),definition?definition->tail_ticks:0u,tick);}
}
FootstepTracker::FootstepTracker(double stride):stride_(stride){require(std::isfinite(stride)&&stride>=.2&&stride<=5,"Footstep stride bounds");}
void FootstepTracker::observe(std::uint64_t network,std::uint64_t avatar_epoch,double x,double z,bool grounded){
    require(network!=0&&std::isfinite(x)&&std::isfinite(z),"A footstep stream needs its actor and a finite position");auto found=walks_.find(network);
    if(found==walks_.end()||found->second.avatar_epoch!=avatar_epoch){walks_[network]=Walk{avatar_epoch,0,x,z,0};return;}auto& walk=found->second;const double moved=std::hypot(x-walk.x,z-walk.z);walk.x=x;walk.z=z;
    if(!grounded||moved>1)return;walk.covered+=moved;while(walk.covered>=stride_){walk.covered-=stride_;steps_.push_back({network,avatar_epoch,++walk.serial});require(steps_.size()<=1024,"Footstep work bound");}
}
void FootstepTracker::retire(std::uint64_t network){walks_.erase(network);}
std::vector<FootstepUpdate> FootstepTracker::drain(){return std::exchange(steps_,{});}
void CuePlayer::rejected(std::uint64_t network,std::uint64_t activation,std::uint64_t tick){
    for(auto at=instances_.begin();at!=instances_.end();){auto next=std::next(at);if(at->first.action&&at->first.network==network&&at->first.activation==activation){
            // Cut short even when it was already fading on its own, longer tail.
            if(at->second.stopping){at->second.until=std::min(at->second.until,tick+limits_.rejected_tail_ticks);if(at->second.visual&&visual_)visual_->stop(at->second.id,limits_.rejected_tail_ticks);if(at->second.audio&&audio_)audio_->stop(at->second.id,limits_.rejected_tail_ticks);}
            else stop(at,limits_.rejected_tail_ticks,tick);}
        at=next;}
}
void CuePlayer::retire(std::uint64_t network){
    for(auto at=instances_.begin();at!=instances_.end();){auto next=std::next(at);if(at->first.network==network)release(at);at=next;}
}
void CuePlayer::advance(std::uint64_t tick){
    for(auto at=instances_.begin();at!=instances_.end();){auto next=std::next(at);if(at->second.stopping&&tick>=at->second.until)release(at);at=next;}
}
ActionCueTracker::ActionCueTracker(std::span<const std::shared_ptr<const ActionDefinition>> actions){
    require(actions.size()<=256,"Cue tracker action bound");for(const auto& action:actions){require(action&&action->duration&&action->loops,"Cue tracker action resource");actions_.emplace(action->id,action);}
}
void ActionCueTracker::markers(std::uint64_t network,CueTiming timing,const Held& held,std::uint64_t from,std::uint64_t to,bool inclusive){
    const auto& definition=*held.definition;const auto first=cues_.size();
    for(unsigned loop=0;loop<definition.loops;++loop){const auto origin=std::uint64_t(loop)*definition.duration;if(origin>to||origin+definition.duration<from)continue;
        for(const auto& block:definition.blocks){if(block.kind!=ActionBlockKind::Cue&&block.kind!=ActionBlockKind::Vfx&&block.kind!=ActionBlockKind::Audio)continue;const auto begin=origin+block.begin,end=origin+block.end;
            if(block.kind==ActionBlockKind::Cue){if(begin>to||(inclusive?begin<from:begin<=from))continue;cues_.push_back({network,held.avatar_epoch,timing,ActionEvent{held.activation,begin,block.id,block.track,loop,ActionEdge::Marker,block.kind,block.key}});continue;}
            if((begin>from||(inclusive&&begin==from))&&begin<=to)cues_.push_back({network,held.avatar_epoch,timing,ActionEvent{held.activation,begin,block.id,block.track,loop,ActionEdge::Begin,block.kind,block.key}});
            if((end>from||(inclusive&&end==from))&&end<=to)cues_.push_back({network,held.avatar_epoch,timing,ActionEvent{held.activation,end,block.id,block.track,loop,ActionEdge::End,block.kind,block.key}});
        }}
    std::sort(cues_.begin()+std::ptrdiff_t(first),cues_.end(),[](const auto& a,const auto& b){return a.event<b.event;});require(cues_.size()<=1024,"Cue tracker work bound");
}
void ActionCueTracker::finish(std::uint64_t network,CueTiming timing,const Held& held){
    const auto total=std::uint64_t(held.definition->duration)*held.definition->loops;if(held.clock>=total)return;
    // One more tick would have ended it: it ran out, and the cues of its last tick are still due. Anything else was cut short.
    if(held.rate&&held.clock+held.rate>=total)markers(network,timing,held,held.clock,total,false);else{cuts_.push_back({network,held.activation});require(cuts_.size()<=1024,"Cue tracker work bound");}
}
void ActionCueTracker::observe(std::uint64_t network,std::uint64_t avatar_epoch,CueTiming timing,AssetId id,const std::optional<ActionState>& state){
    require(network!=0,"A cue tracker stream needs its actor");const auto key=std::pair{network,timing};const auto definition=actions_.find(id);
    const bool running=state&&state->entered&&state->phase!=ActionPhase::Cancelled&&definition!=actions_.end();auto held=held_.find(key);
    if(held!=held_.end()&&(!running||held->second.activation!=state->activation||held->second.avatar_epoch!=avatar_epoch||held->second.definition!=definition->second)){
        if(running&&held->second.activation==state->activation&&held->second.avatar_epoch==avatar_epoch&&held->second.definition!=definition->second){
            const auto& previous=*held->second.definition;const auto local=held->second.clock%previous.duration;const auto loop=static_cast<unsigned>(held->second.clock/previous.duration);
            for(const auto& block:previous.blocks)if((block.kind==ActionBlockKind::Vfx||block.kind==ActionBlockKind::Audio)&&block.begin<=local&&local<block.end)cues_.push_back({network,avatar_epoch,timing,ActionEvent{held->second.activation,held->second.clock,block.id,block.track,loop,ActionEdge::End,block.kind,block.key}});
        }
        // A new avatar is a new actor to presentation: the host retires its cues, nothing is reported here.
        if(held->second.avatar_epoch==avatar_epoch&&!(running&&held->second.activation==state->activation&&held->second.definition!=definition->second))finish(network,timing,held->second);held_.erase(held);held=held_.end();}
    if(!running)return;const auto total=std::uint64_t(definition->second->duration)*definition->second->loops;const auto clock=std::min<std::uint64_t>(state->clock,total);
    if(held==held_.end()){Held begun{avatar_epoch,state->activation,clock,state->rate,definition->second};if(clock<=4*std::uint64_t(action_tick_units))markers(network,timing,begun,0,clock,true);held_.emplace(key,std::move(begun));return;}
    if(clock>held->second.clock){markers(network,timing,held->second,held->second.clock,clock,false);held->second.clock=clock;}held->second.rate=state->rate;
}
void ActionCueTracker::retire(std::uint64_t network){std::erase_if(held_,[&](const auto& item){return item.first.first==network;});}
std::vector<ActionCueUpdate> ActionCueTracker::drain_cues(){return std::exchange(cues_,{});}
std::vector<ActionCueCut> ActionCueTracker::drain_cuts(){return std::exchange(cuts_,{});}
CueDefinition decode_cue(std::string_view text){
    using Json=nlohmann::json;require(text.size()<=4096,"Cue source size bound");const auto value=Json::parse(text);
    const bool cooked=value.is_object()&&value.contains("visual_asset");require(value.is_object(),"Cue source field count");std::size_t known=9;for(auto field:{"schema","kind","asset","name","key","visual","audio","tail_ticks","predictable"})require(value.contains(field),"Cue source missing/unknown field");
    for(auto field:{"trail","decal","socket","scale","offset"})if(value.contains(field))++known;if(cooked){require(value.contains("audio_asset"),"Cue source field count");known+=2;for(auto field:{"trail_asset","decal_asset"})if(value.contains(field))++known;}require(known==value.size(),"Cue source missing/unknown field");
    require(value.at("schema")==1&&value.at("kind")=="cue","Cue source schema/type");CueDefinition cue;cue.id=AssetId::parse(value.at("asset").get<std::string>());require(cue.id!=AssetId{},"Cue identity");
    const auto word=[&](const char* field,std::size_t limit,bool empty,const char* letters,const char* error){require(value.at(field).is_string(),error);auto read=value.at(field).get<std::string>();require((empty||!read.empty())&&read.size()<=limit&&read.find_first_not_of(letters)==read.npos,error);return read;};
    const char* typed="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-";const char* path="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-/";
    require(value.at("name").is_string(),"Cue name type");cue.name=value.at("name").get<std::string>();require(!cue.name.empty()&&cue.name.size()<=64,"Cue display name bounds (1 to 64 characters)");
    cue.key=word("key",64,false,typed,"Cue key: 1 to 64 letters, digits, '_', '.' or '-'");cue.visual=word("visual",128,true,path,"Cue visual name");cue.audio=word("audio",128,true,path,"Cue audio name");
    if(value.contains("trail"))cue.trail=word("trail",128,true,path,"Cue trail name");if(value.contains("decal"))cue.decal=word("decal",128,true,path,"Cue decal name");if(value.contains("socket"))cue.socket=word("socket",64,true,typed,"Cue socket name");
    if(value.contains("scale")){require(value.at("scale").is_number()&&std::isfinite(value.at("scale").get<double>())&&value.at("scale").get<double>()>=.01&&value.at("scale").get<double>()<=10,"Cue scale bounds");cue.scale=value.at("scale").get<float>();}
    if(value.contains("offset")){require(value.at("offset").is_array()&&value.at("offset").size()==3,"Cue local offset shape");for(unsigned axis=0;axis<3;++axis){require(value.at("offset")[axis].is_number()&&std::isfinite(value.at("offset")[axis].get<double>())&&std::abs(value.at("offset")[axis].get<double>())<=10,"Cue local offset bounds");cue.offset[axis]=value.at("offset")[axis].get<float>();}}
    require(cue.visible()||!cue.audio.empty(),"A cue names an effect, a sound, a trail or a decal");
    require(value.at("tail_ticks").is_number_unsigned()&&value.at("tail_ticks").get<std::uint64_t>()<=600,"Cue tail bounds (0 to 600 ticks)");cue.tail_ticks=value.at("tail_ticks").get<unsigned>();
    require(value.at("predictable").is_boolean(),"Cue predictable flag type");cue.predictable=value.at("predictable").get<bool>();
    if(cooked){const auto resolved=[&](const char* field,const std::string& name){require(value.at(field).is_string(),"Cooked cue reference type");const auto text=value.at(field).get<std::string>();require(text.empty()==name.empty(),"Cooked cue reference does not match its name");return text.empty()?AssetId{}:AssetId::parse(text);};cue.visual_asset=resolved("visual_asset",cue.visual);cue.audio_asset=resolved("audio_asset",cue.audio);require(value.contains("trail_asset")==!cue.trail.empty()&&value.contains("decal_asset")==!cue.decal.empty(),"Cooked cue reference does not match its name");if(!cue.trail.empty())cue.trail_asset=resolved("trail_asset",cue.trail);if(!cue.decal.empty())cue.decal_asset=resolved("decal_asset",cue.decal);}
    return cue;
}
std::size_t CuePlayer::live()const{return instances_.size();}
std::vector<CueInstanceInfo> CuePlayer::instances()const{
    std::vector<CueInstanceInfo> result;for(const auto& [source,instance]:instances_)result.push_back({instance.id,source.network,instance.started,instance.stopping?instance.until:0,instance.key,source.action});
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.instance<b.instance;});return result;
}
}
