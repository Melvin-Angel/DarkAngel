#include <darkangel/effect_replication.hpp>
#include "effect_wire.hpp"
#include "session_wire.hpp"
#include <algorithm>
#include <set>
namespace darkangel {
namespace {
void checked(bool b,const char* error){if(!b)throw std::invalid_argument(error);}
void shape(const EffectFrame& frame){
 checked(frame.owner.network&&frame.owner.session_epoch&&frame.avatar_epoch&&(frame.audience==AttributeVisibility::Owner||frame.audience==AttributeVisibility::Public)&&frame.effects.size()<=64,"Effect frame identity/audience/bound");std::set<std::uint64_t> ids;
 for(const auto& effect:frame.effects)checked(effect.handle&&effect.handle<(std::uint64_t{1}<<63)&&ids.insert(effect.handle).second&&effect.definition!=AssetId{}&&effect.generation.size()==64&&effect.generation.find_first_not_of("0123456789abcdef")==effect.generation.npos&&effect.credit.session_epoch==frame.owner.session_epoch&&effect.credit.source_network&&effect.start<=frame.tick&&(!effect.end||effect.end>frame.tick)&&(!effect.next_period||effect.next_period>frame.tick),"Effect replica identity/generation/clock");
}
}
EffectPresentation::EffectPresentation(AttributeVisibility audience,const AttributeSet& attributes,std::shared_ptr<const TagDictionary> tags,std::span<const std::shared_ptr<const EffectDefinition>> definitions):audience_(audience){
 checked(tags&&(audience==AttributeVisibility::Owner||audience==AttributeVisibility::Public)&&definitions.size()<=128,"Effect presentation preparation bound");
 for(const auto& definition:definitions){checked(bool(definition),"Missing prepared effect");auto frozen=freeze_effect_definition(*definition,attributes,*tags);checked(definitions_.emplace(frozen->id,std::move(frozen)).second,"Duplicate prepared effect");}
}
bool EffectPresentation::push(const EffectFrame& frame){
 shape(frame);checked(frame.audience==audience_,"Effect presentation audience mismatch");
 if(current_){const auto& old=*current_;checked(frame.owner.network==old.owner.network&&frame.owner.session_epoch==old.owner.session_epoch,"Effect presentation owner changed; prepare a new lifecycle");if(frame.world_revision<old.world_revision||frame.avatar_epoch<old.avatar_epoch||(frame.world_revision==old.world_revision&&frame.avatar_epoch==old.avatar_epoch&&frame.revision<old.revision))return false;}
 for(const auto& replica:frame.effects){auto found=definitions_.find(replica.definition);checked(found!=definitions_.end()&&found->second->generation==replica.generation,"Effect presentation frozen generation unavailable");const auto& definition=*found->second;
  checked(definition.lifetime!=EffectLifetime::Instant&&(definition.visibility==AttributeVisibility::Public||(audience_==AttributeVisibility::Owner&&definition.visibility==AttributeVisibility::Owner)),"Effect presentation private/instant state");
  checked((definition.lifetime==EffectLifetime::Finite)==bool(replica.end)&&(!replica.end||(replica.end-replica.start>=definition.duration_ticks&&(definition.stacking==EffectStack::RefreshPerSource||replica.end-replica.start==definition.duration_ticks)&&replica.end-frame.tick<=definition.duration_ticks))&&bool(definition.period_ticks)==bool(replica.next_period)&&(!replica.next_period||((replica.next_period-replica.start)%definition.period_ticks==0&&replica.next_period-frame.tick<=definition.period_ticks)),"Effect presentation lifetime/period mismatch");
 }
 if(current_&&frame.world_revision==current_->world_revision&&frame.avatar_epoch==current_->avatar_epoch){checked(frame.tick>=current_->tick,"Effect presentation tick regressed");if(frame.revision==current_->revision){checked(frame.tick==current_->tick&&frame.effects==current_->effects,"Conflicting effect presentation revision");return false;}}
 auto previous=current_?cue_states(*current_):std::map<EffectCueKey,EffectCueState>{};auto next=cue_states(frame);auto updates=cue_updates_;
 for(const auto& [id,state]:previous)if(!next.contains(id))updates.push_back({EffectCueEdge::End,state});
 for(const auto& [id,state]:next){auto old=previous.find(id);if(old==previous.end())updates.push_back({EffectCueEdge::Begin,state});else if(old->second!=state)updates.push_back({EffectCueEdge::Update,state});}
 checked(updates.size()<=512,"Persistent effect cue queue full; drain before publication");auto prepared=frame;current_=std::move(prepared);cue_updates_=std::move(updates);return true;
}
std::map<EffectCueKey,EffectCueState> EffectPresentation::cue_states(const EffectFrame& frame)const{
 std::map<EffectCueKey,EffectCueState> result;for(const auto& effect:frame.effects)if(!effect.suppressed)for(const auto& cue:definitions_.at(effect.definition)->cues){EffectCueKey id{frame.owner.session_epoch,frame.owner.network,frame.avatar_epoch,effect.handle,cue.id};result.emplace(id,EffectCueState{id,frame.owner,effect.credit,effect.start,effect.end,effect.generation,cue.key});}return result;
}
std::vector<EffectCueState> EffectPresentation::cues()const{std::vector<EffectCueState> result;if(current_)for(const auto& [id,state]:cue_states(*current_))result.push_back(state);return result;}
std::vector<EffectCueUpdate> EffectPresentation::drain_cues(){auto result=std::move(cue_updates_);cue_updates_.clear();return result;}
namespace session_detail {
std::vector<std::byte> encode_effect_frame(const EffectFrame& frame){
 shape(frame);Writer w;w.u64(1);for(auto value:{frame.owner.network,frame.owner.session_epoch,frame.world_revision,frame.avatar_epoch,frame.tick,frame.revision,std::uint64_t(frame.audience),std::uint64_t(frame.effects.size())})w.u64(value);
 for(const auto& effect:frame.effects){w.u64(effect.handle);for(auto b:effect.definition.bytes)w.bytes.push_back(static_cast<std::byte>(b));for(auto c:effect.generation)w.bytes.push_back(static_cast<std::byte>(c));for(auto value:{effect.credit.session_epoch,effect.credit.source_network,effect.credit.activation,effect.start,effect.end,effect.next_period,std::uint64_t(effect.suppressed)})w.u64(value);}
 checked(w.bytes.size()<=16384,"Effect frame byte bound");return std::move(w.bytes);
}
EffectFrame decode_effect_frame(std::span<const std::byte> bytes){
 checked(bytes.size()<=16384,"Effect frame decode byte bound");Reader r{bytes};checked(r.u64()==1,"Effect frame schema");EffectFrame frame;frame.owner.network=r.u64();frame.owner.session_epoch=r.u64();frame.world_revision=r.u64();frame.avatar_epoch=r.u64();frame.tick=r.u64();frame.revision=r.u64();auto audience=r.u64(),count=r.u64();checked((audience==1||audience==2)&&count<=64,"Effect frame audience/count");frame.audience=static_cast<AttributeVisibility>(audience);
 for(std::uint64_t i=0;i<count;++i){EffectReplica effect;effect.handle=r.u64();checked(r.bytes.size()-r.at>=80,"Truncated effect generation");for(auto& b:effect.definition.bytes)b=std::to_integer<unsigned char>(r.bytes[r.at++]);for(unsigned n=0;n<64;++n)effect.generation.push_back(static_cast<char>(r.bytes[r.at++]));effect.credit.session_epoch=r.u64();effect.credit.source_network=r.u64();effect.credit.activation=r.u64();effect.start=r.u64();effect.end=r.u64();effect.next_period=r.u64();auto suppressed=r.u64();checked(suppressed<=1,"Effect suppression flag");effect.suppressed=suppressed!=0;frame.effects.push_back(std::move(effect));}
 r.end();shape(frame);return frame;
}
}
}
