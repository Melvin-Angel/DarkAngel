#include <darkangel/world_session.hpp>
#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
namespace darkangel {
namespace {
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
enum class Kind:std::uint64_t { Hello=1,ContentReady,Baseline,Ack,Ready,Resync,MotorCommand,MotorSnapshot };
constexpr std::size_t packet_limit=1000,header_size=56,object_size=104,per_chunk=(packet_limit-header_size)/object_size;
struct Writer {
    std::vector<std::byte> bytes;
    void u64(std::uint64_t n){for(unsigned i=0;i<8;++i)bytes.push_back(static_cast<std::byte>((n>>(i*8))&255));}
    void number(double n){u64(std::bit_cast<std::uint64_t>(n));}
};
struct Reader {
    std::span<const std::byte> bytes;std::size_t at{};
    std::uint64_t u64(){require(bytes.size()-at>=8,"Truncated session packet");std::uint64_t n{};for(unsigned i=0;i<8;++i)n|=std::uint64_t(std::to_integer<unsigned>(bytes[at++]))<<(i*8);return n;}
    double number(){return std::bit_cast<double>(u64());}
    void end(){require(at==bytes.size(),"Trailing session fields");}
};
Writer header(Kind kind,const SessionHandshake& h,std::uint64_t revision=0,std::uint64_t index=0,std::uint64_t chunks=0,std::uint64_t total=0){Writer w;w.u64(0x3153454144);w.u64(static_cast<std::uint64_t>(kind));w.u64(h.session_epoch);w.u64(revision);w.u64(index);w.u64(chunks);w.u64(total);return w;}
void write_object(Writer& w,const ObjectData& o){w.u64(o.network.value);w.u64(o.id.high);w.u64(o.id.low);for(double n:{o.transform.yaw,o.transform.x,o.transform.y,o.transform.z,o.transform.pitch,o.transform.roll,o.transform.scale,o.health.maximum,o.health.current})w.number(n);w.u64(0);}
ObjectData read_object(Reader& r){ObjectData o;o.network.value=r.u64();o.id={r.u64(),r.u64()};o.transform={r.number(),r.number(),r.number(),r.number(),r.number(),r.number(),r.number()};o.health={r.number(),r.number()};require(r.u64()==0,"Unsupported object wire schema");return o;}
void write_vector(Writer& w,MotorVec a){w.number(a.x);w.number(a.y);w.number(a.z);}
MotorVec read_vector(Reader& r){return {r.number(),r.number(),r.number()};}
void write_motor(Writer& w,const MotorState& s){for(auto n:{s.tick,s.sequence,s.epoch,s.topology,s.support,s.action})w.u64(n);for(auto a:{s.position,s.velocity,s.momentum,s.desired,s.achieved,s.support_local,s.support_velocity})write_vector(w,a);w.number(s.yaw);w.u64(s.coyote);w.u64(s.jump_buffer);w.u64(s.grounded?1:0);w.u64(s.crouched?1:0);}
MotorState read_motor(Reader& r){MotorState s;s.tick=r.u64();s.sequence=r.u64();s.epoch=r.u64();s.topology=r.u64();s.support=r.u64();s.action=r.u64();s.position=read_vector(r);s.velocity=read_vector(r);s.momentum=read_vector(r);s.desired=read_vector(r);s.achieved=read_vector(r);s.support_local=read_vector(r);s.support_velocity=read_vector(r);s.yaw=r.number();auto c=r.u64(),j=r.u64(),g=r.u64(),low=r.u64();require(c<=6&&j<=6&&g<=1&&low<=1,"Invalid motor flags/timers");s.coyote=static_cast<unsigned>(c);s.jump_buffer=static_cast<unsigned>(j);s.grounded=g!=0;s.crouched=low!=0;validate_motor_state(s);return s;}

}
struct WorldSession::Impl {
    SessionRole role;SessionHandshake hello;SessionLimits limits;std::thread::id owner=std::this_thread::get_id();
    std::unique_ptr<World> view;std::map<std::uint64_t,ObjectData> objects;std::uint64_t revision{},next_id{1},clock{};
    struct Peer {Transport* transport;ConnectionHandle handle;SessionReadiness state{SessionReadiness::Handshake};std::uint64_t progress{},sent_revision{},acked_revision{};bool greeting{},content_sent{},await_ack{},ready_sent{};std::size_t next_chunk{},total{},chunks{};std::vector<ObjectData> sending;std::map<std::uint64_t,ObjectData> receiving;std::uint64_t receiving_revision{};std::map<std::uint64_t,std::pair<std::uint64_t,std::uint64_t>> motor_sent;};
    std::map<ConnectionHandle,Peer> peers;
    struct MotorQueue {ConnectionHandle owner;std::map<std::uint64_t,MotorInput> pending;MotorInput last;std::uint64_t consumed_tick{},received_sequence{};};
    std::map<std::uint64_t,MotorQueue> motor_inputs;std::map<std::uint64_t,MotorState> motor_states;

