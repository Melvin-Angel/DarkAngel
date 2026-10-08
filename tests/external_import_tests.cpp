#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#ifdef DAE_ANIMATION
#include <darkangel/animation_assets.hpp>
#endif
#include <nlohmann/json.hpp>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F call){bool failed=false;try{call();}catch(const std::exception&){failed=true;}check(failed,"Invalid import was accepted");}
void write(const std::filesystem::path& path,std::string_view bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(out),"Fixture write failed");}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void model(const std::filesystem::path& root){
    std::string bytes;for(float value:{-1.f,-1.f,0.f,1.f,-1.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f,0.f,1.f,0.f,0.f,1.f}){auto n=std::bit_cast<std::uint32_t>(value);for(unsigned i=0;i<4;++i)bytes+=static_cast<char>(n>>(8*i));}bytes.append("\0\0\1\0\2\0",6);write(root/"geometry/data.bin",bytes);
    write(root/"triangle.gltf",R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"metallicFactor":0,"roughnessFactor":1}}],"buffers":[{"uri":"geometry/data.bin","byteLength":78}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,-1,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})");
}
}
int main(){try{
    auto repository=std::filesystem::path(DAE_SOURCE_DIR);auto workspace=std::filesystem::path(DAE_BINARY_DIR)/("external-import-"+AssetId::random().text());auto external=workspace/"external",project=workspace/"project";std::filesystem::create_directories(project);model(external);
    const auto original=file_sha256(external/"triangle.gltf");const auto buffer=file_sha256(external/"geometry/data.bin");AssetId imported;std::string imported_path;PreparedAssetImport abandoned;
    {
        AssetService service(project,workspace/"cache");service.scan();auto revision=service.assets().size();
        {auto cancelled=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});check(service.assets().size()==revision&&!std::filesystem::exists(project/cancelled.destination()),"Validation/cancel published source or catalog");}
        auto first=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});check(first.destination().starts_with("models/")&&first.files().size()==3,"Model routing and buffer/metadata closure");
        auto second=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});check(second.destination()!=first.destination(),"Same-name imports overwrite");
        auto result=service.commit_import(first);imported=result.root;imported_path=first.destination();check(result.generation==1&&service.assets().size()==1,"Import catalog publication");rejects([&]{service.commit_import(first);});
        auto other=service.commit_import(second);check(other.root!=imported&&service.assets().size()==2,"Same-name imports lose identity");
        check(file_sha256(external/"triangle.gltf")==original&&file_sha256(external/"geometry/data.bin")==buffer,"External original mutated");
        service.package(imported,workspace/"model.json");auto loaded_model=load_cooked_model(workspace/"model.json",service.cas_path(),imported);check(loaded_model.meshes.size()==1&&loaded_model.meshes[0].vertices.size()==3,"Imported untagged model cannot load from cooked assets");
        check(!service.cook(imported_path).changed,"Warm import cook reconverted");
        auto inventory=service.assets().size();rejects([&]{service.prepare_import({external/"triangle.gltf",AssetImportType::Vfx});});rejects([&]{service.prepare_import({project/imported_path,AssetImportType::Model});});check(service.assets().size()==inventory,"Rejected preparation changed catalog");
        auto stale=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});write(external/"geometry/data.bin","changed");rejects([&]{service.commit_import(stale);});check(!std::filesystem::exists(project/stale.destination())&&service.assets().size()==inventory,"Stale dependency import partially published");model(external);
        auto unsafe=Json::parse(read(external/"triangle.gltf"));unsafe["buffers"][0]["uri"]="../escape.bin";write(external/"unsafe.gltf",unsafe.dump());rejects([&]{service.prepare_import({external/"unsafe.gltf",AssetImportType::Model});});
        write(external/"broken.png","broken image");rejects([&]{service.prepare_import({external/"broken.png",AssetImportType::Texture});});check(service.assets().size()==inventory,"Failed texture published");
        auto collision=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});std::filesystem::create_directories((project/collision.destination()).parent_path());write((project/collision.destination()).parent_path()/"unrelated.txt","keep");rejects([&]{service.commit_import(collision);});check(read((project/collision.destination()).parent_path()/"unrelated.txt")=="keep","Destination collision overwrote unrelated files");
        // Failure after source publication must restore the original catalog and all previous products.
        auto rollback=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});auto sidecar=Json::parse(read(std::filesystem::path((project/imported_path).string()+".daimport")));auto occupied=rollback.id().text();sidecar["id"]=occupied;auto old=read(std::filesystem::path((project/imported_path).string()+".daimport"));write(std::filesystem::path((project/imported_path).string()+".daimport"),sidecar.dump());
        rejects([&]{service.commit_import(rollback);});check(!std::filesystem::exists(project/rollback.destination())&&service.assets().size()==inventory,"Failed commit did not roll back imported source inventory");write(std::filesystem::path((project/imported_path).string()+".daimport"),old);service.scan();
        std::filesystem::path texture;for(const auto& entry:std::filesystem::recursive_directory_iterator(repository/"content/royal_district"))if(entry.path().extension()==".png"){texture=entry.path();break;}check(!texture.empty(),"Royal color texture fixture missing");std::filesystem::copy_file(texture,external/"color.png");auto texture_hash=file_sha256(external/"color.png");auto color=service.prepare_import({external/"color.png",AssetImportType::Texture});check(color.destination().starts_with("textures/"),"Texture routing");service.commit_import(color);service.package(color.id(),workspace/"texture.json");auto cooked=load_cooked_texture(workspace/"texture.json",service.cas_path(),color.id());check(cooked.srgb&&!cooked.mips.empty()&&file_sha256(external/"color.png")==texture_hash,"Color texture cook/load/original preservation");
