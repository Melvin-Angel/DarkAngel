#include <darkangel/assets.hpp>
#ifdef DAE_SCENE_COLLISION_BAKE
#include <darkangel/model_collision.hpp>
#include <darkangel/editor_document.hpp>
#endif
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){try{
    if(argc<4)throw std::runtime_error("AssetTool scan SOURCE CACHE | adopt-human SOURCE CACHE relative-file canonical-skeleton | adopt/cook SOURCE CACHE relative-file | package SOURCE CACHE UUID registry | inspect registry CAS UUID");
    std::string mode=argv[1];
    if(mode=="bake-scene-collision"){
#ifdef DAE_SCENE_COLLISION_BAKE
        if(argc<9||argc>24)throw std::runtime_error("bake-scene-collision registry CAS scene output.dacollision collision-UUID excluded-skin-UUID mesh-UUID... (maximum 16 bodies)");auto document=darkangel::EditorDocument::open(argv[4]);auto excluded=darkangel::AssetId::parse(argv[7]);darkangel::CollisionDefinition collision;collision.id=darkangel::AssetId::parse(argv[6]);collision.schema=3;unsigned argument=8;std::uint64_t body=1000;
        for(auto [object,asset]:document->plan().models){if(asset==excluded)continue;if(argument>=static_cast<unsigned>(argc))throw std::runtime_error("Missing stable collision mesh UUID");auto model=darkangel::load_cooked_model(argv[2],argv[3],asset);auto pose=document->world().read(document->world().find(object)).transform;auto baked=darkangel::bake_model_collision(model,pose,darkangel::AssetId::parse(argv[argument++]));collision.meshes.push_back({++body,baked.data});std::cout<<"Collision body "<<body<<" object="<<object.text()<<" model="<<asset.text()<<" vertices="<<baked.data->vertices.size()<<" triangles="<<baked.data->triangles.size()<<" duplicates="<<baked.duplicate_faces<<" degenerate="<<baked.degenerate_faces<<'\n';}
        if(argument!=static_cast<unsigned>(argc))throw std::runtime_error("Unused collision mesh UUID");darkangel::publish_collision_source(argv[5],darkangel::encode_collision_source(collision));std::cout<<"Native placed model collision published\n";return 0;
#else
        throw std::runtime_error("Scene collision bake requires native physics authoring tools");
#endif
    }
    if(mode=="inspect"){if(argc!=5)throw std::runtime_error("inspect arguments");auto model=darkangel::load_cooked_model(argv[2],argv[3],darkangel::AssetId::parse(argv[4]));std::size_t vertices{},triangles{};for(const auto& mesh:model.meshes){vertices+=mesh.vertices.size();triangles+=mesh.indices.size()/3;}std::cout<<"Cooked model "<<model.id.text()<<" meshes="<<model.meshes.size()<<" vertices="<<vertices<<" triangles="<<triangles<<" materials="<<model.materials.size()<<" textures="<<model.textures.size()<<'\n';return 0;}
    darkangel::AssetService assets(argv[2],argv[3]);
    if(mode=="scan"){if(argc!=4)throw std::runtime_error("scan arguments");assets.scan();for(const auto& asset:assets.assets())std::cout<<asset.id.text()<<' '<<asset.path<<" generation="<<asset.generation<<'\n';}
    else if(mode=="adopt-human"){if(argc!=6)throw std::runtime_error("adopt-human arguments");std::cout<<assets.adopt_human(argv[4],argv[5]).text()<<'\n';assets.scan();}
    else if(mode=="adopt-human-renderable"){if(argc!=6)throw std::runtime_error("adopt-human-renderable arguments");std::cout<<assets.adopt_human(argv[4],argv[5],true).text()<<'\n';assets.scan();}
    else if(mode=="adopt-clip"){if(argc!=6&&argc!=7)throw std::runtime_error("adopt-clip SOURCE CACHE relative.glb canonical.daskeleton [loop]");if(argc==7&&std::string_view(argv[6])!="loop")throw std::runtime_error("Expected loop option");std::cout<<assets.adopt_clip(argv[4],argv[5],argc==7).text()<<'\n';assets.scan();}
    else if(mode=="adopt"){if(argc!=5)throw std::runtime_error("adopt arguments");std::cout<<assets.adopt(argv[4]).text()<<'\n';assets.scan();}
    else if(mode=="adopt-many"){if(argc<5||argc>132)throw std::runtime_error("adopt-many SOURCE CACHE relative-file... (maximum 128)");for(int index=4;index<argc;++index)std::cout<<assets.adopt(argv[index]).text()<<'\n';assets.scan();}
    else if(mode=="cook"){if(argc!=5)throw std::runtime_error("cook arguments");auto result=assets.cook(argv[4]);std::cout<<result.root.text()<<" generation="<<result.generation<<" changed="<<result.changed<<'\n';}
    else if(mode=="package"){if(argc!=6)throw std::runtime_error("package arguments");assets.package(darkangel::AssetId::parse(argv[4]),argv[5]);std::cout<<"Cooked registry published\n";}
    else if(mode=="package-many"){if(argc<7||argc>69)throw std::runtime_error("package-many SOURCE CACHE primary-UUID registry extra-UUID... (maximum 64 roots)");std::vector<darkangel::AssetId> roots;for(int index=6;index<argc;++index)roots.push_back(darkangel::AssetId::parse(argv[index]));assets.package(darkangel::AssetId::parse(argv[4]),argv[5],roots);std::cout<<"Cooked scene registry published\n";}
    else throw std::runtime_error("Unknown asset command");return 0;
}catch(const std::exception& e){std::cerr<<"AssetTool: "<<e.what()<<'\n';return 1;}}