    Impl(SessionRole r,SessionHandshake h,SessionLimits l):role(r),hello(std::move(h)),limits(l),view(std::make_unique<World>(r==SessionRole::Server?WorldDomain::Server:WorldDomain::ClientPresentation,l.objects)){
        require((r==SessionRole::Server || r==SessionRole::Client) && l.objects>0 && l.objects<=512 && l.peers>0 && l.peers<=4 && l.packets_per_tick>0 && l.packets_per_tick<=32 && l.timeout_ticks>0,"Invalid session role/bounds");validate_session_handshake(hello);
    }
    void thread() const{require(owner==std::this_thread::get_id(),"Session owner thread mismatch");}
    void authority() const{thread();require(role==SessionRole::Server,"Client cannot mutate authoritative state");require(revision<std::numeric_limits<std::uint64_t>::max(),"Session revision exhausted");}
    std::unique_ptr<World> build(const std::map<std::uint64_t,ObjectData>& values) const{auto candidate=std::make_unique<World>(role==SessionRole::Server?WorldDomain::Server:WorldDomain::ClientPresentation,limits.objects);std::set<StableId> ids;for(const auto& [id,o]:values){require(id>0 && o.network.value==id && ids.insert(o.id).second,"Invalid/duplicate replicated identity");candidate->create(o);}return candidate;}
    bool send(Peer& p,Writer w,Delivery delivery=Delivery::ReliableOrdered){return p.transport->send(p.handle,delivery,w.bytes);}
    void receive(Peer& p,const TransportMessage& m){
        require(m.sender==p.handle && m.bytes.size()<=packet_limit,"Invalid session source/channel/size");Reader r{m.bytes};require(r.u64()==0x3153454144,"Session wire version mismatch");auto kind=static_cast<Kind>(r.u64());require(m.delivery==(kind==Kind::Baseline?Delivery::ReliableBootstrap:(kind==Kind::MotorSnapshot?Delivery::UnreliableState:Delivery::ReliableOrdered)),"Session traffic class mismatch");require(r.u64()==hello.session_epoch,"Stale gameplay session epoch");auto rev=r.u64(),index=r.u64(),chunks=r.u64(),total=r.u64();
        if(kind==Kind::Hello){require(rev==0 && index==0 && chunks==0 && total==0 && !p.greeting,"Duplicate/invalid gameplay handshake");require(r.u64()==hello.protocol,"Protocol mismatch");for(const auto* hash:{&hello.schema_hash,&hello.content_hash})for(char c:*hash){require(r.at<r.bytes.size(),"Truncated handshake digest");require(std::to_integer<unsigned char>(r.bytes[r.at++])==static_cast<unsigned char>(c),"Content/schema mismatch");};r.end();p.greeting=true;p.state=SessionReadiness::ContentReady;}
        else if(kind==Kind::ContentReady){require(role==SessionRole::Server && p.greeting && !p.content_sent && rev==0 && index==0 && chunks==0 && total==0,"Unexpected content readiness");r.end();p.content_sent=true;p.state=SessionReadiness::Bootstrap;}
        else if(kind==Kind::Baseline){
            require(role==SessionRole::Client && p.content_sent && total<=limits.objects && chunks==std::max<std::uint64_t>(1,(total+per_chunk-1)/per_chunk) && index<chunks,"Invalid baseline bounds/state");
            require(rev>=revision && (p.next_chunk==0 || (rev==p.receiving_revision && total==p.total && chunks==p.chunks)) && index==p.next_chunk,"Baseline order/revision mismatch");
            if(index==0){p.receiving.clear();p.receiving_revision=rev;p.total=static_cast<std::size_t>(total);p.chunks=static_cast<std::size_t>(chunks);p.state=SessionReadiness::Bootstrap;}
            auto count=std::min<std::uint64_t>(per_chunk,total-index*per_chunk);for(std::size_t i=0;i<count;++i){auto o=read_object(r);require(p.receiving.emplace(o.network.value,o).second,"Duplicate network identity");}r.end();++p.next_chunk;
            if(p.next_chunk==p.chunks){require(p.receiving.size()==total,"Incomplete baseline");auto candidate=build(p.receiving);view=std::move(candidate);objects=std::move(p.receiving);std::erase_if(motor_states,[&](const auto& entry){return !objects.contains(entry.first);});revision=rev;p.next_chunk=0;p.state=SessionReadiness::CatchUp;p.await_ack=true;}
        }else if(kind==Kind::Ack){require(role==SessionRole::Server && p.await_ack && rev==p.sent_revision && index==0 && chunks==0 && total==0,"Unknown baseline ACK");r.end();p.await_ack=false;p.acked_revision=rev;p.state=SessionReadiness::CatchUp;}
        else if(kind==Kind::Ready){if(role==SessionRole::Client && rev<revision){require(index==0 && chunks==0 && total==0,"Invalid obsolete Ready");r.end();return;}require(role==SessionRole::Client && p.state==SessionReadiness::CatchUp && rev==revision && !p.await_ack && index==0 && chunks==0 && total==0,"Premature Ready");r.end();p.state=SessionReadiness::Ready;}
        else if(kind==Kind::MotorCommand){require(role==SessionRole::Server&&p.state==SessionReadiness::Ready&&index&&chunks==0&&total==0&&rev==0,"Invalid motor command state");auto it=motor_inputs.find(index);require(it!=motor_inputs.end()&&it->second.owner==p.handle,"Motor command ownership denied");MotorInput input;input.sequence=r.u64();input.tick=r.u64();input.epoch=r.u64();input.x=r.number();input.z=r.number();input.yaw=r.number();auto jump=r.u64(),low=r.u64();require(jump<=1&&low<=1,"Invalid input flags");input.jump=jump!=0;input.crouch=low!=0;r.end();validate_motor_input(input);require(input.sequence>0,"Invalid owner sequence");auto& q=it->second;if(motor_states.contains(index)&&input.epoch!=motor_states.at(index).epoch)return;if(input.sequence<=q.received_sequence||input.tick<=q.consumed_tick)return;require(input.tick-q.consumed_tick<=8&&q.pending.size()<8,"Motor input lead/work limit");require(!q.pending.contains(input.tick),"Multiple movement commands for one tick");q.pending.emplace(input.tick,input);q.received_sequence=input.sequence;}
        else if(kind==Kind::MotorSnapshot){require(role==SessionRole::Client&&index&&chunks==0&&total==0,"Invalid motor snapshot direction/header");auto state=read_motor(r);r.end();if(rev!=revision||!objects.contains(index))return;auto it=motor_states.find(index);if(it==motor_states.end()||state.epoch>it->second.epoch||(state.epoch==it->second.epoch&&state.tick>it->second.tick)){require(motor_states.size()<4||it!=motor_states.end(),"Motor snapshot capacity");auto transform=objects.at(index).transform;transform.x=state.position.x;transform.y=state.position.y;transform.z=state.position.z;transform.yaw=state.yaw;view->apply_replica_transform(view->find(objects.at(index).id),transform);objects.at(index).transform=transform;motor_states[index]=state;}}
        else throw std::runtime_error("Unapproved client operation/session message");p.progress=clock;
    }
    void pump(Peer& p){
        if(!p.ready_sent){auto w=header(Kind::Hello,hello);w.u64(hello.protocol);for(const auto* hash:{&hello.schema_hash,&hello.content_hash})for(char c:*hash)w.bytes.push_back(static_cast<std::byte>(c));if(send(p,std::move(w)))p.ready_sent=true;return;}if(!p.greeting)return;
        if(role==SessionRole::Client){if(!p.content_sent){if(send(p,header(Kind::ContentReady,hello))){p.content_sent=true;p.state=SessionReadiness::Bootstrap;}return;}if(p.await_ack && send(p,header(Kind::Ack,hello,revision)))p.await_ack=false;return;}
        if(!p.content_sent || p.await_ack)return;
        if(p.sending.empty() && p.next_chunk==0 && p.state!=SessionReadiness::Bootstrap && p.acked_revision==revision){if(p.state!=SessionReadiness::Ready && send(p,header(Kind::Ready,hello,revision)))p.state=SessionReadiness::Ready;return;}
        if(p.next_chunk==0){p.sending.clear();for(const auto& [id,o]:objects)p.sending.push_back(o);p.sent_revision=revision;p.chunks=std::max<std::size_t>(1,(p.sending.size()+per_chunk-1)/per_chunk);p.state=SessionReadiness::Bootstrap;}
        for(std::size_t work=0;work<limits.packets_per_tick && p.next_chunk<p.chunks;++work){auto w=header(Kind::Baseline,hello,p.sent_revision,p.next_chunk,p.chunks,p.sending.size());auto first=p.next_chunk*per_chunk;for(auto i=first;i<std::min(first+per_chunk,p.sending.size());++i)write_object(w,p.sending[i]);if(!send(p,std::move(w),Delivery::ReliableBootstrap))break;++p.next_chunk;p.progress=clock;}
        if(p.next_chunk==p.chunks){p.await_ack=true;p.next_chunk=0;p.sending.clear();}
    }
};
WorldSession::WorldSession(SessionRole r,SessionHandshake h,SessionLimits l):impl_(std::make_unique<Impl>(r,std::move(h),l)){}
WorldSession::~WorldSession()=default;
void WorldSession::attach(Transport& t,ConnectionHandle h){auto& s=*impl_;s.thread();require(t.limits().payload_bytes>=packet_limit && t.limits().queued_bytes>=packet_limit,"Provider payload/queue too small for session protocol");for(const auto& [handle,peer]:s.peers)require(peer.transport!=&t,"Transport already attached to this session");require(t.valid(h) && h.transport && h.epoch && h.peer && !s.peers.contains(h) && s.peers.size()<s.limits.peers && (s.role==SessionRole::Server || s.peers.empty()),"Invalid session peer");s.peers.emplace(h,Impl::Peer{&t,h,SessionReadiness::Handshake,s.clock});}
void WorldSession::detach(ConnectionHandle h){impl_->thread();for(auto& [id,q]:impl_->motor_inputs)if(q.owner==h){q.pending.clear();q.last={};q.owner={};}require(impl_->peers.erase(h)==1,"Unknown session peer");if(impl_->role==SessionRole::Client){impl_->objects.clear();impl_->motor_states.clear();impl_->view=impl_->build({});impl_->revision=0;}}
void WorldSession::tick(){auto& s=*impl_;s.thread();++s.clock;for(auto it=s.peers.begin();it!=s.peers.end();){auto& p=it->second;try{require(p.transport->connected(),"Session peer disconnected");require(p.state==SessionReadiness::Ready || s.clock-p.progress<=s.limits.timeout_ticks,"Bootstrap timeout");for(const auto& m:p.transport->poll(std::min(s.limits.packets_per_tick,p.transport->limits().packets)))s.receive(p,m);s.pump(p);if(s.role==SessionRole::Server&&p.state==SessionReadiness::Ready){for(const auto& [id,state]:s.motor_states){auto sent=p.motor_sent.find(id);if(sent!=p.motor_sent.end()&&sent->second==std::pair{state.tick,s.revision}&&s.clock%30!=0)continue;auto w=header(Kind::MotorSnapshot,s.hello,s.revision,id);write_motor(w,state);if(!s.send(p,std::move(w),Delivery::UnreliableState))break;p.motor_sent[id]={state.tick,s.revision};}}++it;}catch(...){if(p.transport->valid(p.handle))p.transport->disconnect(p.handle);for(auto& [id,q]:s.motor_inputs)if(q.owner==p.handle){q.pending.clear();q.last={};q.owner={};}it=s.peers.erase(it);if(s.role==SessionRole::Client){s.objects.clear();s.motor_states.clear();s.view=s.build({});s.revision=0;}throw;}}}
std::uint64_t WorldSession::create(ObjectData o){auto& s=*impl_;s.authority();require(s.objects.size()<s.limits.objects && s.next_id<std::numeric_limits<std::uint64_t>::max(),"Session identity/capacity exhausted");require(!o.network.value && !o.target && o.scripts.records.empty() && o.scripts.count==0 && o.optional_json.empty(),"Unsupported replicated object fields");o.network.value=s.next_id;auto candidate=s.objects;candidate.emplace(s.next_id,o);s.view->create(o);s.objects=std::move(candidate);++s.revision;return s.next_id++;}
void WorldSession::move(std::uint64_t id,const Transform& t){auto& s=*impl_;s.authority();require(s.objects.contains(id),"Unknown network identity");require(!s.motor_states.contains(id),"Living character movement requires motor publication");auto candidate=s.objects;candidate.at(id).transform=t;s.view->set_transform(s.view->find(candidate.at(id).id),t,Authority::Server);s.objects=std::move(candidate);++s.revision;}
void WorldSession::destroy(std::uint64_t id){auto& s=*impl_;s.authority();auto candidate=s.objects;require(candidate.erase(id)==1,"Unknown network identity");s.view->destroy(s.view->find(s.objects.at(id).id),Authority::Server);s.objects=std::move(candidate);s.motor_inputs.erase(id);s.motor_states.erase(id);++s.revision;}
const World& WorldSession::world() const{impl_->thread();return *impl_->view;}
const std::map<std::uint64_t,ObjectData>& WorldSession::objects() const{impl_->thread();return impl_->objects;}
std::uint64_t WorldSession::revision() const{impl_->thread();return impl_->revision;}
SessionReadiness WorldSession::readiness(ConnectionHandle h) const{impl_->thread();return impl_->peers.at(h).state;}
}

