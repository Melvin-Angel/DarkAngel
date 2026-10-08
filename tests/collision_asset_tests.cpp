#include <darkangel/assets.hpp>
#include <darkangel/character_motor.hpp>
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
        std::cout<<"M4 UUID collision catalog/CAS warm cook, native headless motor and failed-generation preservation passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
