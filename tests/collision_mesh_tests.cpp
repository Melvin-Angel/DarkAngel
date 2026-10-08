#include <darkangel/assets.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace darkangel;
namespace {
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
std::string read(const std::filesystem::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void tick(PhysicsWorld& w,CharacterMotor& m,double x=0,bool jump=false){auto t=w.tick()+1;m.step({t,t,1,x,0,0,jump});w.step();m.post_physics();}
MotorState rendered(const CollisionDefinition& scene,unsigned rate){PhysicsWorld w;w.load_scene(scene);CharacterMotor m(w,{-2,0,0},10);SimulationClock clock;for(unsigned f=0;f<rate*2;++f){auto count=clock.advance(1.0/rate);for(unsigned n=0;n<count;++n)tick(w,m,1);}require(w.tick()==120,"Fixed tick count across render rates");return m.state();}
}
int main(){try{
    auto fixture=std::filesystem::path(DAE_SOURCE_DIR)/"content/physics/m4_cave.dacollision";
    auto native=read(fixture);auto scene=decode_collision_source(native);
    PhysicsWorld w;w.load_scene(scene);
    auto front=w.query_ray({-2,2,0},{0,-4,0});require(front.hits.size()==1&&front.hits[0].identity==7&&front.hits[0].subshape==100&&front.hits[0].normal.y>.99&&w.material_key(front.hits[0])=="Stone","Authored floor key/material/front normal");
    require(w.query_ray({-2,-1,0},{0,2,0}).hits.empty(),"Mesh backface ignored by default");QueryFilter back;back.backfaces=true;auto reverse=w.query_ray({-2,-1,0},{0,2,0},back);require(reverse.hits.size()==1,"Explicit backface admission");
    auto earth=w.query_ray({2,2,0},{0,-4,0});require(earth.hits.size()==1&&earth.hits[0].material==1&&w.material_key(earth.hits[0])=="Earth","Material boundary retains authored index");
    auto roof=w.query_ray({-2,1,0},{0,4,0});require(roof.hits.size()==1&&roof.hits[0].normal.y<-.99&&w.material_key(roof.hits[0])=="Rock","Stacked roof inside-cave winding");
    auto sweep=w.sweep({2,1,0},{0,-2,0},.2);require(!sweep.overflow&&!sweep.hits.empty()&&sweep.hits[0].normal.y>.99,"Mesh sphere sweep returns floor contact");
    auto overlap=w.overlap({2,.1,0},.2);require(!overlap.overflow&&!overlap.hits.empty(),"Mesh sphere overlap");
    auto a=rendered(scene,30),b=rendered(scene,60),c=rendered(scene,144);require(a.position==b.position&&b.position==c.position&&a.position.x>7&&a.grounded,"Cave seam movement is render-independent");
    CharacterMotor motor(w,{-2,0,0},10);tick(w,motor);auto baseline=motor.state();CollisionHistory history;std::vector<MotorInput> commands;
    for(unsigned n=0;n<24;++n){history.retain(w.capture());auto t=w.tick()+1;commands.push_back({t,t,1,1});tick(w,motor,1);}
    auto expected=motor.state();auto retained=w.capture().meshes[0].geometry;w.remove(7);auto after=w.capture();MotorState output;
    require(replay_motor(baseline,commands,history,output,10)==ReplayResult::Applied&&std::abs(output.position.x-expected.position.x)<.001&&output.grounded,"Replay retains removed immutable mesh generation");
    require(w.capture().tick==after.tick&&w.capture().topology==after.topology&&w.capture().meshes.empty()&&motor.state().position==expected.position,"Historical replay never rewinds live world");require(!w.valid_hit(front.hits[0]),"Removed mesh hit expires");
    CollisionHistory changed,expired;for(const auto& command:commands){auto frame=*history.find(command.tick-1);if(command.tick>baseline.tick+1)frame.meshes.clear();changed.retain(frame);if(command.tick!=baseline.tick+2)expired.retain(*history.find(command.tick-1));}
    output.position.x=999;require(replay_motor(baseline,commands,changed,output,10)==ReplayResult::TopologyMismatch&&output.position.x==999,"Unversioned streamed mesh change fails atomically");require(replay_motor(baseline,commands,expired,output,10)==ReplayResult::MissingHistory&&output.position.x==999,"Expired mesh history fails atomically");
    auto mutable_data=std::make_shared<CollisionMeshData>(*scene.meshes[0].data);auto geometry=std::make_shared<CollisionGeometry>(*mutable_data);mutable_data->vertices[0].y=100;require(geometry->definition().vertices[0].y==0,"Geometry freezes source data");
    PhysicsWorld ceiling;ceiling.load_scene(scene);CharacterMotor jumper(ceiling,{0,0,0},10);for(unsigned n=0;n<20;++n)tick(ceiling,jumper);double high=0;for(unsigned n=0;n<60;++n){tick(ceiling,jumper,0,n==0);high=std::max(high,jumper.state().position.y);}require(high>.5&&high<1.25&&jumper.state().grounded,"Cave roof constrains jump and recovers grounded stance");
    auto root=std::filesystem::path(DAE_BINARY_DIR)/("mesh-fixture-"+AssetId::random().text());std::filesystem::create_directories(root/"sources");std::filesystem::copy_file(fixture,root/"sources/cave.dacollision");
    AssetService assets(root/"sources",root/"cache");auto id=assets.adopt("cave.dacollision");auto cooked=assets.cook("cave.dacollision");require(cooked.changed&&!assets.cook("cave.dacollision").changed,"Mesh warm cook");assets.package(id,root/"registry.json");auto loaded=load_cooked_collision_scene(root/"registry.json",assets.cas_path(),id);require(loaded.meshes.size()==1&&loaded.meshes[0].data->signature==scene.meshes[0].data->signature,"Frozen geometry CAS closure");PhysicsWorld runtime;runtime.load_scene(loaded);require(runtime.material_key(runtime.query_ray({2,2,0},{0,-4,0}).hits.at(0))=="Earth","Packaged geometry material query");
    auto bad=nlohmann::json::parse(native);bad["meshes"][0]["triangles"].push_back({999,{2,0,1},0});std::ofstream(root/"sources/cave.dacollision")<<bad.dump();bool rejected=false;try{assets.cook("cave.dacollision");}catch(...){rejected=true;}require(rejected,"Opposite duplicate face rejects before publication");assets.package(id,root/"registry.json");require(load_cooked_collision_scene(root/"registry.json",assets.cas_path(),id).meshes[0].data->signature==scene.meshes[0].data->signature,"Failed mesh cook preserves frozen generation");
    auto assembly=AssetId::random();nlohmann::json source={{"schema",1},{"asset",assembly.text()},{"entities",nlohmann::json::object()},{"mounts",nlohmann::json::object()}};EditorDocument doc(assembly,{7,1},{{assembly,source.dump()},{id,native}});auto edit=nlohmann::json::parse(native);edit["meshes"][0]["materials"][0]="Granite";doc.commit(doc.prepare_source(doc.revision(),id,edit.dump()),"Mesh material edit");doc.undo(doc.revision());require(!doc.dirty(),"Mesh edit undo");doc.redo(doc.revision());doc.save(root/"mesh.dadoc");require(EditorDocument::open(root/"mesh.dadoc")->source(id)==edit.dump(),"Mesh native save/Open");bool invalid=false;try{doc.prepare_source(doc.revision(),id,bad.dump());}catch(...){invalid=true;}require(invalid,"Invalid mesh authoring is atomic");
    std::cout<<"Static cave mesh authored keys/materials/backfaces, seams/roof, 30/60/144 ticks, immutable removed-generation replay, CAS and authoring passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
