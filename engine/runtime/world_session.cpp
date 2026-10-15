#include "world_session_impl.hpp"
#include <darkangel/world_session.hpp>
#include "ability_state.hpp"
#include "session_wire.hpp"
#include "owner_mailbox.hpp"
#include "ability_wire.hpp"
#include "effect_wire.hpp"
#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <optional>
#include <tuple>
namespace darkangel {

WorldSession::WorldSession(SessionRole r,SessionHandshake h,SessionLimits l):impl_(std::make_unique<Impl>(r,std::move(h),l)){}
WorldSession::~WorldSession()=default;
void WorldSession::attach(Transport& t,ConnectionHandle h){auto& s=*impl_;s.thread();require(t.limits().payload_bytes>=packet_limit && t.limits().queued_bytes>=packet_limit,"Provider payload/queue too small for session protocol");for(const auto& [handle,peer]:s.peers)require(peer.transport!=&t,"Transport already attached to this session");require(t.valid(h) && h.transport && h.epoch && h.peer && !s.peers.contains(h) && s.peers.size()<s.limits.peers && (s.role==SessionRole::Server || s.peers.empty()),"Invalid session peer");s.peers.emplace(h,Impl::Peer{{},&t,h,SessionReadiness::Handshake,s.timeout_clock()});}
bool WorldSession::attached(ConnectionHandle h)const{impl_->thread();return impl_->peers.contains(h);}
void WorldSession::detach(ConnectionHandle h){impl_->thread();require(impl_->peers.contains(h),"Unknown session peer");impl_->release_ability_control(h);for(auto& [id,q]:impl_->motor_inputs)if(q.owner==h){q.pending.clear();q.last={};q.owner={};}require(impl_->peers.erase(h)==1,"Unknown session peer");if(impl_->role==SessionRole::Client){impl_->objects.clear();impl_->motor_states.clear();impl_->collision_states.clear();impl_->ability_corrections.clear();impl_->public_abilities.clear();impl_->public_resync.clear();impl_->owner_effect_frames.clear();impl_->public_effect_frames.clear();impl_->effect_resync.clear();impl_->ability_ack_pending.clear();impl_->received_ability_notices.clear();impl_->view=impl_->build({});impl_->revision=0;}}
void WorldSession::disconnect(ConnectionHandle h){auto& s=*impl_;s.thread();auto& peer=s.peers.at(h);if(peer.transport->valid(h))peer.transport->disconnect(h);detach(h);}
void WorldSession::tick(){auto& s=*impl_;s.thread();++s.clock;for(auto it=s.peers.begin();it!=s.peers.end();){auto& p=it->second;p.packet_work=0;try{require(p.transport->connected(),"Session peer disconnected");if(!p.collision_parts.empty()&&s.timeout_clock()-p.collision_partial_birth>s.limits.collision_timeout_ticks){p.collision_parts.clear();++p.collision_stats.expired;p.collision_stats.needs_resync=true;}for(auto partial=p.ability_parts.begin();partial!=p.ability_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){partial=p.ability_parts.erase(partial);s.ability_resync=true;}else ++partial;for(auto partial=p.public_parts.begin();partial!=p.public_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){s.public_resync.insert(partial->first);partial=p.public_parts.erase(partial);}else ++partial;for(auto partial=p.effect_parts.begin();partial!=p.effect_parts.end();)if(s.timeout_clock()-partial->second.birth>s.limits.collision_timeout_ticks){s.effect_resync.insert(partial->first);partial=p.effect_parts.erase(partial);}else ++partial;for(const auto& m:p.transport->poll(std::min(s.limits.packets_per_tick,p.transport->limits().packets)))s.receive(p,m);s.pump(p);s.pump_owner(p);require(p.state==SessionReadiness::Ready || s.timeout_clock()-p.progress<=s.limits.timeout_ticks,"Bootstrap timeout");if(s.role==SessionRole::Server&&p.state==SessionReadiness::Ready){s.pump_ability_notices(p);s.pump_interaction_notices(p);s.pump_effects(p);s.pump_abilities(p);s.pump_public_abilities(p);s.pump_public_index(p);s.pump_projectiles(p);{std::vector<std::uint64_t> due;
        // Relevance: the peer's own character and those near it are due every tick, the rest every far_interval-th tick. Near ones go first.
        const MotorState* own=s.owned_motor(p);
        std::vector<std::pair<double,std::uint64_t>> ordered;
        for(const auto& [id,state]:s.motor_states){auto sent=p.motor_sent.find(id);if(sent!=p.motor_sent.end()&&sent->second==std::pair{state.tick,s.revision}&&s.clock%30!=0)continue;double apart=0;
            if(own&&own!=&state){const double dx=state.position.x-own->position.x,dy=state.position.y-own->position.y,dz=state.position.z-own->position.z;apart=std::sqrt(dx*dx+dy*dy+dz*dz);
                if(apart>s.limits.relevance_radius&&s.limits.far_interval>1&&sent!=p.motor_sent.end()&&sent->second.second==s.revision&&(state.tick+id)%s.limits.far_interval!=0&&state.tick-sent->second.first<s.limits.far_interval)continue;}
            ordered.push_back({apart,id});}
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        // A character this peer owns travels as a full state, unless its correction for this tick already carried it; the others as what an observer uses.
        std::vector<std::uint64_t> remote;for(const auto& entry:ordered){const auto id=entry.second;const auto& state=s.motor_states.at(id);auto control=s.motor_inputs.find(id);
            if(control!=s.motor_inputs.end()&&control->second.owner==p.handle){auto carried=p.correction_motor.find(id);if(carried!=p.correction_motor.end()&&carried->second==std::pair{state.tick,s.revision}){p.motor_sent[id]={state.tick,s.revision};continue;}due.push_back(id);}
            else if(own)remote.push_back(id);else due.push_back(id);}
        // Motor states that are due travel packed, three to a packet, instead of one packet each.
        for(std::size_t first=0;first<due.size();first+=motor_batch_states){const auto count=std::min(motor_batch_states,due.size()-first);auto w=header(Kind::MotorBatch,s.hello,s.revision,0,0,count);for(std::size_t item=0;item<count;++item){w.u64(due[first+item]);write_motor(w,s.motor_states.at(due[first+item]));}require(w.bytes.size()<=packet_limit,"Motor batch packet budget");if(!s.send(p,std::move(w),Delivery::UnreliableState))break;for(std::size_t item=0;item<count;++item)p.motor_sent[due[first+item]]={s.motor_states.at(due[first+item]).tick,s.revision};}
        for(std::size_t first=0;first<remote.size();){auto w=header(Kind::MotorRemote,s.hello,s.revision,s.motor_states.at(remote[first]).topology,0,0);std::size_t count=0;
            for(;first+count<remote.size()&&count<motor_remote_states;++count){const auto id=remote[first+count];const auto& state=s.motor_states.at(id);if(count&&state.topology!=s.motor_states.at(remote[first]).topology)break;const bool carried=state.support_velocity.x!=0||state.support_velocity.y!=0||state.support_velocity.z!=0;if(w.bytes.size()+(carried?128:104)>packet_limit)break;
                w.u64(id);w.u64(state.tick);w.u64(state.epoch);w.u64(state.action);w.u64((state.grounded?1:0)|(state.crouched?2:0)|(carried?4:0));write_vector(w,state.position);w.number(state.yaw);w.number(state.pitch);write_vector(w,state.achieved);if(carried)write_vector(w,state.support_velocity);}
            // The count is known only after packing: it is the header's last field.
            for(unsigned byte=0;byte<8;++byte)w.bytes[48+byte]=static_cast<std::byte>((std::uint64_t(count)>>(byte*8))&255);
            require(count&&w.bytes.size()<=packet_limit,"Remote motor packet budget");if(!s.send(p,std::move(w),Delivery::UnreliableState))break;for(std::size_t item=0;item<count;++item)p.motor_sent[remote[first+item]]={s.motor_states.at(remote[first+item]).tick,s.revision};first+=count;}}s.pump_collision(p);}++it;}catch(...){if(p.transport->valid(p.handle))p.transport->disconnect(p.handle);s.release_ability_control(p.handle);for(auto& [id,q]:s.motor_inputs)if(q.owner==p.handle){q.pending.clear();q.last={};q.owner={};}it=s.peers.erase(it);if(s.role==SessionRole::Client){s.objects.clear();s.motor_states.clear();s.collision_states.clear();s.ability_corrections.clear();s.public_abilities.clear();s.public_index.clear();s.public_resync.clear();s.owner_effect_frames.clear();s.public_effect_frames.clear();s.effect_resync.clear();s.ability_ack_pending.clear();s.received_ability_notices.clear();s.view=s.build({});s.revision=0;}throw;}}}
std::uint64_t WorldSession::create(ObjectData o){auto& s=*impl_;s.authority();require(s.objects.size()<s.limits.objects && s.next_id<std::numeric_limits<std::uint64_t>::max(),"Session identity/capacity exhausted");require(!o.network.value && !o.target && o.scripts.records.empty() && o.scripts.count==0 && o.optional_json.empty(),"Unsupported replicated object fields");o.network.value=s.next_id;auto candidate=s.objects;candidate.emplace(s.next_id,o);s.view->create(o);s.objects=std::move(candidate);++s.revision;return s.next_id++;}
void WorldSession::move(std::uint64_t id,const Transform& t){auto& s=*impl_;s.authority();require(s.objects.contains(id),"Unknown network identity");require(!s.motor_states.contains(id),"Living character movement requires motor publication");auto candidate=s.objects;candidate.at(id).transform=t;s.view->set_transform(s.view->find(candidate.at(id).id),t,Authority::Server);s.objects=std::move(candidate);++s.revision;}
void WorldSession::destroy(std::uint64_t id){
    auto& s=*impl_;s.authority();auto candidate=s.objects;require(candidate.erase(id)==1,"Unknown network identity");s.teams.erase(id);
    std::vector<AbilityActionUpdate> cleanup_updates;auto ability=s.abilities.find(id);std::unique_ptr<AbilityState> retired;
    if(ability!=s.abilities.end()){
        retired=std::make_unique<AbilityState>(*ability->second);auto snapshot=retired->snapshot();
        if(snapshot.active){auto cleanup=retired->cancel(*snapshot.active,AbilityActionReason::Despawned);cleanup_updates=std::move(cleanup.second);}
    }
    std::map<std::uint64_t,std::unique_ptr<AbilityState>> affected;
    for(const auto& [network,old]:s.abilities)if(network!=id){auto state=std::make_unique<AbilityState>(*old);state->source_destroyed(s.hello.session_epoch,id);auto cleanup=state->damage(0);cleanup_updates.insert(cleanup_updates.end(),cleanup.begin(),cleanup.end());affected.emplace(network,std::move(state));}
    auto queue=s.queue_actions(cleanup_updates);std::vector<AbilityState*> states;for(const auto& [network,state]:affected)states.push_back(state.get());if(retired)states.push_back(retired.get());auto commitments=s.queue_commitments(states);
    s.view->destroy(s.view->find(s.objects.at(id).id),Authority::Server);
    for(auto& [network,state]:affected){auto health=state->health();s.view->apply_server_health(state->owner().entity,health);candidate.at(network).health=health;s.abilities.at(network)=std::move(state);}
    s.objects=std::move(candidate);std::erase_if(s.owner_messages,[&](const auto& message){return message.network==id;});s.motor_inputs.erase(id);s.motor_states.erase(id);s.abilities.erase(id);s.ability_inputs.erase(id);s.ability_actions=std::move(queue);s.ability_commitments=std::move(commitments);++s.revision;
}
const World& WorldSession::world() const{impl_->thread();return *impl_->view;}
const std::map<std::uint64_t,ObjectData>& WorldSession::objects() const{impl_->thread();return impl_->objects;}
std::uint64_t WorldSession::revision() const{impl_->thread();return impl_->revision;}
SessionReadiness WorldSession::readiness(ConnectionHandle h) const{impl_->thread();return impl_->peers.at(h).state;}
}

