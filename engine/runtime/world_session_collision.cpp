#include "world_session_impl.hpp"

namespace darkangel {
auto WorldSession::Impl::collision_chunk_starts(const CollisionStreamFrame& frame) -> std::vector<std::size_t> {
        constexpr std::size_t payload=packet_limit-header_size-16;std::vector<std::size_t> starts{0};std::size_t used=0;const auto total=collision_items(frame);
        for(std::size_t item=0;item<total;++item){Writer w;write_collision_item(w,frame,item);if(used&&used+w.bytes.size()>payload){starts.push_back(item);used=0;}used+=w.bytes.size();}
        return starts;
    }

auto WorldSession::Impl::owned_motor(const Peer& p)const -> const MotorState* {for(const auto& [id,queue]:motor_inputs)if(queue.owner==p.handle){auto found=motor_states.find(id);if(found!=motor_states.end())return &found->second;}return nullptr;}

auto WorldSession::Impl::pump_collision(Peer& p) -> void {
        if(!latest_collision||p.state!=SessionReadiness::Ready)return;for(const auto& actor:latest_collision->actors)if(!objects.contains(actor.id))return;
        if(p.collision_sending&&p.collision_revision!=revision){p.collision_sending.reset();p.collision_next=0;}
        if(!p.collision_sending){if(p.collision_sent&&p.collision_last_tick==latest_collision->tick&&p.collision_last_revision==revision&&clock%30!=0)return;p.collision_sending=*latest_collision;p.collision_revision=revision;p.collision_next=0;
            // Character bodies by reach: a peer that owns a character gets the bodies near it, its own always.
            if(const auto* own=owned_motor(p))std::erase_if(p.collision_sending->actors,[&](const CollisionActor& actor){auto control=motor_inputs.find(actor.id);if(control!=motor_inputs.end()&&control->second.owner==p.handle)return false;const double dx=actor.foot.x-own->position.x,dy=actor.foot.y-own->position.y,dz=actor.foot.z-own->position.z;return std::sqrt(dx*dx+dy*dy+dz*dz)>limits.body_radius;});}
        const auto& frame=*p.collision_sending;auto total=collision_items(frame);const auto starts=collision_chunk_starts(frame);const auto chunks=starts.size();
        for(unsigned work=0;work<limits.packets_per_tick&&p.collision_next<chunks;++work){auto w=header(Kind::CollisionSnapshot,hello,p.collision_revision,frame.tick,chunks,total);w.u64(frame.topology);w.u64(p.collision_next);for(auto item=starts[p.collision_next];item<(p.collision_next+1<chunks?starts[p.collision_next+1]:total);++item)write_collision_item(w,frame,item);require(w.bytes.size()<=packet_limit,"Collision packet encoding budget");auto encoded_bytes=w.bytes.size();if(!send(p,std::move(w),Delivery::UnreliableState))return;++p.collision_stats.sent_packets;p.collision_stats.sent_bytes+=encoded_bytes;++p.collision_next;}
        if(p.collision_next==chunks){p.collision_last_tick=frame.tick;p.collision_last_revision=revision;p.collision_sent=true;auto sent=std::tuple{frame.tick,p.collision_revision,frame.topology};if(p.collision_sent_frames.empty()||p.collision_sent_frames.back()!=sent)p.collision_sent_frames.push_back(sent);while(p.collision_sent_frames.size()>31)p.collision_sent_frames.pop_front();p.collision_sending.reset();}
    }

void WorldSession::publish_collision(CollisionStreamFrame frame){auto& s=*impl_;s.authority();validate_collision_stream(frame);for(const auto& actor:frame.actors)require(s.objects.contains(actor.id),"Collision actor not replicated");require(!s.latest_collision||frame.tick>s.latest_collision->tick,"Collision publication tick must advance");s.latest_collision=std::move(frame);}

const std::deque<CollisionStreamFrame>& WorldSession::collisions()const{impl_->thread();return impl_->collision_states;}

void WorldSession::acknowledge_collision(std::uint64_t tick){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1,"Collision ACK requires client");auto& p=s.peers.begin()->second;require(p.state==SessionReadiness::Ready&&std::any_of(s.collision_states.begin(),s.collision_states.end(),[&](const auto& frame){return frame.tick==tick;}),"Collision ACK requires retained complete state");if(p.collision_stats.has_ack&&tick<=p.collision_stats.ack_tick)return;if(!p.collision_ack_pending||tick>*p.collision_ack_pending)p.collision_ack_pending=tick;}

CollisionStreamDiagnostics WorldSession::collision_diagnostics(ConnectionHandle handle)const{auto& s=*impl_;s.thread();const auto& p=s.peers.at(handle);auto stats=p.collision_stats;stats.partial_chunks=p.collision_parts.size();return stats;}

bool WorldSession::collision_control_ready()const{auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1,"Collision control readiness requires client");const auto& p=s.peers.begin()->second;if(p.state!=SessionReadiness::Ready)return false;if(!p.collision_required)return true;return !s.collision_states.empty()&&p.collision_stats.has_ack&&!p.collision_stats.needs_resync&&p.collision_stats.ack_topology==s.collision_states.back().topology;}
}
