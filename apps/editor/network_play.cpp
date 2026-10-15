#include "network_play.hpp"
#include <darkangel/scene_authority.hpp>
#include <darkangel/scene_client.hpp>
#include <darkangel/scene_inventory.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/gns_transport.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace darkangel::editor_app {
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SessionHandshake control_descriptor(){return {1,sha256("DarkAngel editor party control v1"),sha256("DarkAngel editor party admission"),1};}
std::string bounded(std::string value){if(value.empty())value="Scene preparation failed";return value.substr(0,256);}
std::wstring wide(std::string_view value){if(value.empty())return {};const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);check(count>0,"Invalid UTF-8 process argument");std::wstring out(count,L' ');check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count)==count,"Process argument conversion failed");return out;}
std::wstring quote(const std::wstring& value){std::wstring out=L"\"";unsigned slashes{};for(auto c:value){if(c==L'\\'){++slashes;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;}out.append(slashes*2,L'\\');out+=L'\"';return out;}
struct Address {std::string host;unsigned port{};std::string at(unsigned value)const{return host+":"+std::to_string(value);}};
Address split(std::string_view address){const auto colon=address.rfind(':');check(colon!=address.npos&&colon>0&&colon+1<address.size(),"Use an explicit IP address:port");const auto text=std::string(address.substr(colon+1));check(text.find_first_not_of("0123456789")==text.npos,"Invalid party port");const auto port=std::stoul(text);check(port>0&&port<=65000,"Party port must be between 1 and 65000");return {std::string(address.substr(0,colon)),unsigned(port)};}
struct Child {
    HANDLE process{},thread{},log{},input{},job{};DWORD id{};
    ~Child(){stop();if(log&&log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(input&&input!=INVALID_HANDLE_VALUE)CloseHandle(input);if(job)CloseHandle(job);}
    bool exited()const{return process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0;}
    static BOOL CALLBACK close_window(HWND window,LPARAM value){DWORD process{};GetWindowThreadProcessId(window,&process);if(process==DWORD(value))PostMessageW(window,WM_CLOSE,0,0);return TRUE;}
    void stop(){if(!process)return;if(!exited()){EnumWindows(close_window,LPARAM(id));if(WaitForSingleObject(process,1500)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,1500);}}CloseHandle(process);process=nullptr;if(thread){CloseHandle(thread);thread=nullptr;}}
    void start(const std::vector<std::string>& arguments,const std::filesystem::path& path){
        check(!process,"Child already launched");std::filesystem::create_directories(path.parent_path());SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};log=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(log!=INVALID_HANDLE_VALUE,"Cannot create child log");
        std::wstring executable(32768,L' ');const auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));check(length&&length<executable.size(),"Cannot locate the running editor");executable.resize(length);auto command=quote(executable);for(const auto& argument:arguments)command+=L" "+quote(wide(argument));
        input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(input!=INVALID_HANDLE_VALUE,"Cannot create child input handle");
        SIZE_T size{};InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<std::byte> attributes(size);auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());check(InitializeProcThreadAttributeList(list,1,0,&size)!=FALSE,"Cannot initialize child handle list");
        struct AttributeGuard {LPPROC_THREAD_ATTRIBUTE_LIST list;~AttributeGuard(){DeleteProcThreadAttributeList(list);}} guard{list};HANDLE inherited[]{log,input};check(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=FALSE,"Cannot restrict child handles");
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdOutput=startup.StartupInfo.hStdError=log;startup.StartupInfo.hStdInput=input;startup.lpAttributeList=list;PROCESS_INFORMATION created{};
        job=CreateJobObjectW(nullptr,nullptr);check(job!=nullptr,"Cannot create child job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;check(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Cannot configure child lifetime");
        check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&created)!=FALSE,"Cannot launch guest editor");process=created.hProcess;thread=created.hThread;id=created.dwProcessId;
        check(AssignProcessToJobObject(job,process)!=FALSE,"Cannot own the guest process lifetime");check(ResumeThread(thread)!=DWORD(-1),"Cannot start the guest process");
    }
};
struct Gameplay {
    PreparedScene* target{};PartyDestination destination;std::array<std::unique_ptr<Transport>,4> links;std::array<ConnectionHandle,4> handles;
};
}

