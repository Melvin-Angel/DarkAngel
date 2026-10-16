#include <darkangel/assets.hpp>
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/assembly.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
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
    // A standalone native instance carries its texture closure without requiring a model or artist files at runtime.
    {assets.package(shared,folder/"standalone.json");auto manifest=Json::parse(read(folder/"standalone.json"));check(manifest["assets"].size()==2,"Standalone material package omitted its texture closure");
     std::filesystem::rename(source/"textures/trim.png",source/"textures/trim.png.held");auto loaded=load_cooked_material_instance(folder/"standalone.json",assets.cas_path(),shared);std::filesystem::rename(source/"textures/trim.png.held",source/"textures/trim.png");
     check(loaded.value.id==shared&&loaded.value.has_texture&&loaded.value.texture==texture&&loaded.textures.size()==1&&loaded.textures.front().second.srgb,"Standalone material load depended on artist data or lost color semantics");
     auto incomplete=manifest;auto& records=incomplete["assets"];records.erase(std::remove_if(records.begin(),records.end(),[](const Json& record){return record.at("kind")=="texture";}),records.end());write(folder/"incomplete.json",incomplete.dump());
     rejects([&]{load_cooked_material_instance(folder/"incomplete.json",assets.cas_path(),shared);},"Missing standalone texture must refuse");
     rejects([&]{load_cooked_material_instance(folder/"standalone.json",assets.cas_path(),first_id);},"A model cannot be loaded as a material instance");}
    // Refusals keep the previous state.
    // Prefab publication validates stable model slots and freezes native material/texture dependencies.
    {std::filesystem::create_directories(source/"prefabs");const auto prefab=AssetId::random();const auto key="00000000000000000000000000000001";
     Json definition={{"schema",1},{"kind","prefab"},{"asset",prefab.text()},{"name","Material prefab"},{"entities",{{key,{{"name","Renderable"},{"target",nullptr},{"types",{{"1",{{"version",2},{"fields",{{"1",0.0},{"2",0.0},{"3",0.0},{"4",0.0},{"5",0.0},{"6",0.0},{"7",1.0}}}}}}},{"model",first_id.text()},{"materials",{{first_slot.text(),shared.text()}}}}}}}};
     definition["entities"][key]["types"]["2"]={{"version",1},{"fields",{{"1",100},{"2",100}}}};
     assets.create_native("prefabs/Material.daprefab",definition.dump());assets.package(prefab,folder/"prefab-frozen.json");check(load_cooked_material_instance(folder/"prefab-frozen.json",assets.cas_path(),shared).value.has_texture,"Prefab package omitted material texture closure");
     {const std::array<AssetId,1> additional{shared};assets.package(first_id,folder/"bound-model.json",additional);SpawnPlan bindings;const StableId object{0,1};bindings.models[object]=first_id;bindings.materials[object][first_slot]=shared;validate_cooked_material_bindings(folder/"bound-model.json",assets.cas_path(),bindings);
      auto bad_bindings=bindings;bad_bindings.materials[object]={{AssetId::random(),shared}};rejects([&]{validate_cooked_material_bindings(folder/"bound-model.json",assets.cas_path(),bad_bindings);},"Frozen binding preflight accepted a removed slot");
      bad_bindings=bindings;bad_bindings.materials[object][first_slot]=AssetId::random();rejects([&]{validate_cooked_material_bindings(folder/"bound-model.json",assets.cas_path(),bad_bindings);},"Frozen binding preflight accepted an unpackaged material");}
     auto bad=definition;bad["asset"]=AssetId::random().text();bad["entities"][key]["materials"]={{AssetId::random().text(),shared.text()}};rejects([&]{assets.create_native("prefabs/BadSlot.daprefab",bad.dump());},"Prefab publication accepted a nonexistent material slot");
     bad=definition;bad["asset"]=AssetId::random().text();bad["entities"][key]["materials"][first_slot.text()]=AssetId::random().text();rejects([&]{assets.create_native("prefabs/BadMaterial.daprefab",bad.dump());},"Prefab publication accepted a missing material instance");
     const auto original=read(source/"materials/Shared.damaterial");auto edited=Json::parse(original);edited["color"]={0.0,0.0,1.0,1.0};NativeSourceEdit change{"materials/Shared.damaterial",sha256(original),edited.dump()};
     const auto results=assets.commit_native(assets.prepare_native({&change,1}));std::set<AssetId> updated;for(const auto& result:results)updated.insert(result.root);check(updated.contains(shared)&&updated.contains(first_id)&&updated.contains(second_id)&&updated.contains(prefab),"Coordinated material Save omitted model/prefab consumers");
     check(slot(assets,folder,first_id,first_slot).color[2]==1&&slot(assets,folder,second_id,second_slot).color[2]==1,"Coordinated Save did not publish both model heads");
     assets.package(prefab,folder/"prefab-current.json");check(load_cooked_material_instance(folder/"prefab-current.json",assets.cas_path(),shared).value.color[2]==1,"Coordinated Save did not publish prefab closure");
     check(load_cooked_material_instance(folder/"prefab-frozen.json",assets.cas_path(),shared).value.color[1]==1,"Material Save changed a previously frozen prefab package");
     change={"materials/Shared.damaterial",sha256(read(source/"materials/Shared.damaterial")),original};assets.commit_native(assets.prepare_native({&change,1}));}
    {const auto sidecar=read(source/(first+".daimport"));rejects([&]{assets.assign_model_material(first,"material/missing",shared,"materials/Shared.damaterial");},"Unknown slot refused");check(read(source/(first+".daimport"))==sidecar,"Refused assignment changed metadata");}
    {auto path=source/"materials/Shared.damaterial";const auto bytes=read(path);auto bad=Json::parse(bytes);bad["color"][0]=2.0;rejects([&]{assets.edit_native("materials/Shared.damaterial",sha256(bytes),bad.dump(2));},"Out-of-range material refused");check(read(path)==bytes,"Refused material edit changed the source");}
    {auto bad=material(AssetId::random(),"Bad",{1,1,1,1});bad["family"]="toon-v9";rejects([&]{assets.create_native("materials/Bad.damaterial",bad.dump());},"Unknown material family refused");}
    // A stale slot key is a repair, not a guess: the model refuses to cook until it is fixed.
    {const auto path=source/(second+".daimport");const auto original=read(path);auto stale=Json::parse(original);stale["materials"]["material/roof"]=stale["materials"]["material/trim"];write(path,stale.dump(2));
     rejects([&]{assets.cook(second);},"A stale material slot key must be refused");write(path,original);assets.cook(second);}
    // Source array reordering preserves keyed assignments. Added/retired keys require explicit adoption and decisions.
    {const auto path=source/second,sidecar=source/(second+".daimport");const auto original=read(path);auto gltf=Json::parse(original);auto extra=gltf["materials"][0];extra["extras"]["darkangel_key"]="material/extra";gltf["materials"].push_back(extra);write(path,gltf.dump());
     auto changes=assets.model_reimport_changes(second);check(changes.removed.empty()&&changes.added.size()==2,"New material and texture keys were not enumerated");assets.repair_model_reimport(second,sha256(read(sidecar)),{});const auto ids=metadata(second).at("subassets");
     std::swap(gltf["materials"][0],gltf["materials"][1]);for(auto& mesh:gltf["meshes"])for(auto& primitive:mesh["primitives"]){const auto index=primitive.at("material").get<unsigned>();primitive["material"]=index==0?1:0;}write(path,gltf.dump());assets.cook(second);
     check(metadata(second).at("subassets")==ids&&slot(assets,folder,second_id,second_slot).color[1]==1,"Array reorder changed stable identities or remapped an assignment by index");
     write(path,original);changes=assets.model_reimport_changes(second);check(changes.added.empty()&&changes.removed.size()==2,"Retired keys were not enumerated");rejects([&]{assets.repair_model_reimport(second,sha256(read(sidecar)),{});},"Retirement without explicit decisions accepted");
     std::map<std::string,std::string> retire;for(const auto& key:changes.removed)retire[key]="";assets.repair_model_reimport(second,sha256(read(sidecar)),retire);check(!metadata(second).at("subassets").contains("material/extra")&&AssetId::parse(metadata(second)["subassets"]["material/trim"].get<std::string>())==second_slot,"Explicit retirement changed a surviving slot identity");}
    // Reset to imported removes the assignment and restores the imported values.
    // A material creation and metadata assignment are one prepared publication: validation/stale writers touch neither head.
    {const auto sidecar=source/(first+".daimport");const auto original=read(sidecar);const auto id=AssetId::random();
     NativeSourceEdit create{"materials/Atomic.damaterial",{},material(id,"Atomic",{.2,.3,.4,1}).dump(),true};auto assignment=assets.model_material_edit(first,"material/trim",id,create.path);
     std::array<NativeSourceEdit,2> edits{create,assignment};auto prepared=assets.prepare_native(edits);
     check(!std::filesystem::exists(source/create.path)&&read(sidecar)==original,"Preparation published a source or metadata head");
     auto external=Json::parse(original);external["tags"]=Json::array({"external"});write(sidecar,external.dump());
     rejects([&]{assets.commit_native(prepared);},"A stale metadata writer must refuse the whole publication");
     check(!std::filesystem::exists(source/create.path)&&read(sidecar)==external.dump(),"Stale publication lost external metadata or published the material");write(sidecar,original);
     auto bad=create;auto invalid=Json::parse(bad.draft);invalid["color"][0]=2;bad.draft=invalid.dump();edits={bad,assignment};
     rejects([&]{assets.prepare_native(edits);},"Invalid material must refuse the combined candidate");check(!std::filesystem::exists(source/create.path)&&read(sidecar)==original,"Failed candidate changed source heads");
     edits={create,assignment};assets.commit_native(assets.prepare_native(edits));check(std::filesystem::exists(source/create.path)&&metadata(first)["materials"]["material/trim"]["asset"]==id.text(),"Atomic material and assignment publication");
     check(slot(assets,folder,first_id,first_slot).color[0]==.2f,"Atomic assigned values did not reach the model");}
    // Reimport repair maps explicit stable keys, never display name/order. The source material and derived texture identities survive.
    {const auto model_path=source/first,sidecar=source/(first+".daimport");const auto original_model=read(model_path);const auto original_metadata=read(sidecar);auto gltf=Json::parse(original_model);
     const auto texture_id=metadata(first)["subassets"]["texture/base-color/material/trim"];gltf["materials"][0]["extras"]["darkangel_key"]="material/renamed";write(model_path,gltf.dump());
     rejects([&]{assets.cook(first);},"Removed keys must refuse ordinary cook");auto changes=assets.model_reimport_changes(first);check(changes.removed.size()==2&&changes.added.size()==2,"Reimport key changes were not enumerated");
     const auto digest=sha256(read(sidecar));rejects([&]{assets.repair_model_reimport(first,digest,{});},"Every removed key needs a decision");
     rejects([&]{assets.repair_model_reimport(first,digest,{{"material/trim",""},{"texture/base-color/material/trim",""}});},"Retiring a slot still referenced by a prefab must refuse publication");check(read(sidecar)==original_metadata,"Refused dependent retirement changed metadata");
     const std::map<std::string,std::string> mapping{{"material/trim","material/renamed"},{"texture/base-color/material/trim","texture/base-color/material/renamed"}};
     rejects([&]{assets.repair_model_reimport(first,sha256("stale"),mapping);},"Stale metadata repair must refuse");check(read(sidecar)==original_metadata,"Refused repair changed metadata");
     assets.repair_model_reimport(first,digest,mapping);auto repaired=metadata(first);check(repaired["subassets"]["material/renamed"]==first_slot.text()&&repaired["subassets"]["texture/base-color/material/renamed"]==texture_id,"Explicit remap lost subasset identities");
     check(repaired["materials"].contains("material/renamed")&&!repaired["materials"].contains("material/trim"),"Explicit remap lost or duplicated an assignment");
     check(slot(assets,folder,first_id,first_slot).color[0]==.2f,"Reimport changed assigned material values");
     write(model_path,original_model);auto reverse=assets.model_reimport_changes(first);check(reverse.removed.size()==2,"Reverse reimport changes");assets.repair_model_reimport(first,sha256(read(sidecar)),{{"material/renamed","material/trim"},{"texture/base-color/material/renamed","texture/base-color/material/trim"}});}
    assets.assign_model_material(first,"material/trim",{});check(!metadata(first).contains("materials"),"Reset removed the assignment");
    {auto a=slot(assets,folder,first_id,first_slot);check(a.color==before.color&&a.roughness==before.roughness&&a.has_texture==before.has_texture,"Reset to imported values");}
    {std::error_code ignored;std::filesystem::remove_all(folder,ignored);} // the catalog may still be open; a temp folder is left behind then
    std::cout<<"R5 materials: imported values, shared assignment, shared edit propagation, texture closure, refusals, stale-key repair and reset passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
