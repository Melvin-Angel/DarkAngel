#include <darkangel/script_runtime.hpp>
#include <darkangel/hash.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void check(bool test,const char* error){if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(...){failed=true;}check(failed,"Expected rejection");}
StableId id(unsigned n){return {0xfeed,n};}
ScriptPackage package(double multiplier=2){
    return {"game/spin",{{id(100),"game/spin",R"(--!strict
local mathModule = require('@game/math')
type State = {angle:number}
type Config = {speed:number}
return {update=function(ctx:Context, state:State, config:Config, dt:number)
    state.angle += mathModule.delta(config.speed,dt)
    ctx:set_yaw(state.angle)
end}
)",{"game/math"}}, {id(101),"game/math","--!strict\nreturn {delta=function(speed:number,dt:number):number return speed*dt*"+std::to_string(multiplier)+" end}",{}}},{{"game","game"}}};
}
EntityHandle make(World& w,unsigned n){ObjectData d;d.id=id(n);return w.create(d);}
void step(World& w,ScriptRuntime& s,double dt=.25){w.begin_scripts();s.tick(dt);w.commit();w.publish();}
void imports(){
    check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 mismatch");
    auto source=package();auto cooked=cook_scripts(source);check(cooked.modules.size()==2,"Dependency closure lost");
    World w(WorldDomain::Server);auto h=make(w,1);ScriptRuntime runtime(w);ScriptDescriptor descriptor{id(100),1,Authority::Server,1};
    check(runtime.reload(descriptor,source),runtime.diagnostic().c_str());runtime.attach(h);step(w,runtime);check(runtime.state(h)==.5,"Declared import failed");
    auto generation=runtime.generation();source.modules[1].source="--!strict\nerror('dependency injected failure'); return {}";
    check(!runtime.reload(descriptor,source),"Broken dependency accepted");check(runtime.generation()==generation,"Failed dependency replaced old generation");step(w,runtime);check(runtime.state(h)==1,"Old imported function was not retained");
    check(runtime.reload(descriptor,package(4)),runtime.diagnostic().c_str());step(w,runtime);check(runtime.state(h)==2,"Cached dependent not reloaded");
    source=package();source.modules[0].source.replace(source.modules[0].source.find("'@game/math'"),12,"'./math'");
    check(resolve_script_import("game/spin","./math",source.aliases)=="game/math","Relative alias agreement failed");
    check(runtime.reload(descriptor,source),"Relative import differs between analyzer and runtime");
    source=package();source.modules[0].dependencies.clear();rejects([&]{cook_scripts(source);});
    source=package();source.modules[1].dependencies={"game/spin"};source.modules[1].source="--!strict\nlocal root = require('./spin'); return {}";rejects([&]{cook_scripts(source);});
    source=package();source.modules[0].source="--!strict\nlocal path = '@game/math'; local dep=require(path);return {}";rejects([&]{cook_scripts(source);});
    check(runtime.metrics().calls>0 && runtime.metrics().total_microseconds>=runtime.metrics().last_microseconds,"Metrics unavailable");
}
void cooked(const char* output){
    auto source=package();auto artifact=cook_scripts(source).serialize();check(cook_scripts(source).serialize()==artifact,"Cook is not deterministic");
    if(output){std::ofstream out(output,std::ios::binary);out<<artifact;check(out.good(),"Cook output failed");return;}
    World w(WorldDomain::Server);auto h=make(w,1);ScriptRuntime runtime(w);ScriptDescriptor d{id(100),1,Authority::Server,1};
    check(runtime.load_cooked(d,artifact),runtime.diagnostic().c_str());runtime.attach(h);step(w,runtime);check(w.read(h).transform.yaw==.5,"Cooked behavior failed");
    auto generation=runtime.generation();auto bad=artifact;bad[bad.find("0.729")]='9';check(!runtime.load_cooked(d,bad),"Wrong ABI accepted");
    bad=artifact;auto key=bad.find("code\":\"")+7;bad[key]=bad[key]=='a'?'b':'a';check(!runtime.load_cooked(d,bad),"Corrupt cooked code accepted");check(runtime.generation()==generation,"Bad artifact replaced generation");
}
void tasks(){
    const std::string source=R"(--!strict
type State = {angle:number}
type Config = {speed:number}
return {
 update=function(ctx:Context,state:State,config:Config,dt:number) end,
 task=function(ctx:Context,state:State,config:Config)
    ctx:wait_event('go')
    state.angle += 1
    ctx:set_yaw(state.angle)
    ctx:wait_seconds(.5)
    state.angle += 2
    ctx:set_yaw(state.angle)
 end
}
)";
    World w(WorldDomain::Server);auto a=make(w,1),b=make(w,2);ScriptLimits limits;limits.tasks=2;limits.resumes_per_tick=1;limits.events=2;
    ScriptRuntime runtime(w,limits);ScriptDescriptor d{id(100),1,Authority::Server,1};check(runtime.reload(d,source),runtime.diagnostic().c_str());runtime.attach(a);runtime.attach(b);
    auto first=runtime.start_task(a);runtime.start_task(b);rejects([&]{runtime.start_task(a);});step(w,runtime);step(w,runtime);
    check(runtime.tasks().size()==2 && runtime.tasks()[0].waiting=="event:go","Event wait failed");
    auto generation=runtime.generation();check(!runtime.reload(d,"--!strict\nreturn {update=function("),"Bad reload accepted");check(runtime.tasks().size()==2 && runtime.generation()==generation,"Failed reload cancelled old tasks");
    runtime.signal_event("go");step(w,runtime);check(runtime.state(a)==1 && runtime.state(b)==0,"Resume budget ignored");
    // A delivered event must stay queued for a waiter skipped due to the resume budget.
    step(w,runtime);check(runtime.state(b)==1,"Budget dropped an event-wait continuation");step(w,runtime);check(runtime.state(a)==3,"Simulation wait failed");
    runtime.cancel_task(runtime.tasks().front().token);check(runtime.tasks().empty(),"Task cancellation failed");
    runtime.start_task(a);step(w,runtime);check(runtime.reload(d,source),"Good reload failed");check(runtime.tasks().empty() && runtime.state(a)==3,"Successful reload did not cancel stacks/preserve data");
    runtime.start_task(b);w.destroy(b,Authority::Server);step(w,runtime);check(runtime.tasks().empty() && runtime.instance_count()==1,"Deleted owner retained coroutine");
    runtime.start_task(a);runtime.detach(a);check(runtime.tasks().empty(),"Detach retained task");
    (void)first;
}
void bindings(){
    World world(WorldDomain::Server);auto a=make(world,1),b=make(world,2);ScriptRuntime runtime(world);
    ScriptDescriptor first{id(100),1,Authority::Server,1},second{id(200),1,Authority::Server,1};
    auto source=package();check(runtime.reload(first,source),runtime.diagnostic().c_str());source.modules[0].asset=second.asset;source.modules[1].asset=id(201);check(runtime.reload(second,source),runtime.diagnostic().c_str());
    runtime.attach(a,{id(10),first.asset,"{\"speed\":2}",true});runtime.attach(b,{id(11),first.asset,"{\"speed\":3}",true});runtime.attach(a,{id(12),second.asset,"{\"speed\":4}",false});step(world,runtime);
    check(runtime.binding_state(id(10)).find("1.0")!=std::string::npos && runtime.binding_state(id(11)).find("1.5")!=std::string::npos,"Per-binding configuration/state shared");
    runtime.enable_binding(id(10),false);runtime.enable_binding(id(12),true);step(world,runtime);check(runtime.binding_state(id(10)).find("1.0")!=std::string::npos && runtime.binding_state(id(12)).find("2.0")!=std::string::npos,"Enable/re-enable lifecycle failed");
    auto before=runtime.binding_state(id(12));check(runtime.reload(first,package(4)),runtime.diagnostic().c_str());check(runtime.binding_state(id(12))==before,"Other asset reload migrated unrelated attachment");
    runtime.enable_binding(id(12),false);runtime.detach_binding(id(10));runtime.attach(a,{id(10),first.asset,"{\"speed\":1}",true});rejects([&]{runtime.attach(a,{id(10),first.asset,"{}",true});});
    rejects([&]{runtime.configure_binding(id(11),"{\"speed\":\"bad\"}");});runtime.detach_binding(id(12));check(runtime.bindings().size()==2 && runtime.bindings()[0].metrics.calls>0,"Binding inspection/profile unavailable");
}
void declared_state(){
    ScriptDescriptor d{id(100),1,Authority::Server,1};
    d.state_schema=R"({"type":"record","fields":{
      "angle":{"id":1,"type":"number","default":0},
      "label":{"id":7,"type":"string","max_bytes":32,"default":"spin"},
      "active":{"id":8,"type":"boolean","default":true},
      "counter":{"id":9,"type":"uint64","default":"18446744073709551615"},
      "target":{"id":10,"type":"entity"},
      "material":{"id":11,"type":"asset"},
      "history":{"id":12,"type":"array","max_count":4,"element":{"type":"number"}},
      "nested":{"id":13,"type":"record","fields":{"value":{"id":3,"type":"number","default":2}}}
    }})";
    const std::string source=R"(--!strict
