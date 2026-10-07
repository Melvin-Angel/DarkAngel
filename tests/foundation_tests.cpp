#include <darkangel/script_runtime.hpp>
#include <darkangel/jobs.hpp>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace darkangel;
void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F&& action) { bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"Expected rejection"); }
StableId id(std::uint64_t low) {return {0xfeed,low};}
ObjectData object(std::uint64_t low,double yaw=0) {ObjectData d;d.id=id(low);d.transform.yaw=yaw;return d;}
std::string spin() {
    std::ifstream in(DAE_SOURCE_DIR "/games/AshenRoots/scripts/spin.luau");check(in.good(),"Spin fixture missing");
    return {std::istreambuf_iterator<char>(in),{}};
}
void step(World& w,ScriptRuntime& scripts,double dt=0.25) {w.begin_scripts();scripts.tick(dt);w.commit();w.publish();}
void identity() {
    World a(WorldDomain::Authoring),b(WorldDomain::Server);
    auto h=a.create(object(1));check(a.valid(h) && !b.valid(h),"Wrong-world handle accepted");
    rejects([&]{b.read(h);});a.destroy(h,Authority::Authoring);check(!a.valid(h),"Deleted handle accepted");
    auto replacement=a.create(object(2));check(h.runtime!=replacement.runtime,"Generation did not change");
    rejects([&]{a.read(h);});a.reset();check(!a.valid(replacement),"Epoch did not invalidate handle");
    auto again=a.create(object(2));check(again.epoch!=replacement.epoch,"Reset did not change epoch");
    rejects([&]{a.create(object(2));});
    auto forged=again;forged.runtime=1;check(!a.valid(forged),"Internal Flecs ID accepted as object handle");rejects([&]{a.read(forged);});
    bool rejected=false;std::thread worker([&]{try{a.read(again);}catch(...){rejected=true;}});worker.join();check(rejected,"Wrong thread accepted");
    check(a.reflection_backed(),"Owned types missing Flecs Meta");
    bool replicated=false;for(const auto& t:metadata())for(const auto& p:t.properties)if(t.id==2 && p.id==2)replicated=(p.flags&Replicated)!=0;
    check(replicated,"Health.Current replication metadata missing");
    check(luau_types().find("current: number")!=std::string::npos,"Generated types missing current");
}
void scene() {
    World authoring(WorldDomain::Authoring);auto d=object(1,2);d.target=id(2);d.network.value=std::numeric_limits<std::uint64_t>::max();
    auto h=authoring.create(d);authoring.create(object(2));auto json=authoring.serialize();
    check(json.find(id(1).text())!=std::string::npos,"Stable ID precision lost");
    check(json.find("18446744073709551615")==std::string::npos,"Session identity leaked into scene");
    World play(WorldDomain::Server);auto loaded=play.load(json);check(loaded.size()==2,"Scene count mismatch");
    check(play.serialize()==json,"Canonical scene round trip mismatch");
    check(play.target(play.find(id(1)))==play.find(id(2)),"Reference remap failed");
    rejects([&]{play.read(h);});
    authoring.edit_number(h,2,2,12,Authority::Authoring);check(play.read(play.find(id(1))).health.current==100,"Authoring/play share state");
    rejects([&]{authoring.edit_number(h,2,2,101,Authority::Authoring);});
    check(authoring.read(h).health.current==12,"Failed edit changed state");
    rejects([&]{authoring.edit_number(h,3,1,4,Authority::Authoring);});
    World empty(WorldDomain::Server);
    auto bad=json;bad.replace(bad.find("\"version\":1"),11,"\"version\":9");rejects([&]{empty.load(bad);});
    check(!empty.find(id(1)).runtime,"Failed load populated world");
    rejects([&]{empty.load("{\"version\":1,\"version\":1,\"objects\":[]}");});
    rejects([&]{empty.load("{\"version\":1,\"objects\":[NaN]}");});
    bad=json;bad.replace(bad.find(id(2).text()),32,id(99).text());rejects([&]{empty.load(bad);});
    rejects([&]{StableId::parse("00000000000000000000000000000000");});
}
void phases() {
    World w(WorldDomain::Server,4,2);auto a=w.create(object(1)),b=w.create(object(2));
    rejects([&]{w.stage_yaw(a,1,Authority::Server);});w.begin_scripts();
    w.stage_yaw(a,1,Authority::Server);check(w.read(a).transform.yaw==0,"Write visible before commit");
    rejects([&]{w.stage_yaw(b,2,Authority::Presentation);});rejects([&]{w.create(object(3));});
    w.destroy(b,Authority::Server);check(w.valid(b),"Structural write visible before merge");
    rejects([&]{w.stage_yaw(a,3,Authority::Server);});w.commit();
    check(w.read(a).transform.yaw==1 && !w.valid(b),"Merge failed");
    rejects([&]{w.begin_scripts();});w.publish();
    rejects([&]{w.commit();});
    World client(WorldDomain::ClientPresentation);auto ch=client.create(object(1));client.begin_scripts();
    rejects([&]{client.stage_yaw(ch,1,Authority::Server);});client.stage_yaw(ch,2,Authority::Presentation);client.commit();client.publish();
    Jobs jobs(w,2);unsigned ran{};
    auto token=jobs.submit(a,1,[&]{++ran;});jobs.submit(a,1,[&]{++ran;});
    rejects([&]{jobs.submit(a,1,[]{});});jobs.cancel(token);check(jobs.drain(1)==1 && ran==1,"Job cancel/drain failed");
    jobs.submit(a,1,[&]{++ran;});jobs.cancel_owner(a,1);check(jobs.pending()==0,"Generation jobs not cancelled");
    jobs.submit(a,2,[&]{++ran;});w.destroy(a,Authority::Server);jobs.drain(1);check(ran==1,"Stale job executed");
}
void scripts() {
    World w(WorldDomain::Server);auto a=w.create(object(1)),b=w.create(object(2,10));ScriptRuntime s(w);
    rejects([&]{ScriptRuntime duplicate(w);});
    check(s.reload({id(100),1,Authority::Server,2},spin()),s.diagnostic().c_str());s.attach(a);s.attach(b);
    w.begin_scripts();s.tick(.5);check(w.read(a).transform.yaw==0,"Script bypassed staging");w.commit();w.publish();
    check(w.read(a).transform.yaw==1 && w.read(b).transform.yaw==11,"Instances share state");
    check(s.state(a)==1 && s.state(b)==11,"Explicit state mismatch");
    World client(WorldDomain::ClientPresentation);auto c=client.create(object(1));ScriptRuntime cs(client);
    check(cs.reload({id(100),1,Authority::Server,2},spin()),"Client compile failed");cs.attach(c);step(client,cs);
    check(client.read(c).transform.yaw==0 && !cs.enabled(c),"Client performed server mutation");
    check(s.enabled(a),"Client VM fault leaked to server VM");
    w.destroy(a,Authority::Server);step(w,s);check(s.instance_count()==1,"Deletion did not clean binding");
    rejects([&]{s.state(a);});s.detach(b);check(s.instance_count()==0,"Detach failed");
}
void reload() {
    World w(WorldDomain::Server);auto h=w.create(object(1));ScriptRuntime s(w);auto descriptor=ScriptDescriptor{id(100),1,Authority::Server,2};
    check(s.reload(descriptor,spin()),"Initial reload failed");s.attach(h);step(w,s);auto generation=s.generation();
    check(!s.reload(descriptor,"--!strict\nreturn { update = function("),"Bad syntax accepted");check(s.generation()==generation,"Compile failure replaced generation");
    step(w,s);check(s.state(h)==1,"Old behavior not preserved");
    descriptor.state_version=2;
    check(!s.reload(descriptor,"--!strict\nreturn { update=function() end, migrate=function() error('injected migration failure') end }"),"Failed migration accepted");
    check(s.diagnostic().find("injected migration failure")!=std::string::npos,"Migration failure was not exercised in the VM");
    check(s.generation()==generation && s.state(h)==1,"Failed migration changed active state");
    check(!s.reload(descriptor,"--!strict\nreturn { update=function() end, migrate=function(_, s) s.angle=999; return { angle='bad' } end }"),"Invalid state accepted");
    check(s.state(h)==1,"Migration mutated old state");
    auto code=spin();auto pos=code.find("return { angle = oldState.angle }");code.replace(pos,33,"return { angle = oldState.angle + 10 }");
    check(s.reload(descriptor,code),s.diagnostic().c_str());check(s.state(h)==11,"Migration not committed");step(w,s);check(s.state(h)==11.5,"New generation not active");
    auto before=s.generation();w.begin_scripts();check(!s.reload(descriptor,spin()),"Reload outside safe point accepted");check(s.generation()==before,"Unsafe reload changed generation");w.commit();w.publish();
    check(!s.reload(descriptor,"--!strict\nlocal count=0; return { update=function() count+=1 end }"),"Hidden mutable closure accepted");
    check(!s.reload(descriptor,"--!strict\nlocal speed: number = 'bad'; return { update=function() end }"),"Strict type error accepted");
    check(!s.reload(descriptor,"--!strict\n--!nocheck\nreturn {update=function() end}"),"Strict analysis bypass accepted");
}
void limits() {
    World w(WorldDomain::Server,2,1);auto h=w.create(object(1));ScriptLimits limits;limits.instances=1;limits.call_time=std::chrono::milliseconds(10);ScriptRuntime s(w,limits);
    auto d=ScriptDescriptor{id(100),1,Authority::Server,1};
    check(!s.reload(d,"--!strict\nwhile true do end"),"Runaway top-level not interrupted");
    check(s.reload(d,"--!strict\nreturn { update=function() while true do end end }"),"Loop fixture failed to load");s.attach(h);
    auto start=std::chrono::steady_clock::now();step(w,s);check(!s.enabled(h),"Runaway instance not disabled");
    check(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"Interrupt exceeded bound");
    check(s.memory_bytes()<limits.memory_bytes,"VM memory quota exceeded");
    rejects([&]{s.attach(w.create(object(2)));});
    rejects([&]{w.create(object(3));});
    check(!s.reload(d,std::string(limits.source_bytes+1,' ')),"Source quota missing");
    check(!s.reload(d,"--!strict\nlocal huge = string.rep('x',16777216); return { update=function() end }"),"Oversized allocation accepted");
    check(s.diagnostic().find("memory")!=std::string::npos,"Memory quota did not reject oversized allocation");
    check(s.memory_bytes()<limits.memory_bytes,"Failed candidate retained oversized VM allocations");
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected case argument");std::string name=argv[1];
        if(name=="identity")identity();else if(name=="scene")scene();else if(name=="phases")phases();else if(name=="scripts")scripts();else if(name=="reload")reload();else if(name=="limits")limits();else throw std::runtime_error("Unknown case");
        std::cout<<"M1 "<<name<<" passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"M1 failed: "<<e.what()<<'\n';return 1;}
}
