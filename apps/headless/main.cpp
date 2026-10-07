#include <darkangel/bootstrap.hpp>
#include <darkangel/script_runtime.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
int main(int argc,char** argv) {
    if(argc==1)return darkangel::bootstrap();
    try {
        if(argc!=3 || std::string_view(argv[1])!="--script")throw std::runtime_error("Usage: DarkAngelHeadless --script cooked-package");
        std::ifstream in(argv[2],std::ios::binary|std::ios::ate);if(!in || in.tellg()>4*1024*1024)throw std::runtime_error("Cooked script input limit/open failure");
        in.seekg(0);std::string artifact{std::istreambuf_iterator<char>(in),{}};auto package=darkangel::CookedScriptPackage::deserialize(artifact);
        auto root=std::find_if(package.modules.begin(),package.modules.end(),[&](const auto& m){return m.path==package.entry;});
        darkangel::World world(darkangel::WorldDomain::Server);darkangel::ObjectData data;data.id={0xfeed,1};auto h=world.create(data);darkangel::ScriptRuntime scripts(world);
        if(!scripts.load_cooked({root->asset,1,darkangel::Authority::Server,1},artifact))throw std::runtime_error(scripts.diagnostic());
        scripts.attach(h);world.begin_scripts();scripts.tick(.25);world.commit();world.publish();if(!scripts.enabled(h))throw std::runtime_error(scripts.diagnostic());
        std::cout<<"Cooked headless script yaw="<<world.read(h).transform.yaw<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
