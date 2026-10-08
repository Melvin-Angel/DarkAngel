#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace darkangel {
struct SessionHandshake {unsigned protocol{1};std::string schema_hash,content_hash;std::uint64_t session_epoch{1};};
struct ConnectionHandle {std::uint64_t transport{},epoch{},peer{};auto operator<=>(const ConnectionHandle&) const=default;};
enum class Delivery {ReliableOrdered,UnreliableState,ReliableBootstrap};
struct TransportMessage {ConnectionHandle sender;Delivery delivery;std::uint64_t sequence;std::vector<std::byte> bytes;};
struct TransportLimits {std::size_t packets{32},payload_bytes{64*1024},queued_bytes{512*1024};};
// The shared adapter boundary; gameplay authority and replication remain above transport.
class Transport {
public:
    virtual ~Transport()=default;
    virtual ConnectionHandle open(const SessionHandshake&)=0;
    virtual bool connected() const=0;
    virtual bool valid(ConnectionHandle) const=0;
    virtual TransportLimits limits() const=0;
    virtual bool send(ConnectionHandle,Delivery,std::span<const std::byte>)=0;
    virtual std::vector<TransportMessage> poll(std::size_t max_packets)=0;
    virtual void disconnect(ConnectionHandle)=0;
};
struct LoopbackPair {std::unique_ptr<Transport> host,client;};
// One process-wide namespace across every provider; foreign handles never
// alias merely because two adapters started their private counters at one.
std::uint64_t allocate_transport_identity();
void validate_session_handshake(const SessionHandshake&);
bool compatible_session_handshake(const SessionHandshake&,const SessionHandshake&);
void validate_transport_limits(TransportLimits);
LoopbackPair create_loopback(SessionHandshake expected,TransportLimits={});
}