return {update=function(ctx:Context,state:BehaviorState,config:{speed:number},dt:number)
  state.angle += dt
  state.nested.value += 1
  table.insert(state.history,state.angle)
  state.target = '000000000000feed0000000000000001'
  state.material = 'd10003ce-2f74-41ea-928c-7cf71c8476b3'
  ctx:set_yaw(state.angle)
end}
)";
    World w(WorldDomain::Server);auto a=make(w,1),b=make(w,2);ScriptRuntime runtime(w);
    check(runtime.reload(d,source),runtime.diagnostic().c_str());runtime.attach(a);runtime.attach(b);step(w,runtime);
    auto snapshot=runtime.state_json(a);check(snapshot.find("18446744073709551615")!=snapshot.npos && snapshot.find("\"7\":\"spin\"")!=snapshot.npos && snapshot.find("\"13\":{\"3\":3.0}")!=snapshot.npos,"Stable declared state snapshot lost types/IDs");
    auto generation=runtime.generation();auto changed=d;changed.state_schema.replace(changed.state_schema.find("\"max_count\":4"),13,"\"max_count\":3");
    check(!runtime.reload(changed,source),"Schema changed without version increment");check(runtime.generation()==generation && runtime.state_json(a)==snapshot,"Failed schema reload changed live state");
    auto v2=d;v2.state_version=2;
    auto invalid=source;invalid.replace(invalid.find("return {update"),14,"return {migrate=function(version:number,old:any):any old.nested.value='bad';return old end, update");
    check(!runtime.reload(v2,invalid),"Invalid nested migration accepted");check(runtime.generation()==generation && runtime.state_json(a)==snapshot,"Failed nested migration mutated old data");
    auto good=source;good.replace(good.find("return {update"),14,"return {migrate=function(version:number,old:any):BehaviorState old.label='migrated';return old end, update");
    check(runtime.reload(v2,good),runtime.diagnostic().c_str());check(runtime.state_json(a).find("migrated")!=std::string::npos,"Declared state migration failed");
    step(w,runtime);step(w,runtime);step(w,runtime);check(runtime.enabled(a),"Bounded array rejected before capacity");step(w,runtime);check(!runtime.enabled(a) && !runtime.enabled(b),"Declared array limit ignored");
    auto cycle=source;cycle.replace(cycle.find("state.angle += dt"),17,"(state :: any).nested = state");
    v2.state_version=1;ScriptPackage p{"behavior",{{d.asset,"behavior",cycle,{}}},{},d.state_schema};auto artifact=cook_scripts(p).serialize();
    World isolated(WorldDomain::Server);auto h=make(isolated,3);ScriptRuntime cooked_runtime(isolated);check(cooked_runtime.load_cooked(d,artifact),cooked_runtime.diagnostic().c_str());cooked_runtime.attach(h);step(isolated,cooked_runtime);check(!cooked_runtime.enabled(h),"Cyclic runtime state accepted");
    const std::string task_source=R"(--!strict
return {update=function(ctx:Context,state:BehaviorState,config:{speed:number},dt:number) state.nested.value += 1 end,
task=function(ctx:Context,state:BehaviorState,config:{speed:number})
 local nested=state.nested
 ctx:wait_event('continue')
 nested.value += 10
end}
)";
    World task_world(WorldDomain::Server);auto owner=make(task_world,4);ScriptRuntime task_runtime(task_world);check(task_runtime.reload(d,task_source),task_runtime.diagnostic().c_str());task_runtime.attach(owner);task_runtime.start_task(owner);step(task_world,task_runtime);task_runtime.signal_event("continue");step(task_world,task_runtime);
    check(task_runtime.state_json(owner).find("\"13\":{\"3\":14.0}")!=std::string::npos,"Task wait lost nested alias identity or overwrote update data");
}
int main(int argc,char** argv){try{check(argc>=2,"Missing case");std::string mode=argv[1];if(mode=="imports")imports();else if(mode=="tasks")tasks();else if(mode=="bindings")bindings();else if(mode=="state")declared_state();else if(mode=="cooked")cooked(argc==3?argv[2]:nullptr);else throw std::runtime_error("Unknown case");std::cout<<"M1 "<<mode<<" passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
