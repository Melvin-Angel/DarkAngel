#include <darkangel/assets.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <bit>
using namespace darkangel;
void check(bool test,const char* error){if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,"Expected asset rejection");}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);check(in.good(),"Test input missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,std::string_view bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(out.good(),"Test write failed");}
void fixture(const std::filesystem::path& root){
    std::string bytes;auto f=[&](float value){auto n=std::bit_cast<std::uint32_t>(value);for(unsigned i=0;i<4;++i)bytes+=static_cast<char>(n>>(8*i));};
    for(float x:{-1.f,-1.f,0.f,1.f,-1.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,1.f,0.f,0.f,1.f,0.f,0.f,1.f,0.f,.5f,1.f})f(x);
    bytes.append("\0\0\1\0\2\0",6);write(root/"triangle.bin",bytes);
    write(root/"triangle.gltf",R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"extras":{"darkangel_key":"mesh/prop"},"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0,"mode":4}]}],"materials":[{"extras":{"darkangel_key":"material/plain"},"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"metallicFactor":0,"roughnessFactor":1}}],"buffers":[{"uri":"triangle.bin","byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}]})");
}
int main(int argc,char** argv){try{
    auto parent=std::filesystem::weakly_canonical(DAE_BINARY_DIR);auto workspace=parent/"asset-tests"/AssetId::random().text();auto sources=workspace/"source",cache=workspace/"cache";std::filesystem::create_directories(sources);
    std::string filename="triangle.gltf";if(argc==2){filename="prop.glb";std::filesystem::copy_file(argv[1],sources/filename);}else fixture(sources);
    AssetId root;std::string initial_registry;
    {
        AssetService service(sources,cache);rejects([&]{service.scan();});root=service.adopt(filename);rejects([&]{service.adopt(filename);});
        check(AssetId::parse(root.text())==root,"UUID round trip failed");service.scan();check(service.assets().size()==1 && service.assets()[0].id==root,"Catalog source identity missing");
        auto result=service.cook(filename);check(result.root==root && result.generation==1 && result.changed,"First cook failed");
        service.package(root,workspace/"registry.json");initial_registry=read(workspace/"registry.json");auto model=load_cooked_model(workspace/"registry.json",service.cas_path(),root);check(!model.meshes.empty() && !model.materials.empty(),"Runtime model missing mesh/material");
        if(argc==2)check(model.meshes[0].indices.size()>3 && !model.textures.empty() && model.textures[0].second.mips.size()>1,"Real mesh/texture/mips not cooked");else check(model.meshes[0].vertices.size()==3 && model.meshes[0].indices.size()==3,"Synthetic geometry mismatch");
        auto conversions=service.conversion_count();check(!service.cook(filename).changed && service.conversion_count()==conversions,"Warm reconciliation performed redundant conversion");
        auto second_name=std::string(argc==2?"second.glb":"second.gltf");std::filesystem::copy_file(sources/filename,sources/second_name);auto second=service.adopt(second_name);service.cook(second_name);std::array<AssetId,1> extra{second};service.package(root,workspace/"scene-registry.json",extra);auto scene_registry=read(workspace/"scene-registry.json");check(!load_cooked_model(workspace/"scene-registry.json",service.cas_path(),root).meshes.empty()&&!load_cooked_model(workspace/"scene-registry.json",service.cas_path(),second).meshes.empty(),"Scene package did not preserve both roots");rejects([&]{load_cooked_model(workspace/"registry.json",service.cas_path(),second);});rejects([&]{load_cooked_model(workspace/"scene-registry.json",service.cas_path(),AssetId::random());});std::array<AssetId,1> duplicate_root{root};rejects([&]{service.package(root,workspace/"scene-registry.json",duplicate_root);});std::array<AssetId,1> absent_root{AssetId::random()};rejects([&]{service.package(root,workspace/"scene-registry.json",absent_root);});check(read(workspace/"scene-registry.json")==scene_registry,"Rejected package replaced valid scene registry");std::filesystem::remove(sources/second_name);std::filesystem::remove((sources/second_name).string()+".daimport");service.scan();
        ModelStore store;store.publish(store.prepare(workspace/"registry.json",service.cas_path(),root));auto pinned=store.acquire();store.publish(store.prepare(workspace/"registry.json",service.cas_path(),root));check(store.collect()==0 && store.retired()==1,"Pinned resource generation retired early");pinned.model.reset();check(store.collect()==1 && store.retired()==0,"Unpinned resource generation retained");
        auto source=read(sources/filename);write(sources/filename,"broken glTF");rejects([&]{service.cook(filename);});service.package(root,workspace/"after-failure.json");check(read(workspace/"after-failure.json")==initial_registry,"Failed cook replaced last valid manifest");check(!load_cooked_model(workspace/"after-failure.json",service.cas_path(),root).meshes.empty(),"Failed cook broke last valid runtime data");write(sources/filename,source);
        auto duplicate=sources/(argc==2?"duplicate.glb":"duplicate.gltf");std::filesystem::copy_file(sources/filename,duplicate);std::filesystem::copy_file((sources/filename).string()+".daimport",duplicate.string()+".daimport");rejects([&]{service.scan();});check(service.assets().size()==1,"Failed duplicate scan partially updated catalog");std::filesystem::remove(duplicate);std::filesystem::remove(duplicate.string()+".daimport");
        auto moved=std::string(argc==2?"moved.glb":"moved.gltf");std::filesystem::rename(sources/filename,sources/moved);std::filesystem::rename((sources/filename).string()+".daimport",(sources/moved).string()+".daimport");filename=moved;service.scan();check(service.assets()[0].id==root && service.assets()[0].path==moved,"Move changed logical AssetID");service.cook(filename);service.package(root,workspace/"moved-registry.json");check(read(workspace/"moved-registry.json")==initial_registry,"Source path leaked into runtime artifacts");
        for(const auto& entry:std::filesystem::directory_iterator(service.cas_path()))if(entry.path().extension()==".mesh"){auto bytes=read(entry.path());bytes[0]='X';write(entry.path(),bytes);}
        rejects([&]{load_cooked_model(workspace/"moved-registry.json",service.cas_path(),root);});service.cook(filename);check(!load_cooked_model(workspace/"moved-registry.json",service.cas_path(),root).meshes.empty(),"Corrupt cache was not repaired");
        rejects([&]{service.cook("../escape.glb");});
    }
    // This deletes only a uniquely generated CTest cache directory; source sidecars survive.
    auto resolved=std::filesystem::weakly_canonical(cache);check(resolved.parent_path()==std::filesystem::weakly_canonical(workspace) && resolved.lexically_relative(parent).begin()->string()=="asset-tests","Unsafe test cache cleanup target");std::filesystem::remove_all(resolved);
    AssetService rebuilt(sources,cache);rebuilt.scan();check(rebuilt.assets()[0].id==root,"Catalog deletion lost logical identity");rebuilt.cook(filename);rebuilt.package(root,workspace/"rebuilt-registry.json");check(read(workspace/"rebuilt-registry.json")==initial_registry,"Cold rebuild changed immutable products");
    std::cout<<"Asset catalog/cook/runtime checks passed; root="<<root.text()<<" workspace="<<workspace.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"Asset test: "<<e.what()<<'\n';return 1;}}