StableId local_player_profile(unsigned slot){
    check(slot<scene_party_limit,"Local profile slot is outside the party limit");const auto* location=std::getenv("LOCALAPPDATA");check(location&&*location,"LOCALAPPDATA is unavailable");const auto path=std::filesystem::path(location)/"DarkAngel"/"profiles"/"local.json";StableId root;
    if(std::filesystem::exists(path)){std::ifstream file(path);auto value=nlohmann::json::parse(file);check(value.is_object()&&value.size()==2&&value.at("schema")==1,"Unsupported local profile");root=StableId::parse(value.at("profile").get<std::string>());}
    else{root=asset_key(AssetId::random());std::filesystem::create_directories(path.parent_path());const auto data=nlohmann::json{{"schema",1},{"profile",root.text()}}.dump(2)+"\n";const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){check(GetLastError()==ERROR_FILE_EXISTS,"Cannot create local profile");return local_player_profile(slot);}DWORD written{};const bool saved=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);check(saved,"Cannot commit local profile");}
    return slot?StableId::parse(sha256("DarkAngel local player v1:"+root.text()+":"+std::to_string(slot)).substr(0,32)):root;
}

struct EditorPartyHost::Impl {
    std::unique_ptr<PartyInventory> inventory;PartyHost party;unsigned expected;Address bind;std::string address;std::array<std::unique_ptr<Transport>,4> controls;std::array<ConnectionHandle,4> handles;std::array<bool,4> attached{};std::array<std::unique_ptr<Child>,4> children;
    std::unique_ptr<Gameplay> pending,active;std::uint64_t epoch{};bool settled{},stopped{},local_ready{},held{};double age{};std::array<unsigned,4> admission{};std::array<double,4> reopen{};std::vector<PartyDrop> drops;std::string bootstrap;
    Impl(StableId profile,unsigned count,std::string requested):party(profile,{120,60}),expected(count){
        check(count>=1&&count<=scene_party_limit,"Editor Players must be between 1 and 4");
        auto open=[&](Address value){std::array<std::unique_ptr<Transport>,4> candidates;std::array<ConnectionHandle,4> peers;for(unsigned seat=1;seat<4;++seat){candidates[seat]=create_gns_host(value.at(value.port+seat-1),control_descriptor());peers[seat]=candidates[seat]->open(control_descriptor());}bind=std::move(value);controls=std::move(candidates);handles=peers;address=bind.at(bind.port);};
        if(!requested.empty()){auto value=split(requested);check(value.port<=64000,"Party port leaves no gameplay range");open(std::move(value));}
        else{const auto seed=AssetId::random().text();const auto begin=22000u+unsigned(std::stoul(seed.substr(0,4),nullptr,16)%18000);bool found{};for(unsigned n=0;n<32&&!found;++n)try{open({"127.0.0.1",begin+n*8});found=true;}catch(const std::exception&){}check(found,"No localhost party port was available");}
    }
};
EditorPartyHost::EditorPartyHost(StableId profile,unsigned players,std::string address):impl_(std::make_unique<Impl>(profile,players,std::move(address))){}
EditorPartyHost::~EditorPartyHost()=default;
const std::string& EditorPartyHost::address()const{return impl_->address;}
void EditorPartyHost::launch(unsigned seat,std::vector<std::string> arguments,const std::filesystem::path& log){auto& p=*impl_;check(seat>0&&seat<p.expected&&!p.children[seat],"Invalid child seat");arguments.insert(arguments.end(),{"--join",p.address,"--player",std::to_string(seat),"--local-profile",std::to_string(seat)});auto child=std::make_unique<Child>();child->start(arguments,log);p.children[seat]=std::move(child);}
void EditorPartyHost::pump(double seconds,CharacterSceneSession* running,bool travelling){auto& p=*impl_;check(std::isfinite(seconds)&&seconds>=0&&seconds<=10,"Invalid party frame delta");if(p.stopped)return;p.age+=seconds;
    // A seat whose guest left listens again, so the player can come back.
    for(unsigned seat=1;seat<4;++seat)if(!p.controls[seat]&&p.age>=p.reopen[seat]){try{p.controls[seat]=create_gns_host(p.bind.at(p.bind.port+seat-1),control_descriptor());p.handles[seat]=p.controls[seat]->open(control_descriptor());p.attached[seat]=false;}catch(const std::exception&){p.controls[seat].reset();p.reopen[seat]=p.age+1;}}
    for(unsigned seat=1;seat<4;++seat)if(!p.attached[seat]&&p.controls[seat]&&!p.pending&&p.party.phase()!=PartyPhase::Preparing&&p.controls[seat]->connected()){try{p.party.attach(seat,*p.controls[seat],p.handles[seat]);p.attached[seat]=true;}catch(const std::exception&){if(p.controls[seat]->valid(p.handles[seat]))p.controls[seat]->disconnect(p.handles[seat]);}}
    p.party.pump(seconds);for(auto drop:p.party.drain_drops()){if(p.active&&p.active->links[drop.seat]&&p.active->links[drop.seat]->valid(p.active->handles[drop.seat]))p.active->links[drop.seat]->disconnect(p.active->handles[drop.seat]);
        if(drop.seat){p.admission[drop.seat]=0;p.controls[drop.seat].reset();p.reopen[drop.seat]=p.age+.5;}p.drops.push_back(std::move(drop));}
    if(running&&p.active&&!p.pending&&p.party.phase()==PartyPhase::Running){auto& game=*p.active;
        if(p.held!=travelling){p.party.hold(travelling);p.held=travelling;}
        // Live admission: a gameplay endpoint and the scene offer first; the avatar enters once the guest has connected to it.
        if(!travelling)for(const auto& member:p.party.members()){const auto seat=member.seat;if(!seat||member.avatar||p.admission[seat]||running->authority().network(party_member(seat)))continue;
            try{game.links[seat].reset();const auto endpoint=p.bind.at(p.bind.port+8+unsigned((p.epoch-1)%128)*4+seat-1);game.links[seat]=create_gns_host(endpoint,game.destination.hello);game.handles[seat]=game.links[seat]->open(game.destination.hello);p.party.admit(seat,party_member(seat),endpoint);p.admission[seat]=1;std::cout<<"Party host admitting seat="<<seat<<" epoch="<<p.epoch<<std::endl;}
            catch(const std::exception& error){p.party.expel(seat,bounded(error.what()));}}
        for(unsigned seat=1;seat<4;++seat)if(p.admission[seat]==1&&game.links[seat]&&game.links[seat]->connected()){try{auto roster=p.party.members();auto member=std::find_if(roster.begin(),roster.end(),[&](const auto& m){return m.seat==seat;});check(member!=roster.end(),"Inventory profile missing for admission");running->add_member({party_member(seat),game.links[seat].get(),game.handles[seat],{},0,member->profile});p.admission[seat]=2;}catch(const std::exception& error){p.admission[seat]=0;p.party.expel(seat,bounded(error.what()));}}
        for(const auto& [avatar,reason]:running->take_refused())for(unsigned seat=1;seat<4;++seat)if(party_member(seat)==avatar){p.admission[seat]=0;p.party.expel(seat,bounded(reason));}
    }
    if(!p.settled&&p.party.joined()&&p.party.members().size()>=p.expected)p.settled=true;if(!p.settled&&p.age>120){p.settled=true;p.drops.push_back({0,{},"Some players did not join before the timeout"});}
}
std::unique_ptr<CharacterSceneSession> EditorPartyHost::build(PreparedScene& scene,StableId entry,const SceneStart& start,std::span<const ScenePlayer>){auto& p=*impl_;if(!p.settled||!p.party.joined())return {};
    if(!p.pending||p.pending->target!=&scene){if(p.party.phase()==PartyPhase::Preparing||p.party.phase()==PartyPhase::Ready)p.party.abort("Rebuilding the source scene");p.pending.reset();check(p.epoch<UINT64_MAX,"Scene epoch exhausted");scene.hello.session_epoch=++p.epoch;
        auto next=std::make_unique<Gameplay>();next->target=&scene;auto& destination=next->destination;destination.scene=scene.scene;destination.entry=entry;destination.hello=scene.hello;destination.members=p.party.members();destination.endpoints.resize(4);
        for(auto& member:destination.members){member.avatar=member.seat?party_member(member.seat):scene.player;if(member.seat){const auto port=p.bind.port+8+unsigned((p.epoch-1)%128)*4+member.seat-1;destination.endpoints[member.seat]=p.bind.at(port);next->links[member.seat]=create_gns_host(destination.endpoints[member.seat],scene.hello);next->handles[member.seat]=next->links[member.seat]->open(scene.hello);}}
        p.party.prepare(destination);p.pending=std::move(next);p.local_ready=false;p.held=false;p.admission={};}
    const auto roster=p.party.members();for(const auto& member:roster)if(member.seat&&!p.pending->links[member.seat]->connected())return {};
    std::vector<SceneSeat> seats;for(const auto& member:roster)if(member.seat)seats.push_back({party_member(member.seat),p.pending->links[member.seat].get(),p.pending->handles[member.seat],{},0,member.profile});
    SceneStart placed=start;if(scene.combat&&scene.combat->inventory){if(!p.inventory)p.inventory=std::make_unique<PartyInventory>(scene.combat->inventory->catalog);placed.inventory=p.inventory.get();for(const auto& member:roster)placed.profiles[member.seat?party_member(member.seat):scene.player]=member.profile;}if(!placed.arrival&&!seats.empty())for(const auto& object:scene.objects)if(object.id==scene.player)placed.arrival=SceneArrival{{object.transform.x,object.transform.y,object.transform.z},object.transform.yaw};
    auto session=std::make_unique<CharacterSceneSession>(scene.hello,scene.objects,scene.player,scene.collision,scene.plan,scene.rig,scene.rig_archive,scene.combat,std::span<const ScenePlayer>{},placed,seats,SceneTiming::Independent);p.active=std::move(p.pending);return session;
}
bool EditorPartyHost::ready(CharacterSceneSession& session){auto& p=*impl_;session.pump();if(!p.local_ready){auto status=session.client(session.players().front())->bootstrap_status();if(status!=p.bootstrap){p.bootstrap=std::move(status);std::cout<<"Party host bootstrap "<<p.bootstrap<<std::endl;}}if(!p.active||!session.bootstrapped())return false;if(!p.local_ready){p.party.local_ready(p.active->destination.scene,p.active->destination.hello);p.local_ready=true;std::cout<<"Party host prepared epoch="<<p.epoch<<" players="<<p.party.members().size()<<std::endl;}return p.party.ready();}
void EditorPartyHost::resume(){check(impl_->party.resume(),"Party resumed before all players were ready");std::cout<<"Party host resumed epoch="<<impl_->epoch<<" players="<<impl_->party.members().size()<<std::endl;}
void EditorPartyHost::cancel(std::string_view){/* No scene offer has been sent before the coordinator swaps. */}
std::string EditorPartyHost::status()const{const auto& p=*impl_;return !p.settled?"Waiting for players":p.pending?"Connecting players to the scene":"Waiting for players to prepare";}
PartyInventory* EditorPartyHost::inventory()const{return impl_->inventory.get();}
std::size_t EditorPartyHost::members()const{return impl_->party.members().size();}
std::vector<PartyMember> EditorPartyHost::roster()const{return impl_->party.members();}
std::vector<PartyDrop> EditorPartyHost::drain_drops(){auto value=std::move(impl_->drops);impl_->drops.clear();return value;}
std::uint64_t EditorPartyHost::epoch()const{return impl_->epoch;}
void EditorPartyHost::stop(){auto& p=*impl_;if(p.stopped)return;for(auto& child:p.children)if(child)child->stop();p.stopped=true;p.pending.reset();p.active.reset();for(auto& link:p.controls)link.reset();}

