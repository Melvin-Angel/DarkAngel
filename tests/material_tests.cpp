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
    // R5 A3: texture semantics. The same image adopted twice is cooked sRGB as colour and linear as data or a normal map.
    std::filesystem::copy_file(huts/"mudhut-trim.png",source/"textures/normal.png");std::filesystem::copy_file(huts/"mudhut-trim.png",source/"textures/surface.png");
    const auto normal_map=assets.adopt("textures/normal.png"),surface_map=assets.adopt("textures/surface.png");assets.scan();
    assets.cook("textures/trim.png");assets.set_texture_semantic("textures/normal.png",TextureSemantic::Normal);assets.set_texture_semantic("textures/surface.png",TextureSemantic::Data);
    check(metadata("textures/normal.png").at("importer")=="texture-normal-v1"&&metadata("textures/surface.png").at("importer")=="texture-data-v1","Texture semantics are recorded in import metadata");
    auto cooked_texture=[&](AssetId id){assets.package(id,folder/"texture.json");return load_cooked_texture(folder/"texture.json",assets.cas_path(),id);};
    {auto color=cooked_texture(texture),normal=cooked_texture(normal_map),data=cooked_texture(surface_map);check(color.srgb&&!normal.srgb&&!data.srgb,"Colour is sRGB; normal and data textures are linear");
     // A normal map's mips are renormalised: every texel of the smallest mip encodes a unit vector.
     const auto& last=normal.mips.back();for(std::size_t i=0;i+3<last.rgba.size();i+=4){double length=0;for(int c=0;c<3;++c){double n=last.rgba[i+c]/127.5-1;length+=n*n;}check(std::abs(std::sqrt(length)-1)<.03,"Normal map mips are renormalised");}}
    // The lit-surface-v1 family: normal, packed surface and emissive maps, emissive colour and cutout alpha.
    const auto surface_material=AssetId::random();
    auto surface_json=[&](){auto value=material(surface_material,"Surface",{.8,.8,.8,1});value["family"]="lit-surface-v1";value["texture"]=texture.text();value["normal_texture"]=normal_map.text();value["normal_scale"]=1.5;value["surface_texture"]=surface_map.text();value["occlusion_strength"]=.7;
        value["emissive"]={.5,.25,0};value["emissive_texture"]=texture.text();value["alpha_mode"]="cutout";value["alpha_cutoff"]=.4;value["sources"]={{"texture","textures/trim.png"},{"normal_texture","textures/normal.png"},{"surface_texture","textures/surface.png"},{"emissive_texture","textures/trim.png"}};return value;};
    {auto bad=surface_json();bad["asset"]=AssetId::random().text();bad["normal_texture"]=texture.text();bad["sources"]["normal_texture"]="textures/trim.png";rejects([&]{assets.create_native("materials/BadNormal.damaterial",bad.dump());},"A colour texture in a normal-map slot is refused");}
    {auto bad=material(AssetId::random(),"Bad",{1,1,1,1});bad["normal_scale"]=1.0;rejects([&]{assets.create_native("materials/BadField.damaterial",bad.dump());},"A field the family does not have is refused");}
    {auto bad=surface_json();bad["asset"]=AssetId::random().text();bad["alpha_mode"]="additive";rejects([&]{assets.create_native("materials/BadAlpha.damaterial",bad.dump());},"An unknown alpha mode is refused");}
    assets.create_native("materials/Surface.damaterial",surface_json().dump());
    assets.assign_model_material(first,"material/trim",surface_material,"materials/Surface.damaterial");
    {auto a=slot(assets,folder,first_id,first_slot);check(a.has_texture&&a.has_normal&&a.has_surface&&a.has_emissive_texture&&a.alpha_mode==1&&std::abs(a.alpha_cutoff-.4f)<1e-6&&std::abs(a.normal_scale-1.5f)<1e-6&&std::abs(a.occlusion_strength-.7f)<1e-6&&std::abs(a.emissive[1]-.25f)<1e-6,"Assigned surface material values reach the cooked slot");
     assets.package(first_id,folder/"model.json");auto cooked=load_cooked_model(folder/"model.json",assets.cas_path(),first_id);
     auto find=[&](AssetId id)->const CookedTexture*{for(const auto& [texture_id,data]:cooked.textures)if(texture_id==id)return &data;return nullptr;};
     check(find(normal_map)&&!find(normal_map)->srgb&&find(surface_map)&&!find(surface_map)->srgb&&find(texture)&&find(texture)->srgb,"Surface maps join the model closure with their own transfer function");}
    // A standalone instance loads with its whole texture closure (per-placement bindings).
    {assets.package(surface_material,folder/"material.json");auto loaded=load_cooked_material_instance(folder/"material.json",assets.cas_path(),surface_material);check(loaded.value.has_normal&&loaded.value.has_surface&&loaded.textures.size()==3,"Standalone surface material closure");}
    // A texture's meaning cannot change while a material slot needs the current one.
    rejects([&]{assets.set_texture_semantic("textures/normal.png",TextureSemantic::Color);},"Changing a used texture's meaning is refused");
    assets.assign_model_material(first,"material/trim",{});
    // The static glTF profile accepts normal, packed occlusion/roughness/metallic and emissive textures and masked alpha.
    {const auto tri=source/"models/tri";std::filesystem::create_directories(tri);for(const char* name:{"base.png","normal.png","orm.png","glow.png"})std::filesystem::copy_file(huts/"mudhut-trim.png",tri/name);
     std::string bin;auto f32=[&](float v){bin.append(reinterpret_cast<const char*>(&v),4);};auto u16=[&](std::uint16_t v){bin.append(reinterpret_cast<const char*>(&v),2);};
     for(float v:{0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,0.f})f32(v);for(int i=0;i<3;++i){f32(0);f32(0);f32(1);}for(float v:{0.f,0.f,1.f,0.f,0.f,1.f})f32(v);u16(0);u16(1);u16(2);u16(0);write(tri/"tri.bin",bin);
     Json gltf={{"asset",{{"version","2.0"}}},{"buffers",{{{"uri","tri.bin"},{"byteLength",bin.size()}}}},
        {"bufferViews",{{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",36}},{{"buffer",0},{"byteOffset",72},{"byteLength",24}},{{"buffer",0},{"byteOffset",96},{"byteLength",6}}}},
        {"accessors",{{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{1,1,0}}},{{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",2},{"componentType",5126},{"count",3},{"type","VEC2"}},{{"bufferView",3},{"componentType",5123},{"count",3},{"type","SCALAR"}}}},
        {"images",{{{"uri","base.png"}},{{"uri","normal.png"}},{{"uri","orm.png"}},{{"uri","glow.png"}}}},{"textures",{{{"source",0}},{{"source",1}},{{"source",2}},{{"source",3}}}},
        {"materials",{{{"name","Surface"},{"extras",{{"darkangel_key","material/surface"}}},{"pbrMetallicRoughness",{{"baseColorTexture",{{"index",0}}},{"metallicRoughnessTexture",{{"index",2}}}}},{"normalTexture",{{"index",1},{"scale",.8}}},{"occlusionTexture",{{"index",2},{"strength",.5}}},{"emissiveTexture",{{"index",3}}},{"emissiveFactor",{1,.5,0}},{"alphaMode","MASK"},{"alphaCutoff",.3}}}},
        {"meshes",{{{"extras",{{"darkangel_key","mesh/tri"}}},{"primitives",{{{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2}}},{"indices",3},{"material",0}}}}}}},
        {"nodes",{{{"mesh",0}}}},{"scenes",{{{"nodes",{0}}}}},{"scene",0}};
     write(tri/"tri.gltf",gltf.dump());const auto tri_id=assets.adopt("models/tri/tri.gltf");assets.scan();assets.cook("models/tri/tri.gltf");
     const auto tri_meta=metadata("models/tri/tri.gltf");check(tri_meta["subassets"].contains("texture/normal/material/surface")&&tri_meta["subassets"].contains("texture/surface/material/surface")&&tri_meta["subassets"].contains("texture/emissive/material/surface"),"Surface textures get stable subasset keys");
     auto a=slot(assets,folder,tri_id,AssetId::parse(tri_meta["subassets"]["material/surface"].get<std::string>()));
     check(a.has_normal&&a.has_surface&&a.has_emissive_texture&&a.alpha_mode==1&&std::abs(a.alpha_cutoff-.3f)<1e-6&&std::abs(a.occlusion_strength-.5f)<1e-6&&std::abs(a.normal_scale-.8f)<1e-6&&a.emissive[0]==1,"glTF surface features reach the cooked material");
     // Occlusion in a separate image (not packed with metallic-roughness) is refused.
     gltf["images"].push_back({{"uri","glow.png"}});gltf["textures"].push_back({{"source",4}});gltf["materials"][0]["occlusionTexture"]["index"]=4;write(tri/"tri.gltf",gltf.dump());rejects([&]{assets.cook("models/tri/tri.gltf");},"A separate occlusion image is refused");}
    {std::error_code ignored;std::filesystem::remove_all(folder,ignored);} // the catalog may still be open; a temp folder is left behind then
    std::cout<<"R5 materials: imported values, shared assignment, shared edit propagation, texture closure, refusals, stale-key repair, reset, texture semantics, lit-surface family, glTF surface profile passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