namespace darkangel {
void WorldSession::own_motor(std::uint64_t id,ConnectionHandle peer){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&s.peers.contains(peer)&&s.motor_inputs.size()<4&&!s.motor_inputs.contains(id),"Invalid motor ownership");s.motor_inputs[id].owner=peer;}
bool WorldSession::submit_motor(std::uint64_t id,const MotorInput& input){auto& s=*impl_;s.thread();require(s.role==SessionRole::Client&&s.objects.contains(id)&&s.peers.size()==1,"Invalid client motor request");validate_motor_input(input);require(input.sequence>0,"Invalid owner sequence");auto& p=s.peers.begin()->second;require(p.state==SessionReadiness::Ready,"Motor request before Ready");auto w=header(Kind::MotorCommand,s.hello,0,id);w.u64(input.sequence);w.u64(input.tick);w.u64(input.epoch);w.number(input.x);w.number(input.z);w.number(input.yaw);w.u64(input.jump?1:0);w.u64(input.crouch?1:0);return s.send(p,std::move(w));}
bool WorldSession::motor_input_ready(std::uint64_t id,std::uint64_t tick)const{auto& s=*impl_;s.thread();return s.motor_inputs.at(id).pending.contains(tick);}
MotorInput WorldSession::consume_motor(std::uint64_t id,std::uint64_t tick,std::uint64_t epoch){auto& s=*impl_;s.authority();auto& q=s.motor_inputs.at(id);require(tick==q.consumed_tick+1&&epoch>0,"Movement consumes exactly one fixed step");q.consumed_tick=tick;MotorInput result{q.last.sequence,tick,epoch};auto it=q.pending.find(tick);if(it!=q.pending.end()){if(it->second.epoch==epoch)result=it->second;q.pending.erase(it);}for(auto i=q.pending.begin();i!=q.pending.end()&&i->first<tick;)i=q.pending.erase(i);q.last=result;return result;}
void WorldSession::publish_motor(std::uint64_t id,const MotorState& state){auto& s=*impl_;s.authority();require(s.objects.contains(id)&&(s.motor_states.contains(id)||s.motor_states.size()<4),"Invalid motor publication");validate_motor_state(state);if(s.motor_states.contains(id)){auto& old=s.motor_states.at(id);require(state.epoch>old.epoch||(state.epoch==old.epoch&&state.tick>old.tick),"Motor snapshot order");}auto transform=s.objects.at(id).transform;transform.x=state.position.x;transform.y=state.position.y;transform.z=state.position.z;transform.yaw=state.yaw;s.view->set_transform(s.view->find(s.objects.at(id).id),transform,Authority::Server);s.objects.at(id).transform=transform;if(s.motor_states.contains(id)&&state.epoch>s.motor_states.at(id).epoch&&s.motor_inputs.contains(id)){auto& q=s.motor_inputs.at(id);q.pending.clear();q.last={};q.received_sequence=0;}s.motor_states[id]=state;}
const std::map<std::uint64_t,MotorState>& WorldSession::motors()const{impl_->thread();return impl_->motor_states;}
}
