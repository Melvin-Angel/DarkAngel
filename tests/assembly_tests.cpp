#include <darkangel/assembly.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void check(bool test,const char* error){if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"Expected assembly rejection");}
int main(int argc,char** argv){try{
    const auto assembly=AssetId::parse("11111111-1111-4111-8111-111111111111"),scene=AssetId::parse("22222222-2222-4222-8222-222222222222");
    const auto script=StableId::parse("33333333333343338333333333333333");
    const std::string definition=R"({"schema":1,"asset":"11111111-1111-4111-8111-111111111111","entities":{
      "00000000000000000000000000000001":{"name":"Spinner","types":{"1":{"version":1,"fields":{"1":0}},"2":{"version":1,"fields":{"1":100,"2":100}}},"scripts":{"00000000000000000000000000000009":{"asset":"33333333-3333-4333-8333-333333333333","config":{"speed":1},"enabled":true}}}},"exports":{"00000000000000000000000000000008":"00000000000000000000000000000001"}})";
    const std::string placements=R"({"schema":1,"asset":"22222222-2222-4222-8222-222222222222","entities":{},"mounts":{
       "00000000000000000000000000000002":{"asset":"11111111-1111-4111-8111-111111111111","patches":[]},
       "00000000000000000000000000000003":{"asset":"11111111-1111-4111-8111-111111111111","patches":[{"op":"SetProperty","target":"00000000000000000000000000000001","type":1,"property":1,"value":10}]}}})";
    AssemblySources sources{{assembly,definition},{scene,placements}};auto plan=resolve_assembly(scene,{1,1},sources);check(plan.origins.size()==2 && plan.bindings.size()==2,"Assembly did not produce independent placements");
    check(resolve_assembly(scene,{1,1},sources).serialize()==plan.serialize(),"Assembly resolve is not deterministic");
    auto duplicated=resolve_assembly(scene,{1,2},sources);check(duplicated.origins[0].object!=plan.origins[0].object,"Duplicating placement recycled persistent identity");
    const std::string code=R"(--!strict
return {update=function(ctx:Context,state:BehaviorState,config:BehaviorConfig,dt:number) state.angle += config.speed*dt;ctx:set_yaw(state.angle) end}
)";
    ScriptDescriptor descriptor{script,1,Authority::Server,1};ScriptPackage package{"spin",{{script,"spin",code,{}}},{}};
    std::map<StableId,ScriptDefinition> scripts{{script,{descriptor,cook_scripts(package).serialize()}}};SceneSession session(WorldDomain::Server);auto prepared=session.prepare(plan,scripts);check(!session.active(),"Preparation exposed partial scene");session.activate(std::move(prepared));session.step(.5);
    auto* old=session.active();auto first=old->world->find(plan.origins[0].object),second=old->world->find(plan.origins[1].object);check(old->world->read(first).transform.yaw==.5 && old->world->read(second).transform.yaw==10.5,"Assembly mutable/script state shared");
    rejects([&]{session.prepare(plan,{});});check(session.active()==old && old->world->valid(first),"Failed preparation retired active scene");
    auto decoded=SpawnPlan::deserialize(plan.serialize());check(decoded.serialize()==plan.serialize(),"Cooked spawn plan round trip mismatch");session.activate(session.prepare(decoded,scripts));check(!session.active()->world->valid(first),"Structural restart retained stale handles");session.retire();check(!session.active(),"Scene retirement failed");
    auto cyclic=placements;cyclic.replace(cyclic.find(assembly.text()),36,scene.text());sources[scene]=cyclic;rejects([&]{resolve_assembly(scene,{1,1},sources);});
    if(argc==2){std::ofstream out(argv[1],std::ios::binary);out<<CookedScene{plan,scripts}.serialize();check(out.good(),"Cooked scene fixture output failed");}
    std::cout<<"Assembly identity/patch/migration/prepare/activate/rollback checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