struct EditorPartyGuest::Impl {
    SceneProvider& provider;std::unique_ptr<Transport> control;ConnectionHandle peer;std::unique_ptr<PartyGuest> party;std::unique_ptr<Transport> gameplay;ConnectionHandle game_peer;
    std::unique_ptr<PreparedScene> initial,prepared;std::unique_ptr<CharacterSceneSession> session;StableId profile,owner;unsigned seat;std::uint64_t transition{};bool loading{},mismatch{},failed{},finished{},reported{},held{};std::string error,bootstrap,text{"Joining party"};
    Impl(StableId id,unsigned assigned,std::string address,SceneProvider& scenes,bool fault):provider(scenes),profile(id),seat(assigned),mismatch(fault){check(seat>0&&seat<4,"Guest player seat must be between 1 and 3");auto endpoint=split(address);control=create_gns_client(endpoint.at(endpoint.port+seat-1),control_descriptor());peer=control->open(control_descriptor());party=std::make_unique<PartyGuest>(profile,seat,*control,peer);}
};
EditorPartyGuest::EditorPartyGuest(StableId profile,unsigned seat,std::string address,SceneProvider& provider,bool mismatch):impl_(std::make_unique<Impl>(profile,seat,std::move(address),provider,mismatch)){}
EditorPartyGuest::~EditorPartyGuest()=default;
void EditorPartyGuest::initial(std::unique_ptr<PreparedScene> scene){impl_->initial=std::move(scene);}
void EditorPartyGuest::pump(){auto& p=*impl_;if(p.finished)return;p.party->pump();if(p.party->held()!=p.held){p.held=p.party->held();std::cout<<"Party guest held="<<p.held<<std::endl;}if(p.party->phase()==PartyPhase::Failed&&!p.control->connected()){p.error=p.party->error();p.finished=true;return;}
    try{const auto& offer=p.party->offer();if(!offer)return;
        if(offer->transition!=p.transition){if(p.session&&unloading)unloading();p.session.reset();p.gameplay.reset();if(p.prepared&&!p.initial&&p.prepared->scene==offer->destination.scene)p.initial=std::move(p.prepared);p.prepared.reset();if(p.loading)p.provider.cancel();p.loading=false;p.transition=offer->transition;p.failed=p.reported=false;p.error.clear();
            for(const auto& member:offer->destination.members)if(member.profile==p.profile&&member.seat==p.seat)p.owner=member.avatar;check(bool(p.owner),"Scene offer lacks the guest avatar");
            if(p.initial&&p.initial->scene==offer->destination.scene)p.prepared=std::move(p.initial);else{p.initial.reset();p.provider.begin(offer->destination.scene);p.loading=true;}p.text="Preparing scene";}
        if(p.failed)return;if(p.loading){const auto progress=p.provider.poll();p.text=progress.text;if(progress.state==SceneLoad::Failed)throw std::runtime_error(progress.text);if(progress.state!=SceneLoad::Ready)return;p.prepared=p.provider.take();p.loading=false;}
        check(bool(p.prepared),"Scene provider returned no guest scene");p.prepared->hello.session_epoch=offer->destination.hello.session_epoch;
        if(p.mismatch)p.prepared->hello.content_hash=sha256("Injected guest content mismatch");
        check(p.prepared->scene==offer->destination.scene&&compatible_session_handshake(p.prepared->hello,offer->destination.hello),"Scene content or epoch mismatch");
        if(!p.gameplay){p.gameplay=create_gns_client(offer->endpoint,p.prepared->hello);p.game_peer=p.gameplay->open(p.prepared->hello);p.text="Connecting to the scene";}
        if(!p.gameplay->connected())return;
        if(!p.session){// Every avatar a member of this scene can have: the host's, and one per seat for those here or admitted later.
            std::vector<StableId> party;for(const auto& member:offer->destination.members)if(!member.seat)party.push_back(member.avatar);for(unsigned seat=1;seat<scene_party_limit;++seat)party.push_back(party_member(seat));auto& scene=*p.prepared;p.session=std::make_unique<CharacterSceneSession>(scene.hello,*p.gameplay,p.game_peer,p.owner,party,scene.collision,scene.plan,scene.rig,scene.rig_archive,scene.combat,p.profile);}
        p.session->pump();if(!p.reported){auto status=p.session->client(p.owner)->bootstrap_status();if(status!=p.bootstrap){p.bootstrap=std::move(status);std::cout<<"Party guest bootstrap "<<p.bootstrap<<std::endl;}}
        if(p.session->bootstrapped()&&!p.reported){p.party->ready(p.prepared->scene,p.prepared->hello);p.reported=true;std::cout<<"Party guest prepared epoch="<<p.prepared->hello.session_epoch<<" owner="<<p.owner.text()<<std::endl;if(loaded)loaded();}p.text=p.party->phase()!=PartyPhase::Running?"Waiting for the party":p.party->held()?"The party is travelling":"";
    }catch(const std::exception& e){p.error=bounded(e.what());p.text=p.error;p.failed=true;std::cout<<"Party guest failed: "<<p.error<<std::endl;p.party->failed(p.error);p.party->pump();}
}
CharacterSceneSession* EditorPartyGuest::session()const{return impl_->session.get();}
const PreparedScene* EditorPartyGuest::scene()const{return impl_->prepared.get();}
std::vector<PartyMember> EditorPartyGuest::roster()const{const auto& offer=impl_->party->offer();return offer?offer->destination.members:std::vector<PartyMember>{};}
StableId EditorPartyGuest::owner()const{return impl_->owner;}
bool EditorPartyGuest::running()const{return impl_->party->phase()==PartyPhase::Running&&!impl_->party->held()&&impl_->session&&impl_->session->bootstrapped();}
bool EditorPartyGuest::held()const{return impl_->party->held();}
bool EditorPartyGuest::finished()const{return impl_->finished;}
std::uint64_t EditorPartyGuest::epoch()const{return impl_->prepared?impl_->prepared->hello.session_epoch:0;}
std::string EditorPartyGuest::status()const{return impl_->text;}
const std::string& EditorPartyGuest::error()const{return impl_->error;}
void EditorPartyGuest::stop(){auto& p=*impl_;if(p.session&&unloading)unloading();p.session.reset();p.gameplay.reset();if(p.loading){p.provider.cancel();p.loading=false;}p.party->leave();p.finished=true;}
}
