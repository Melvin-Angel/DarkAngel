#include <darkangel/assets.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/editor_document.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace darkangel;
void require(bool b,const char* s){
    if(!b)throw std::runtime_error(s);
}
int main(){
    try{
        auto root=std::filesystem::path(DAE_BINARY_DIR)/("collision-fixture-"+AssetId::random().text());
        std::filesystem::create_directories(root/"sources");
        auto path=root/"sources/arena.dacollision";
        auto write=[&](const char* s){
            std::ofstream(path)<<s;
        };
        write(R"({"asset":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","kind":"collision","schema":1,"boxes":[{"id":"1","center":[0,-0.5,0],"half":[20,0.5,20],"yaw":0,"roll":0,"moving":false}]})");
        AssetService assets(root/"sources",root/"cache");
        auto id=assets.adopt("arena.dacollision");
        auto first=assets.cook("arena.dacollision"),warm=assets.cook("arena.dacollision");
        require(first.changed&&!warm.changed&&warm.generation==first.generation,"Collision warm cook");
        assets.package(id,root/"registry.json");
        auto cooked=load_cooked_collision(root/"registry.json",assets.cas_path(),id);
        PhysicsWorld world;
        world.load_cooked(cooked);
        CharacterMotor motor(world,{
            0,0,0
        });
        for(unsigned n=1;n<=60;++n){
            motor.step({
                n,n,1,1
            });
            world.step();
            motor.post_physics();
        }require(motor.state().position.x>4&&motor.state().grounded,"Cooked collision motor headless");
        write(R"({"asset":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa","kind":"collision","schema":1,"boxes":[{"id":"1","center":[0,0,0],"half":[-1,1,1],"yaw":0,"roll":0,"moving":false}]})");
        bool denied=false;
        try{
            assets.cook("arena.dacollision");
        }catch(...){
            denied=true;
        }require(denied,"Invalid collision cook rejected");
        assets.package(id,root/"registry.json");
        require(load_cooked_collision(root/"registry.json",assets.cas_path(),id)==cooked,"Failed collision generation retains prior product");
        auto crate_path=std::filesystem::path(DAE_SOURCE_DIR)/"content/physics/m4_crate.dacollision";
        std::filesystem::copy_file(crate_path,root/"sources/crate.dacollision");
        {std::ifstream fixture(root/"sources/crate.dacollision");nlohmann::json fields;fixture>>fields;fields["boxes"].push_back({{"id","3"},{"center",{0,1,0}},{"half",{3,1,3}},{"yaw",0},{"roll",0},{"motion","sensor"}});std::ofstream(root/"sources/crate.dacollision")<<fields.dump();}
        auto crate_id=assets.adopt("crate.dacollision");auto crate_cook=assets.cook("crate.dacollision");
        require(crate_cook.changed&&!assets.cook("crate.dacollision").changed,"Dynamic collision native UUID and warm cook");
        assets.package(crate_id,root/"crate.registry.json");auto crate_bytes=load_cooked_collision(root/"crate.registry.json",assets.cas_path(),crate_id);
        PhysicsWorld server;server.load_cooked(crate_bytes);CharacterMotor pusher(server,{0,0,0});
        require(server.capture().boxes[2].sensor,"Native source sensor cooks into a nonblocking query shape");
        for(unsigned n=1;n<=50;++n){pusher.step({n,n,1,1});server.step();pusher.post_physics();}
        require(server.capture().boxes[1].center.x>1.6,"Cooked dynamic crate can be pushed by server motor");
        PhysicsWorld client(PhysicsWorld::Mode::Prediction);client.load_cooked(crate_bytes);bool no_impulse=false;try{client.apply_impulse(2,{1,0,0});}catch(...){no_impulse=true;}
        require(no_impulse&&client.capture().boxes[1].dynamic,"Cooked client dynamic body becomes authoritative proxy");
        std::ifstream input(crate_path);std::string native{std::istreambuf_iterator<char>(input),{}};auto definition=decode_collision_source(native);
        auto assembly_id=AssetId::random();nlohmann::json assembly={{"schema",1},{"asset",assembly_id.text()},{"entities",nlohmann::json::object()},{"mounts",nlohmann::json::object()}};
        EditorDocument document(assembly_id,{7,1},{{assembly_id,assembly.dump()},{definition.id,native}});
        auto edited=nlohmann::json::parse(native);edited["boxes"][1]["mass"]=75;
        document.commit(document.prepare_source(document.revision(),definition.id,edited.dump()),"M4 native collision authoring");
        require(document.dirty(),"Collision source edit shares document dirty state");document.undo(document.revision());require(!document.dirty(),"Collision undo restores saved source");document.redo(document.revision());document.save(root/"collision.dadoc");
        auto reopened=EditorDocument::open(root/"collision.dadoc");require(reopened->source(definition.id)==document.source(definition.id),"Collision source save/Open preserves identity and motion");
        edited["boxes"][1]["mass"]=0;bool invalid_edit=false;try{document.prepare_source(document.revision(),definition.id,edited.dump());}catch(...){invalid_edit=true;}
        require(invalid_edit,"Invalid dynamic source rejected before authoring commit");
        std::cout<<"M4 UUID collision catalog/CAS warm cook, native/dynamic headless motor, authoring transactions and failed-generation preservation passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
