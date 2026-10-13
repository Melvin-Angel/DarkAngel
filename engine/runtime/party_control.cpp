#include <darkangel/party_control.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
constexpr std::size_t byte_limit=4096,queue_limit=16;
std::uint64_t integer(const Json& j,const char* key,std::uint64_t maximum=UINT64_MAX){const auto& value=j.at(key);require(value.is_number_unsigned()||(value.is_number_integer()&&value.get<std::int64_t>()>=0),"Party unsigned integer field");const auto result=value.get<std::uint64_t>();require(result<=maximum,"Party integer field bound");return result;}
Json packet(std::string_view kind){return { {"version",1},{"kind",kind} };}
void reason_valid(const std::string& reason){require(!reason.empty()&&reason.size()<=256,"Party failure text bound");}
void destination_valid(const PartyDestination& d){
    validate_session_handshake(d.hello);require(d.scene!=AssetId{}&&d.hello.protocol>=2&&d.hello.protocol<=3&&!d.members.empty()&&d.members.size()<=4&&d.endpoints.size()==4,"Party destination bounds");
    std::set<StableId> profiles,avatars;std::set<unsigned> seats;
    for(const auto& member:d.members){require(member.profile&&member.avatar&&member.seat<4&&profiles.insert(member.profile).second&&avatars.insert(member.avatar).second&&seats.insert(member.seat).second,"Party profile/avatar/seat identity");if(member.seat)require(!d.endpoints[member.seat].empty()&&d.endpoints[member.seat].size()<=128,"Party gameplay endpoint bound");}
    require(seats.contains(0),"Party destination lacks host");
}
Json encode(const PartyDestination& d){destination_valid(d);Json members=Json::array();for(const auto& m:d.members)members.push_back({{"profile",m.profile.text()},{"avatar",m.avatar.text()},{"seat",m.seat}});
    return {{"scene",d.scene.text()},{"entry",d.entry.text()},{"protocol",d.hello.protocol},{"schema",d.hello.schema_hash},{"content",d.hello.content_hash},{"epoch",d.hello.session_epoch},{"members",members},{"endpoints",d.endpoints}};
}
PartyDestination decode(const Json& j){require(j.is_object()&&j.size()==8&&j.at("members").is_array()&&j.at("members").size()<=4&&j.at("endpoints").is_array()&&j.at("endpoints").size()==4,"Party destination shape");PartyDestination d;d.scene=AssetId::parse(j.at("scene").get<std::string>());const auto entry=j.at("entry").get<std::string>();d.entry=entry==StableId{}.text()?StableId{}:StableId::parse(entry);d.hello={unsigned(integer(j,"protocol",3)),j.at("schema").get<std::string>(),j.at("content").get<std::string>(),integer(j,"epoch")};
    for(const auto& m:j.at("members")){require(m.is_object()&&m.size()==3,"Party member shape");d.members.push_back({StableId::parse(m.at("profile").get<std::string>()),StableId::parse(m.at("avatar").get<std::string>()),unsigned(integer(m,"seat",3))});}d.endpoints=j.at("endpoints").get<std::vector<std::string>>();destination_valid(d);return d;
}
struct Link {
    Transport& transport;ConnectionHandle peer;std::deque<std::string> outgoing;
    void queue(Json j){auto bytes=j.dump();require(bytes.size()<=byte_limit&&outgoing.size()<queue_limit,"Party control queue bound");outgoing.push_back(std::move(bytes));}
    void flush(){while(!outgoing.empty()&&transport.valid(peer)){const auto& bytes=outgoing.front();if(!transport.send(peer,Delivery::ReliableOrdered,{reinterpret_cast<const std::byte*>(bytes.data()),bytes.size()}))break;outgoing.pop_front();}}
    std::vector<Json> poll(){std::vector<Json> result;for(const auto& m:transport.poll(std::min(queue_limit,transport.limits().packets))){require(m.sender==peer&&m.delivery==Delivery::ReliableOrdered&&m.bytes.size()<=byte_limit,"Party control envelope");unsigned work{};auto j=Json::parse(m.bytes.begin(),m.bytes.end(),[&](int depth,Json::parse_event_t,Json&){require(depth<=5&&++work<=256,"Party control document bound");return true;});require(j.is_object()&&integer(j,"version",1)==1&&j.at("kind").is_string(),"Party control version/shape");if(j.contains("transition"))integer(j,"transition");if(j.contains("epoch"))integer(j,"epoch");if(j.contains("seat"))integer(j,"seat",3);result.push_back(std::move(j));}return result;}
    void close(){if(transport.valid(peer))transport.disconnect(peer);outgoing.clear();}
};
}
struct PartyHost::Impl {
    struct Peer {Link link;StableId profile;bool joined{},ready{},admitting{};double age{};Peer(Transport& t,ConnectionHandle h):link{t,h,{}}{}};
    StableId profile;PartyControlLimits limits;std::array<std::unique_ptr<Peer>,4> peers;std::optional<PartyDestination> pending,active;std::uint64_t transition{},epoch{},cancelled{},committed{};bool local{},held{};double ready_age{};PartyPhase phase{PartyPhase::Joined};std::vector<PartyDrop> drops;
    void drop(unsigned seat,std::string reason){auto& p=peers[seat];if(!p)return;drops.push_back({seat,p->profile,std::move(reason)});p->link.close();p.reset();if(pending)std::erase_if(pending->members,[&](const auto& m){return m.seat==seat;});if(active){std::erase_if(active->members,[&](const auto& m){return m.seat==seat;});if(phase==PartyPhase::Running){auto j=packet("roster");j["transition"]=committed;j["destination"]=encode(*active);for(unsigned i=1;i<4;++i)if(peers[i])peers[i]->link.queue(j);}}}
    bool joined()const{for(unsigned i=1;i<4;++i)if(peers[i]&&!peers[i]->joined)return false;return true;}
    std::vector<PartyMember> members()const{std::vector<PartyMember> result{{profile,{},0}};for(unsigned i=1;i<4;++i)if(peers[i]&&peers[i]->joined)result.push_back({peers[i]->profile,{},i});if(active)for(auto& m:result)for(const auto& a:active->members)if(a.profile==m.profile&&a.seat==m.seat)m.avatar=a.avatar;return result;}
    void receive(unsigned seat,const Json& j){auto& p=*peers[seat];const auto kind=j.at("kind").get<std::string>();
        if(kind=="join"){require(j.size()==4&&j.at("seat")==seat,"Party join assigned seat");const auto id=StableId::parse(j.at("profile").get<std::string>());require(bool(id)&&id!=profile,"Party guest profile identity");for(unsigned i=1;i<4;++i)if(i!=seat&&peers[i])require(peers[i]->profile!=id,"Duplicate party profile");require(!p.joined||p.profile==id,"Party connection changed profile");p.profile=id;p.joined=true;auto welcome=packet("welcome");welcome["seat"]=seat;welcome["profile"]=id.text();p.link.queue(std::move(welcome));}
        else if(kind=="ready"){require(j.size()==6&&p.joined,"Party ready shape/member");const auto id=integer(j,"transition");if(id<transition||id<=cancelled)return;if(!pending&&active&&id==committed){require(j.at("scene")==active->scene.text()&&j.at("epoch")==active->hello.session_epoch&&j.at("content")==active->hello.content_hash,"Duplicate party readiness changed content");if(p.admitting){p.admitting=false;p.ready=true;auto resume=packet("resume");resume["transition"]=committed;resume["epoch"]=active->hello.session_epoch;resume["destination"]=encode(*active);p.link.queue(std::move(resume));}return;}require(pending&&id==transition&&j.at("scene")==pending->scene.text()&&j.at("epoch")==pending->hello.session_epoch&&j.at("content")==pending->hello.content_hash,"Party ready transition/epoch/content");p.ready=true;}
        else if(kind=="failed"){require(j.size()==4&&p.joined,"Party failure shape/member");if(integer(j,"transition")<transition)return;require((pending||p.admitting)&&j.at("transition")==transition,"Party failure transition");auto reason=j.at("reason").get<std::string>();reason_valid(reason);drop(seat,std::move(reason));}
        else if(kind=="leave"){require(j.size()==2,"Party leave shape");drop(seat,"Guest left");}
        else throw std::runtime_error("Guest sent a host-only party message");
    }
};
PartyHost::PartyHost(StableId profile,PartyControlLimits limits):impl_(std::make_unique<Impl>()){require(bool(profile)&&std::isfinite(limits.join_seconds)&&std::isfinite(limits.ready_seconds)&&limits.join_seconds>0&&limits.join_seconds<=300&&limits.ready_seconds>0&&limits.ready_seconds<=300,"Party host identity/time bounds");impl_->profile=profile;impl_->limits=limits;}
PartyHost::~PartyHost()=default;
void PartyHost::attach(unsigned seat,Transport& t,ConnectionHandle h){auto& s=*impl_;require(seat>0&&seat<4&&!s.peers[seat]&&!s.pending&&h.transport&&h.epoch&&h.peer,"Party join belongs between scene offers");require(t.limits().payload_bytes>=byte_limit,"Party control payload limit");s.peers[seat]=std::make_unique<Impl::Peer>(t,h);if(!s.active)s.phase=PartyPhase::Joining;}
void PartyHost::pump(double seconds){auto& s=*impl_;require(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Party control clock delta");if(s.pending&&s.local)s.ready_age+=seconds;
    for(unsigned i=1;i<4;++i){auto& p=s.peers[i];if(!p)continue;p->age+=seconds;if(!p->link.transport.connected()){if(p->joined||p->age>s.limits.join_seconds)s.drop(i,p->joined?"Guest disconnected":"Party join timeout");continue;}
        try{p->link.flush();for(const auto& j:p->link.poll()){s.receive(i,j);if(!s.peers[i])break;}if(s.peers[i])s.peers[i]->link.flush();}catch(const std::exception& e){s.drop(i,e.what());}
        if(s.peers[i]&&(!s.peers[i]->joined&&s.peers[i]->age>s.limits.join_seconds))s.drop(i,"Party join timeout");
        if(s.peers[i]&&s.pending&&s.local&&!s.peers[i]->ready&&s.ready_age>s.limits.ready_seconds)s.drop(i,"Scene readiness timeout");
        if(s.peers[i]&&s.peers[i]->admitting&&s.peers[i]->age>s.limits.ready_seconds)s.drop(i,"Scene readiness timeout");
    }if(s.phase==PartyPhase::Joining&&s.joined())s.phase=PartyPhase::Joined;
}
bool PartyHost::joined()const{return impl_->joined();}
std::vector<PartyMember> PartyHost::members()const{return impl_->members();}
std::uint64_t PartyHost::prepare(PartyDestination destination){auto& s=*impl_;destination_valid(destination);require(s.joined()&&!s.pending&&s.transition!=UINT64_MAX,"Party preparation state");const auto roster=s.members();require(roster.size()==destination.members.size(),"Party destination membership count");for(const auto& m:roster)require(std::any_of(destination.members.begin(),destination.members.end(),[&](const auto& d){return m.profile==d.profile&&m.seat==d.seat;}),"Party destination profile mapping");require(destination.hello.session_epoch>s.epoch,"Scene transition needs a fresh epoch");s.epoch=destination.hello.session_epoch;s.pending=std::move(destination);++s.transition;s.local=false;s.held=false;s.ready_age=0;s.phase=PartyPhase::Preparing;
    for(unsigned i=1;i<4;++i)if(s.peers[i]&&s.peers[i]->joined){s.peers[i]->ready=false;s.peers[i]->admitting=false;auto j=packet("prepare");j["transition"]=s.transition;j["destination"]=encode(*s.pending);j["endpoint"]=s.pending->endpoints[i];s.peers[i]->link.queue(std::move(j));}return s.transition;
}
void PartyHost::local_ready(AssetId scene,const SessionHandshake& actual){auto& s=*impl_;require(s.pending&&scene==s.pending->scene&&compatible_session_handshake(actual,s.pending->hello),"Host prepared the wrong scene content/epoch");s.local=true;s.ready_age=0;}
bool PartyHost::ready()const{const auto& s=*impl_;if(!s.pending||!s.local)return false;for(unsigned i=1;i<4;++i)if(s.peers[i]&&(!s.peers[i]->joined||!s.peers[i]->ready))return false;return true;}
bool PartyHost::resume(){auto& s=*impl_;if(!ready())return false;auto j=packet("resume");j["transition"]=s.transition;j["epoch"]=s.pending->hello.session_epoch;j["destination"]=encode(*s.pending);for(unsigned i=1;i<4;++i)if(s.peers[i])s.peers[i]->link.queue(j);s.active=std::move(s.pending);s.pending.reset();s.committed=s.transition;s.phase=PartyPhase::Running;return true;}
void PartyHost::abort(std::string reason){auto& s=*impl_;reason_valid(reason);require(s.pending.has_value(),"Party abort without preparation");auto j=packet("abort");j["transition"]=s.transition;j["reason"]=reason;for(unsigned i=1;i<4;++i)if(s.peers[i])s.peers[i]->link.queue(j);s.cancelled=s.transition;s.pending.reset();s.local=false;s.phase=PartyPhase::Failed;}
std::uint64_t PartyHost::admit(unsigned seat,StableId avatar,std::string endpoint){auto& s=*impl_;require(seat>0&&seat<4&&s.peers[seat]&&s.peers[seat]->joined&&!s.peers[seat]->admitting&&s.active&&!s.pending&&s.phase==PartyPhase::Running&&bool(avatar)&&!endpoint.empty()&&endpoint.size()<=128,"Party live admission state");
    auto& p=*s.peers[seat];for(const auto& m:s.active->members)require(m.seat!=seat&&m.avatar!=avatar&&m.profile!=p.profile,"Party live admission identity");
    auto grown=*s.active;grown.members.push_back({p.profile,avatar,seat});std::sort(grown.members.begin(),grown.members.end(),[](const auto& a,const auto& b){return a.seat<b.seat;});grown.endpoints[seat]=endpoint;destination_valid(grown);s.active=std::move(grown);
    p.ready=false;p.admitting=true;p.age=0;auto offer=packet("prepare");offer["transition"]=s.committed;offer["destination"]=encode(*s.active);offer["endpoint"]=std::move(endpoint);p.link.queue(std::move(offer));
    auto roster=packet("roster");roster["transition"]=s.committed;roster["destination"]=encode(*s.active);for(unsigned i=1;i<4;++i)if(i!=seat&&s.peers[i]&&s.peers[i]->joined&&std::any_of(s.active->members.begin(),s.active->members.end(),[&](const auto& m){return m.seat==i;}))s.peers[i]->link.queue(roster);return s.committed;
}
void PartyHost::expel(unsigned seat,std::string reason){reason_valid(reason);require(seat>0&&seat<4,"Party seat");impl_->drop(seat,std::move(reason));}
void PartyHost::hold(bool held){auto& s=*impl_;require(s.active&&!s.pending&&s.phase==PartyPhase::Running,"Party hold needs a running scene");if(s.held==held)return;s.held=held;auto j=packet("hold");j["transition"]=s.committed;j["held"]=held;for(unsigned i=1;i<4;++i)if(s.peers[i]&&s.peers[i]->joined&&!s.peers[i]->admitting&&std::any_of(s.active->members.begin(),s.active->members.end(),[&](const auto& m){return m.seat==i;}))s.peers[i]->link.queue(j);}
PartyPhase PartyHost::phase()const{return impl_->phase;}
std::vector<PartyDrop> PartyHost::drain_drops(){auto result=std::move(impl_->drops);impl_->drops.clear();return result;}
struct PartyGuest::Impl {Link link;StableId profile;unsigned seat{};bool sent{},held{};PartyPhase phase{PartyPhase::Joining};std::optional<PartyOffer> offer;std::string error;
    Impl(StableId id,unsigned assigned,Transport& t,ConnectionHandle h):link{t,h,{}},profile(id),seat(assigned){}
    void receive(const Json& j){const auto kind=j.at("kind").get<std::string>();
        if(kind=="welcome"){require(j.size()==4&&j.at("seat")==seat&&j.at("profile")==profile.text()&&phase==PartyPhase::Joining,"Party welcome identity/state");phase=PartyPhase::Joined;}
        else if(kind=="prepare"){require(j.size()==5&&phase!=PartyPhase::Joining,"Party offer state/shape");const auto id=integer(j,"transition");if(offer&&id<offer->transition)return;require(id&&(!offer||id>offer->transition),"Repeated or conflicting party offer");auto destination=decode(j.at("destination"));require(std::any_of(destination.members.begin(),destination.members.end(),[&](const auto& m){return m.profile==profile&&m.seat==seat;}),"Party offer lacks guest profile");auto endpoint=j.at("endpoint").get<std::string>();require(endpoint==destination.endpoints[seat],"Party offer endpoint mismatch");if(offer)require(destination.hello.session_epoch>offer->destination.hello.session_epoch,"Party offer stale scene epoch");offer=PartyOffer{id,std::move(destination),std::move(endpoint)};error.clear();held=false;phase=PartyPhase::Preparing;}
        else if(kind=="resume"){require(j.size()==5&&offer,"Party resume without offer");if(integer(j,"transition")<offer->transition)return;require(j.at("transition")==offer->transition&&j.at("epoch")==offer->destination.hello.session_epoch&&(phase==PartyPhase::Ready||phase==PartyPhase::Running),"Party resume before matching readiness");auto destination=decode(j.at("destination"));require(compatible_session_handshake(destination.hello,offer->destination.hello)&&destination.scene==offer->destination.scene&&destination.entry==offer->destination.entry,"Party resume changed prepared destination");for(const auto& member:destination.members)require(std::find(offer->destination.members.begin(),offer->destination.members.end(),member)!=offer->destination.members.end(),"Party resume changed profile/avatar mapping");require(std::any_of(destination.members.begin(),destination.members.end(),[&](const auto& m){return m.profile==profile&&m.seat==seat;}),"Party resume dropped recipient");offer->destination=std::move(destination);held=false;phase=PartyPhase::Running;}
        else if(kind=="abort"){require(j.size()==4&&offer,"Party abort without offer");if(integer(j,"transition")<offer->transition)return;require(j.at("transition")==offer->transition,"Party abort identity");error=j.at("reason").get<std::string>();reason_valid(error);phase=PartyPhase::Failed;}
        else if(kind=="roster"){require(j.size()==4&&offer,"Party roster without scene offer");if(integer(j,"transition")<offer->transition)return;require(j.at("transition")==offer->transition,"Party roster transition");auto destination=decode(j.at("destination"));require(destination.scene==offer->destination.scene&&destination.entry==offer->destination.entry&&compatible_session_handshake(destination.hello,offer->destination.hello),"Party roster changed scene/epoch");// Members may leave, and join the running scene; nobody already here changes profile, avatar or seat.
            for(const auto& known:offer->destination.members)for(const auto& m:destination.members)require(m==known||(m.profile!=known.profile&&m.avatar!=known.avatar&&m.seat!=known.seat),"Party roster changed a prepared member");require(std::any_of(destination.members.begin(),destination.members.end(),[&](const auto& m){return m.profile==profile&&m.seat==seat;}),"Party roster dropped recipient");offer->destination=std::move(destination);}
        else if(kind=="hold"){require(j.size()==4&&offer&&j.at("held").is_boolean(),"Party hold shape");if(integer(j,"transition")<offer->transition)return;require(j.at("transition")==offer->transition&&phase==PartyPhase::Running,"Party hold outside a running scene");held=j.at("held").get<bool>();}
        else throw std::runtime_error("Host sent a guest-only party message");
    }
};
PartyGuest::PartyGuest(StableId profile,unsigned seat,Transport& t,ConnectionHandle h):impl_(std::make_unique<Impl>(profile,seat,t,h)){require(profile&&seat>0&&seat<4&&h.transport&&h.epoch&&h.peer&&t.limits().payload_bytes>=byte_limit,"Party guest identity/transport");}
PartyGuest::~PartyGuest()=default;
void PartyGuest::pump(){auto& s=*impl_;if(s.phase==PartyPhase::Left)return;if(!s.link.transport.connected()){if(s.sent){s.phase=PartyPhase::Failed;if(s.error.empty())s.error="Party disconnected";}return;}
    try{if(!s.sent){auto j=packet("join");j["profile"]=s.profile.text();j["seat"]=s.seat;s.link.queue(std::move(j));s.sent=true;}s.link.flush();for(const auto& j:s.link.poll())s.receive(j);s.link.flush();}catch(const std::exception& e){s.error=e.what();s.phase=PartyPhase::Failed;s.link.close();}
}
PartyPhase PartyGuest::phase()const{return impl_->phase;}
const std::optional<PartyOffer>& PartyGuest::offer()const{return impl_->offer;}
bool PartyGuest::held()const{return impl_->held;}
bool PartyGuest::ready(AssetId scene,const SessionHandshake& actual){auto& s=*impl_;require(s.offer&&(s.phase==PartyPhase::Preparing||s.phase==PartyPhase::Ready),"Guest readiness without prepared offer");if(scene!=s.offer->destination.scene||!compatible_session_handshake(actual,s.offer->destination.hello)){failed("Scene content or epoch mismatch");return false;}auto j=packet("ready");j["transition"]=s.offer->transition;j["scene"]=scene.text();j["epoch"]=actual.session_epoch;j["content"]=actual.content_hash;s.link.queue(std::move(j));s.phase=PartyPhase::Ready;return true;}
void PartyGuest::failed(std::string reason){auto& s=*impl_;require(s.offer.has_value(),"Guest failure without offer");reason_valid(reason);auto j=packet("failed");j["transition"]=s.offer->transition;j["reason"]=reason;s.link.queue(std::move(j));s.error=std::move(reason);s.phase=PartyPhase::Failed;}
void PartyGuest::leave(){auto& s=*impl_;if(s.phase==PartyPhase::Left)return;s.link.queue(packet("leave"));s.link.flush();s.link.close();s.phase=PartyPhase::Left;}
const std::string& PartyGuest::error()const{return impl_->error;}
}
