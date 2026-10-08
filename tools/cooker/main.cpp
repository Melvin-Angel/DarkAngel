#include <darkangel/assets.hpp>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{
    if(argc<4)throw std::runtime_error("AssetTool scan SOURCE CACHE | adopt-human SOURCE CACHE relative-file canonical-skeleton | adopt/cook SOURCE CACHE relative-file | package SOURCE CACHE UUID registry | inspect registry CAS UUID");
    std::string mode=argv[1];
    if(mode=="inspect"){if(argc!=5)throw std::runtime_error("inspect arguments");auto model=darkangel::load_cooked_model(argv[2],argv[3],darkangel::AssetId::parse(argv[4]));std::size_t vertices{},triangles{};for(const auto& mesh:model.meshes){vertices+=mesh.vertices.size();triangles+=mesh.indices.size()/3;}std::cout<<"Cooked model "<<model.id.text()<<" meshes="<<model.meshes.size()<<" vertices="<<vertices<<" triangles="<<triangles<<" materials="<<model.materials.size()<<" textures="<<model.textures.size()<<'\n';return 0;}
    darkangel::AssetService assets(argv[2],argv[3]);
    if(mode=="scan"){if(argc!=4)throw std::runtime_error("scan arguments");assets.scan();for(const auto& asset:assets.assets())std::cout<<asset.id.text()<<' '<<asset.path<<" generation="<<asset.generation<<'\n';}
    else if(mode=="adopt-human"){if(argc!=6)throw std::runtime_error("adopt-human arguments");std::cout<<assets.adopt_human(argv[4],argv[5]).text()<<'\n';assets.scan();}
    else if(mode=="adopt"){if(argc!=5)throw std::runtime_error("adopt arguments");std::cout<<assets.adopt(argv[4]).text()<<'\n';assets.scan();}
    else if(mode=="cook"){if(argc!=5)throw std::runtime_error("cook arguments");auto result=assets.cook(argv[4]);std::cout<<result.root.text()<<" generation="<<result.generation<<" changed="<<result.changed<<'\n';}
    else if(mode=="package"){if(argc!=6)throw std::runtime_error("package arguments");assets.package(darkangel::AssetId::parse(argv[4]),argv[5]);std::cout<<"Cooked registry published\n";}
    else throw std::runtime_error("Unknown asset command");return 0;
}catch(const std::exception& e){std::cerr<<"AssetTool: "<<e.what()<<'\n';return 1;}}