namespace darkangel {
void WorldSession::own_motor(std::uint64_t id,ConnectionHandle peer,std::uint64_t consumed_tick){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&s.peers.contains(peer)&&s.motor_inputs.size()<4&&!s.motor_inputs.contains(id),"Invalid motor ownership");auto& queue=s.motor_inputs[id];queue.owner=peer;queue.consumed_tick=consumed_tick;queue.last.sequence=consumed_tick;}
bool WorldSession::submit_motor(std::uint64_t id,const MotorInput& input){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.objects.contains(id)&&s.peers.size()==1,"Invalid client motor request");validate_motor_input(input);require(input.sequence>0,"Invalid owner sequence");auto& p=s.peers.begin()->second;require(p.state==SessionReadiness::Ready,"Motor request before Ready");if(!collision_control_ready())return false;auto w=header(Kind::MotorCommand,s.hello,0,id);w.u64(input.sequence);w.u64(input.tick);w.u64(input.epoch);w.number(input.x);w.number(input.z);w.number(input.yaw);w.u64(input.jump?1:0);w.u64(input.crouch?1:0);w.number(input.pitch);return s.send(p,std::move(w));}
bool WorldSession::motor_input_ready(std::uint64_t id,std::uint64_t tick)const{auto& s=*impl_;s.thread();return s.motor_inputs.at(id).pending.contains(tick);}
MotorInput WorldSession::consume_motor(std::uint64_t id,std::uint64_t tick,std::uint64_t epoch){auto& s=*impl_;s.authority();auto& q=s.motor_inputs.at(id);require(tick==q.consumed_tick+1&&epoch>0,"Movement consumes exactly one fixed step");q.consumed_tick=tick;MotorInput result{q.last.sequence,tick,epoch};auto it=q.pending.find(tick);if(it!=q.pending.end()){if(it->second.epoch==epoch)result=it->second;q.pending.erase(it);}for(auto i=q.pending.begin();i!=q.pending.end()&&i->first<tick;)i=q.pending.erase(i);q.last=result;return result;}
void WorldSession::publish_motor(std::uint64_t id,const MotorState& state){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&(s.motor_states.contains(id)||s.motor_states.size()<session_character_capacity),"Invalid motor publication");validate_motor_state(state);if(s.motor_states.contains(id)){auto& old=s.motor_states.at(id);require(state.epoch>old.epoch||(state.epoch==old.epoch&&state.tick>old.tick),"Motor snapshot order");}auto transform=s.objects.at(id).transform;transform.x=state.position.x;transform.y=state.position.y;transform.z=state.position.z;transform.yaw=state.yaw;s.view->set_transform(s.view->find(s.objects.at(id).id),transform,Authority::Server);s.objects.at(id).transform=transform;if(s.motor_states.contains(id)&&state.epoch>s.motor_states.at(id).epoch&&s.motor_inputs.contains(id)){auto& q=s.motor_inputs.at(id);q.pending.clear();q.last={};q.received_sequence=0;}s.motor_states[id]=state;}
const std::map<std::uint64_t,MotorState>& WorldSession::motors()const{impl_->thread();return impl_->motor_states;}
}

namespace darkangel {


}

namespace darkangel {


}

namespace darkangel {

}

namespace darkangel {









double WorldSession::ability_available(AbilityOwnerHandle owner,AttributeId id)const{return impl_->ability(owner).available(id);}



}

namespace darkangel {















}

namespace darkangel {
std::uint64_t WorldSession::packets_sent(ConnectionHandle peer)const{impl_->thread();auto found=impl_->peers.find(peer);require(found!=impl_->peers.end(),"Unknown session peer");return found->second.packets_sent;}




std::vector<std::uint64_t> WorldSession::packets_by_kind(ConnectionHandle peer)const{impl_->thread();auto found=impl_->peers.find(peer);require(found!=impl_->peers.end(),"Unknown session peer");return {found->second.packets_by_kind.begin(),found->second.packets_by_kind.end()};}







}

namespace darkangel {






}

namespace darkangel {



}
