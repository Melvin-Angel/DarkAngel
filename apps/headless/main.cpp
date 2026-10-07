#include <darkangel/bootstrap.hpp>
#include <darkangel/script_runtime.hpp>
#include <darkangel/assembly.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
int main(int argc,char** argv) {
    if(argc==1)return darkangel::bootstrap();
    try {
        if(argc!=3 || (std::string_view(argv[1])!="--script" && std::string_view(argv[1])!="--scene"))throw std::runtime_error("Usage: DarkAngelHeadless --script cooked-package | --scene cooked-scene");
        std::ifstream in(argv[2],std::ios::binary|std::ios::ate);if(!in || in.tellg()<0 || in.tellg()>16*1024*1024)throw std::runtime_error("Cooked input limit/open failure");
        if(std::string_view(argv[1])=="--scene"){in.seekg(0);std::string artifact{std::istreambuf_iterator<char>(in),{}};auto cooked=darkangel::CookedScene::deserialize(artifact);darkangel::SceneSession session(darkangel::WorldDomain::Server);session.activate(session.prepare(cooked.plan,cooked.scripts));session.step(.5);auto* active=session.active();std::cout<<"Cooked headless scene objects="<<active->plan.origins.size()<<" bindings="<<active->scripts->instance_count();if(!active->plan.origins.empty())std::cout<<" yaw="<<active->world->read(active->world->find(active->plan.origins.front().object)).transform.yaw;std::cout<<'\n';return 0;}
        in.seekg(0);std::string artifact{std::istreambuf_iterator<char>(in),{}};auto package=darkangel::CookedScriptPackage::deserialize(artifact);
        auto root=std::find_if(package.modules.begin(),package.modules.end(),[&](const auto& m){return m.path==package.entry;});
        darkangel::World world(darkangel::WorldDomain::Server);darkangel::ObjectData data;data.id={0xfeed,1};auto h=world.create(data);darkangel::ScriptRuntime scripts(world);
        if(!scripts.load_cooked({root->asset,1,darkangel::Authority::Server,1},artifact))throw std::runtime_error(scripts.diagnostic());
        scripts.attach(h);world.begin_scripts();scripts.tick(.25);world.commit();world.publish();if(!scripts.enabled(h))throw std::runtime_error(scripts.diagnostic());
        std::cout<<"Cooked headless script yaw="<<world.read(h).transform.yaw<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
