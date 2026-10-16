#include <darkangel/assets.hpp>
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace darkangel;using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void write(const std::filesystem::path& path,const std::string& text){std::ofstream(path,std::ios::binary)<<text;}
Json material(AssetId id,const char* name,std::array<double,4> color){return {{"schema",1},{"kind","material"},{"asset",id.text()},{"name",name},{"family","lit-color-v1"},{"color",color},{"roughness",.4},{"metallic",.1},{"double_sided",false},{"texture",nullptr}};}
CookedMaterial slot(AssetService& assets,const std::filesystem::path& folder,AssetId model,AssetId material_id){
    assets.package(model,folder/"model.json");auto cooked=load_cooked_model(folder/"model.json",assets.cas_path(),model);
    for(const auto& m:cooked.materials)if(m.id==material_id)return m;throw std::runtime_error("Material slot missing from cooked model");}
}
int main(){try{
    const auto folder=std::filesystem::temp_directory_path()/("material-tests-"+AssetId::random().text());const auto source=folder/"sources";
    const auto huts=std::filesystem::path(DAE_SOURCE_DIR)/"content/royal_district/static/mud_huts";std::filesystem::create_directories(source/"models");std::filesystem::create_directories(source/"textures");std::filesystem::create_directories(source/"materials");
    for(const char* name:{"SM_RC_MudHut_Round_1F","SM_RC_MudHut_Round_2F_Setback"})for(const char* extension:{".gltf",".bin",".gltf.daimport"})std::filesystem::copy_file(huts/(std::string(name)+extension),source/"models"/(std::string(name)+extension));
    std::filesystem::copy_file(huts/"mudhut-trim.png",source/"models/mudhut-trim.png");std::filesystem::copy_file(huts/"mudhut-trim.png",source/"textures/trim.png");
    AssetService assets(source,folder/"cache");const auto texture=assets.adopt("textures/trim.png");assets.scan();
    const std::string first="models/SM_RC_MudHut_Round_1F.gltf",second="models/SM_RC_MudHut_Round_2F_Setback.gltf";
    auto metadata=[&](const std::string& model){return Json::parse(read(source/(model+".daimport")));};
    const auto first_id=AssetId::parse(metadata(first).at("id").get<std::string>()),second_id=AssetId::parse(metadata(second).at("id").get<std::string>());
    const auto first_slot=AssetId::parse(metadata(first)["subassets"]["material/trim"].get<std::string>()),second_slot=AssetId::parse(metadata(second)["subassets"]["material/trim"].get<std::string>());
    assets.cook(first);assets.cook(second);
    // Imported values are readable, and an unassigned slot cooks them unchanged.
    const auto imported=Json::parse(assets.material_values(first,"material/trim"));const auto before=slot(assets,folder,first_id,first_slot);
    check(imported.at("roughness").get<float>()==before.roughness&&before.has_texture,"Imported slot values");
    // A shared material assigned to both models' slots reaches both.
    const auto shared=AssetId::random();assets.create_native("materials/Shared.damaterial",material(shared,"Shared",{1,0,0,1}).dump());
    assets.assign_model_material(first,"material/trim",shared,"materials/Shared.damaterial");assets.assign_model_material(second,"material/trim",shared,"materials/Shared.damaterial");
    {auto a=slot(assets,folder,first_id,first_slot),b=slot(assets,folder,second_id,second_slot);check(a.color[0]==1&&a.color[1]==0&&b.color[0]==1&&!a.has_texture&&std::abs(a.roughness-.4f)<1e-6,"Assigned material values on both models");}
    // Editing the shared material changes both consumers on their next cook.
    {auto path=source/"materials/Shared.damaterial";auto edited=material(shared,"Shared",{0,1,0,1});assets.edit_native("materials/Shared.damaterial",sha256(read(path)),edited.dump(2));
     assets.cook(first);assets.cook(second);auto a=slot(assets,folder,first_id,first_slot),b=slot(assets,folder,second_id,second_slot);check(a.color[1]==1&&b.color[1]==1,"Shared material edit reaches both models");}
    // A project texture on the material is carried into the model's closure.
    {auto path=source/"materials/Shared.damaterial";auto textured=material(shared,"Shared",{0,1,0,1});textured["texture"]=texture.text();textured["sources"]={{"texture","textures/trim.png"}};
     assets.edit_native("materials/Shared.damaterial",sha256(read(path)),textured.dump(2));assets.cook(first);assets.package(first_id,folder/"model.json");auto cooked=load_cooked_model(folder/"model.json",assets.cas_path(),first_id);
     bool found=false;for(const auto& m:cooked.materials)if(m.id==first_slot)found=m.has_texture&&m.texture==texture;bool loaded=false;for(const auto& [id,unused]:cooked.textures)loaded|=id==texture;
     check(found&&loaded,"Material texture reaches the cooked model");}
    // Refusals keep the previous state.
    {const auto sidecar=read(source/(first+".daimport"));rejects([&]{assets.assign_model_material(first,"material/missing",shared,"materials/Shared.damaterial");},"Unknown slot refused");check(read(source/(first+".daimport"))==sidecar,"Refused assignment changed metadata");}
    {auto path=source/"materials/Shared.damaterial";const auto bytes=read(path);auto bad=Json::parse(bytes);bad["color"][0]=2.0;rejects([&]{assets.edit_native("materials/Shared.damaterial",sha256(bytes),bad.dump(2));},"Out-of-range material refused");check(read(path)==bytes,"Refused material edit changed the source");}
    {auto bad=material(AssetId::random(),"Bad",{1,1,1,1});bad["family"]="toon-v9";rejects([&]{assets.create_native("materials/Bad.damaterial",bad.dump());},"Unknown material family refused");}
    // A stale slot key is a repair, not a guess: the model refuses to cook until it is fixed.
    {const auto path=source/(second+".daimport");const auto original=read(path);auto stale=Json::parse(original);stale["materials"]["material/roof"]=stale["materials"]["material/trim"];write(path,stale.dump(2));
     rejects([&]{assets.cook(second);},"A stale material slot key must be refused");write(path,original);assets.cook(second);}
    // Reset to imported removes the assignment and restores the imported values.
    assets.assign_model_material(first,"material/trim",{});check(!metadata(first).contains("materials"),"Reset removed the assignment");
    {auto a=slot(assets,folder,first_id,first_slot);check(a.color==before.color&&a.roughness==before.roughness&&a.has_texture==before.has_texture,"Reset to imported values");}
    {std::error_code ignored;std::filesystem::remove_all(folder,ignored);} // the catalog may still be open; a temp folder is left behind then
    std::cout<<"R5 materials: imported values, shared assignment, shared edit propagation, texture closure, refusals, stale-key repair and reset passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
