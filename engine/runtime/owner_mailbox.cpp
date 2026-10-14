#include "owner_mailbox.hpp"
#include "session_wire.hpp"
#include <algorithm>

namespace darkangel::session_detail {
bool OwnerMailbox::queue(std::uint64_t network,std::uint32_t channel,std::span<const std::byte> bytes){
    wire_require(network&&channel&&!bytes.empty()&&bytes.size()<=owner_message_bytes,"Owner message identity/size");
    wire_require(next_!=UINT64_MAX,"Owner message serial exhausted");
    if(sending_.size()==8)return false;
    sending_.push_back({next_,network,channel,{bytes.begin(),bytes.end()}});++next_;return true;
}
void OwnerMailbox::sent(){auto& head=sending_.front();if(++head.part==(head.bytes.size()+owner_fragment_bytes-1)/owner_fragment_bytes)sending_.pop_front();}
std::optional<OwnerMessage> OwnerMailbox::receive(ConnectionHandle source,std::uint64_t serial,std::uint64_t network,std::uint32_t channel,
    std::uint64_t part,std::uint64_t parts,std::uint64_t total,std::span<const std::byte> bytes){
    wire_require(serial&&serial==received_+1&&network&&channel&&total&&total<=owner_message_bytes&&
        parts==(total+owner_fragment_bytes-1)/owner_fragment_bytes&&part<parts,"Owner fragment bounds/sequence");
    wire_require(bytes.size()==std::min<std::uint64_t>(owner_fragment_bytes,total-part*owner_fragment_bytes),"Owner fragment length");
    if(!partial_){wire_require(part==0,"Owner fragment order");partial_=Sending{serial,network,channel};total_=std::size_t(total);partial_->bytes.reserve(total_);}
    auto& value=*partial_;wire_require(value.serial==serial&&value.network==network&&value.channel==channel&&total_==total&&value.part==part,"Owner fragment identity/order");
    value.bytes.insert(value.bytes.end(),bytes.begin(),bytes.end());++value.part;
    if(value.part!=parts)return {};
    OwnerMessage message{source,network,channel,std::move(value.bytes)};partial_.reset();received_=serial;return message;
}
}
