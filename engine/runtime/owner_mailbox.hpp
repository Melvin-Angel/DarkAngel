#pragma once
#include <darkangel/world_session.hpp>
#include <deque>
#include <optional>

namespace darkangel::session_detail {
inline constexpr std::size_t owner_fragment_bytes=928;
inline constexpr std::size_t owner_message_bytes=32768;
// One ordered stream per peer. No arbitrary fragment map or unbounded backlog.
class OwnerMailbox {
public:
    struct Sending {std::uint64_t serial{},network{};std::uint32_t channel{};std::vector<std::byte> bytes;std::size_t part{};};
    bool queue(std::uint64_t network,std::uint32_t channel,std::span<const std::byte> bytes);
    const Sending* front()const{return sending_.empty()?nullptr:&sending_.front();}
    void sent();
    std::optional<OwnerMessage> receive(ConnectionHandle,std::uint64_t serial,std::uint64_t network,std::uint32_t channel,
        std::uint64_t part,std::uint64_t parts,std::uint64_t total,std::span<const std::byte>);
private:
    std::deque<Sending> sending_;std::uint64_t next_{1},received_{};
    std::optional<Sending> partial_;std::size_t total_{};
};
}
