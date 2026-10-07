#include <darkangel/transport.hpp>
#include <atomic>
#include <deque>
#include <limits>
#include <stdexcept>
#include <thread>
namespace darkangel {
namespace {
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
std::atomic<std::uint64_t> next_transport{1};
struct Shared {
    SessionHandshake expected;TransportLimits limits;const std::uint64_t token=next_transport.fetch_add(1);
    std::uint64_t epoch{1},sequence[2]{};bool open[2]{};std::size_t queued[2]{};
    std::deque<TransportMessage> incoming[2];std::thread::id owner=std::this_thread::get_id();
};
class Loopback final:public Transport {
    std::shared_ptr<Shared> state;unsigned side;
    void thread() const{require(state->owner==std::this_thread::get_id(),"Loopback accessed outside its owner thread");}
    ConnectionHandle peer() const{return {state->token,state->epoch,2-side};}
    void check(ConnectionHandle handle) const{thread();require(handle==peer() && connected(),"Stale, disconnected or foreign transport handle");}
public:
    Loopback(std::shared_ptr<Shared> shared,unsigned role):state(std::move(shared)),side(role){}
    ~Loopback() override {if(state->open[side]){for(unsigned i=0;i<2;++i){state->incoming[i].clear();state->open[i]=false;state->queued[i]=0;}if(state->epoch<std::numeric_limits<std::uint64_t>::max())++state->epoch;}}
    ConnectionHandle open(const SessionHandshake& hello) override {thread();const auto& expected=state->expected;require(hello.protocol==expected.protocol && hello.schema_hash==expected.schema_hash && hello.content_hash==expected.content_hash && hello.session_epoch==expected.session_epoch,"Incompatible protocol/schema/content/session epoch");state->open[side]=true;return peer();}
    bool connected() const override {thread();return state->open[0] && state->open[1];}
    bool send(ConnectionHandle target,Delivery delivery,std::span<const std::byte> bytes) override {check(target);require(delivery==Delivery::ReliableOrdered || delivery==Delivery::UnreliableState,"Invalid transport channel");require(bytes.size()<=state->limits.payload_bytes,"Transport payload limit");auto destination=1-side;
        if(state->incoming[destination].size()>=state->limits.packets || bytes.size()>state->limits.queued_bytes-state->queued[destination])return false;
        require(state->sequence[side]<std::numeric_limits<std::uint64_t>::max(),"Transport sequence exhausted");TransportMessage message{{state->token,state->epoch,side+1},delivery,state->sequence[side]+1,{bytes.begin(),bytes.end()}};
        state->incoming[destination].push_back(std::move(message));state->queued[destination]+=bytes.size();++state->sequence[side];return true;
    }
    std::vector<TransportMessage> poll(std::size_t count) override {thread();require(count<=state->limits.packets,"Transport poll work limit");std::vector<TransportMessage> messages;auto& queue=state->incoming[side];while(!queue.empty() && messages.size()<count){state->queued[side]-=queue.front().bytes.size();messages.push_back(std::move(queue.front()));queue.pop_front();}return messages;}
    void disconnect(ConnectionHandle handle) override {check(handle);require(state->epoch<std::numeric_limits<std::uint64_t>::max(),"Transport epoch exhausted");for(unsigned i=0;i<2;++i){state->incoming[i].clear();state->open[i]=false;state->queued[i]=0;state->sequence[i]=0;}++state->epoch;}
};
}
LoopbackPair create_loopback(SessionHandshake expected,TransportLimits limits){auto digest=[](const std::string& hash){return hash.size()==64 && hash.find_first_not_of("0123456789abcdef")==hash.npos;};require(expected.protocol>0 && expected.session_epoch>0 && digest(expected.schema_hash) && digest(expected.content_hash),"Invalid session admission descriptor");require(limits.packets>0 && limits.packets<=1024 && limits.payload_bytes>0 && limits.payload_bytes<=64*1024 && limits.queued_bytes>0 && limits.queued_bytes<=64*1024*1024,"Invalid transport limits");auto shared=std::make_shared<Shared>();shared->expected=std::move(expected);shared->limits=limits;return {std::make_unique<Loopback>(shared,0),std::make_unique<Loopback>(shared,1)};}
}
