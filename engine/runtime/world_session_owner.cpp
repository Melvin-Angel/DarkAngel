#include "world_session_impl.hpp"

namespace darkangel {
auto WorldSession::Impl::pump_owner(Peer& p) -> void {
        if(p.state!=SessionReadiness::Ready)return;
        const auto* value=p.owner_mailbox.front();if(!value)return;
        // Despawning/losing ownership must never disclose an old owner's payload.
        if(role==SessionRole::Server){auto owner=motor_inputs.find(value->network);require(owner!=motor_inputs.end()&&owner->second.owner==p.handle,"Queued owner service lost ownership");}
        auto w=header(Kind::OwnerService,hello,value->serial,value->network,(value->bytes.size()+owner_fragment_bytes-1)/owner_fragment_bytes,value->bytes.size());
        w.u64(value->channel);w.u64(value->part);const auto first=value->part*owner_fragment_bytes;
        w.bytes.insert(w.bytes.end(),value->bytes.begin()+first,value->bytes.begin()+std::min(first+owner_fragment_bytes,value->bytes.size()));
        if(send(p,std::move(w)))p.owner_mailbox.sent();
    }

auto WorldSession::Impl::pump_interaction_notices(Peer& p) -> void {
        auto found=interaction_notices.find(p.handle);if(found==interaction_notices.end())return;
        while(!found->second.empty()){const auto& notice=found->second.front();auto w=header(Kind::InteractReceipt,hello,0,notice.network);w.u64(notice.object.high);w.u64(notice.object.low);w.u64(notice.result);if(!send(p,std::move(w)))return;found->second.pop_front();}interaction_notices.erase(found);
    }

bool WorldSession::submit_interaction(std::uint64_t network,StableId object){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.peers.size()==1&&network&&bool(object),"An interaction request needs a client, its character and an object");auto& peer=s.peers.begin()->second;if(peer.state!=SessionReadiness::Ready||!s.objects.contains(network))return false;auto w=header(Kind::Interact,s.hello,0,network);w.u64(object.high);w.u64(object.low);return s.send(peer,std::move(w));}

std::vector<InteractionRequest> WorldSession::drain_interactions(){auto& s=*impl_;s.authority();auto result=std::move(s.interaction_requests);s.interaction_requests.clear();return result;}

void WorldSession::answer_interaction(std::uint64_t network,StableId object,std::uint8_t result){auto& s=*impl_;s.authority();require(result<=5&&bool(object),"Interaction answer bounds");auto control=s.motor_inputs.find(network);if(control==s.motor_inputs.end()||!s.peers.contains(control->second.owner))return;auto& queue=s.interaction_notices[control->second.owner];require(queue.size()<64,"Interaction receipt backlog");queue.push_back({network,object,result});}

std::vector<InteractionNotice> WorldSession::drain_interaction_receipts(){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client,"Interaction receipts are a client view");auto result=std::move(s.received_interaction_notices);s.received_interaction_notices.clear();return result;}

void WorldSession::register_owner_channel(std::uint32_t channel,StableId avatar){auto& s=*impl_;s.thread();require(s.peers.empty()&&channel&&s.owner_channels.size()<8&&(s.role==SessionRole::Client?bool(avatar):!bool(avatar)),"Owner channel registration role/lifetime/bounds");require(s.owner_channels.emplace(channel,avatar).second,"Duplicate owner channel");}

bool WorldSession::send_owner_message(std::uint64_t network,std::uint32_t channel,std::span<const std::byte> bytes){
    auto& s=*impl_;s.thread();require(s.owner_channels.contains(channel),"Unregistered owner channel");
    require(network&&!bytes.empty()&&bytes.size()<=session_detail::owner_message_bytes,"Owner message identity/size");
    if(s.role==SessionRole::Server){auto owner=s.motor_inputs.find(network);if(owner==s.motor_inputs.end()||!s.peers.contains(owner->second.owner))return false;auto& peer=s.peers.at(owner->second.owner);if(peer.state!=SessionReadiness::Ready)return false;return peer.owner_mailbox.queue(network,channel,bytes);}
    if(s.peers.empty()||s.peers.begin()->second.state!=SessionReadiness::Ready)return false;
    require(s.objects.contains(network)&&s.objects.at(network).id==s.owner_channels.at(channel),"Owner message avatar denied");
    return s.peers.begin()->second.owner_mailbox.queue(network,channel,bytes);
}

std::vector<OwnerMessage> WorldSession::drain_owner_messages(std::uint32_t channel){auto& s=*impl_;s.thread();require(s.owner_channels.contains(channel),"Unregistered owner channel");std::vector<OwnerMessage> result;for(auto it=s.owner_messages.begin();it!=s.owner_messages.end();)if(it->channel==channel){result.push_back(std::move(*it));it=s.owner_messages.erase(it);}else ++it;return result;}
}