#ifdef DAE_ANIMATION
        std::filesystem::create_directories(project/"animation");std::filesystem::copy_file(repository/"content/animation/canonical_human.daskeleton",project/"animation/canonical_human.daskeleton");
        std::filesystem::copy_file(repository/"content/royal_district/character/canonical-human.glb",external/"human.glb");auto human_hash=file_sha256(external/"human.glb");auto human=service.prepare_import({external/"human.glb",AssetImportType::SkinnedMesh,"animation/canonical_human.daskeleton"});check(human.destination().starts_with("characters/"),"Human routing");service.commit_import(human);service.package(human.id(),workspace/"human.json");auto skin=load_cooked_skinned_model(workspace/"human.json",service.cas_path(),human.id());check(skin.meshes.size()==14&&skin.rig.definition.joints.size()==81&&file_sha256(external/"human.glb")==human_hash,"Canonical human import/cooked skin");
        std::filesystem::copy_file(repository/"content/royal_district/clips/attack.glb",external/"attack.glb");auto clip=service.prepare_import({external/"attack.glb",AssetImportType::Animation,"animation/canonical_human.daskeleton"});check(clip.destination().starts_with("animation/clips/"),"Clip routing");service.commit_import(clip);service.package(clip.id(),workspace/"clip.json");auto animation=load_cooked_clip(workspace/"clip.json",service.cas_path(),clip.id());check(animation.definition.ticks>0,"Imported normalized clip cannot load");
        auto stale_rig=service.prepare_import({external/"attack.glb",AssetImportType::Animation,"animation/canonical_human.daskeleton"});auto rig=read(project/"animation/canonical_human.daskeleton");write(project/"animation/canonical_human.daskeleton",rig+" ");rejects([&]{service.commit_import(stale_rig);});write(project/"animation/canonical_human.daskeleton",rig);
#else
        rejects([&]{service.prepare_import({external/"triangle.gltf",AssetImportType::SkinnedMesh});});
#endif
        abandoned=service.prepare_import({external/"triangle.gltf",AssetImportType::Model});
    }
    AssetService rebuilt(project,workspace/"rebuilt-cache");rejects([&]{rebuilt.commit_import(abandoned);});rebuilt.scan();rebuilt.cook(imported_path);rebuilt.package(imported,workspace/"rebuilt.json");check(!load_cooked_model(workspace/"rebuilt.json",rebuilt.cas_path(),imported).meshes.empty(),"Cold catalog rebuild lost imported identity");
    std::cout<<"Typed external import: model/dependency routing, cancellation, stale inputs, rollback, textures, canonical profiles and cold rebuild passed; workspace="<<workspace.string()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
