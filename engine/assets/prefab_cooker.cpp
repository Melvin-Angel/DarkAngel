#include "assets_internal.hpp"
#include <darkangel/assembly.hpp>
#include <darkangel/prefab_assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
namespace darkangel {
using namespace assets_detail;
namespace {
// A placement identity no scene uses: validating a prefab resolves it as the only source of a throwaway scene.
const StableId validation_placement=StableId::parse("7f0000000000000000000000000000a4");
}
std::vector<AssetId> prefab_references(std::string_view text){
    auto value=json(text,1024*1024);std::vector<AssetId> result;
    if(value.contains("base")&&!value["base"].is_null())result.push_back(AssetId::parse(value["base"].get<std::string>()));
    if(value.contains("mounts")&&value["mounts"].is_object())for(const auto& [key,mount]:value["mounts"].items())result.push_back(AssetId::parse(mount.at("asset").get<std::string>()));
    return result;
}
PrefabDefinition decode_prefab(std::string_view text,const std::map<AssetId,std::string>& linked){
    require(text.size()<=1024*1024,"Prefab source size bound");auto value=json(text,1024*1024);
    require(value.is_object()&&value.at("schema")==1&&value.at("kind")=="prefab","Prefab schema/type");
    for(const auto& [key,unused]:value.items())require(key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="entities"||key=="exports"||key=="mounts"||key=="base"||key=="base_fingerprint"||key=="patches","Prefab fields: schema, kind, asset, name, entities, exports, mounts, base, base_fingerprint and patches");
    PrefabDefinition result;result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"Prefab identity");
    require(value.at("name").is_string(),"Prefab name type");result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"Prefab name bounds");
    const auto& entities=value.at("entities");const bool variant=value.contains("base")&&!value["base"].is_null();
    const auto mounts=value.value("mounts",nlohmann::json::object());require(mounts.is_object()&&mounts.size()<=16,"A prefab mounts up to 16 nested prefabs");
    require(entities.is_object()&&entities.size()<=prefab_member_limit,"A prefab holds up to 64 objects");
    // A variant's members are its base's, changed only by typed patches; any other prefab holds at least one object or nested prefab.
    if(variant)require(entities.empty()&&mounts.empty()&&value.contains("base_fingerprint"),"A prefab variant holds patches against its base and the base's fingerprint, no objects of its own");
    else require(!entities.empty()||!mounts.empty(),"A prefab holds one to 64 objects or nested prefabs");
    for(const auto& [local,entity]:entities.items())
        for(const char* role:{"spawn","encounter","volume","spline","spline_point","entry","transition","audio","collision"})require(!entity.contains(role)||entity[role].is_null(),"Prefabs hold models and world objects (altar, door or platform, pickup, interactable, trigger); scene roles stay in the scene");
    // The prefab must resolve exactly as a mounted assembly would: every component decoder and reference check, with the
    // prefabs it mounts or derives from.
    std::map<AssetId,std::string> sources=linked;sources[result.id]=value.dump();
    const auto plan=resolve_assembly(result.id,validation_placement,sources);
    require(plan.spawns.empty()&&plan.encounters.empty()&&plan.volumes.empty()&&plan.splines.empty()&&plan.spline_points.empty()&&plan.entries.empty()&&plan.transitions.empty()&&plan.audio_profiles.empty()&&plan.collision_profiles.empty(),"Prefabs hold models and world objects; scene roles stay in the scene");
    require(!plan.origins.empty()&&plan.origins.size()<=256,"A prefab resolves to one to 256 objects");
    const auto prefix=validation_placement.text()+"/";
    for(const auto& origin:plan.origins){auto rest=origin.local_path.substr(prefix.size());if(rest.find('/')==rest.npos)result.members.push_back(rest);}
    std::sort(result.members.begin(),result.members.end());
    return result;
}
namespace assets_detail {
Import import_prefab(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,1024*1024);Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    // Nested and base prefabs are read from the same source mount and are inputs of this cook, so changing one recooks it.
    std::map<AssetId,std::string> linked;auto pending=prefab_references(bytes);
    if(!pending.empty()){std::map<AssetId,std::filesystem::path> index;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file()&&entry.path().extension()==".daprefab"&&entry.path()!=source){try{auto value=json(read(entry.path(),1024*1024),1024*1024);index.emplace(AssetId::parse(value.at("asset").get<std::string>()),entry.path());}catch(const std::exception&){}}
        while(!pending.empty()){auto next=pending.back();pending.pop_back();if(linked.contains(next))continue;require(index.contains(next),"A prefab this prefab mounts or derives from is missing from the project");require(linked.size()<64,"Nested prefab closure limit");
            auto text=read(index.at(next),1024*1024);result.inputs[index.at(next).lexically_relative(root).generic_string()]=sha256(text);for(auto inner:prefab_references(text))pending.push_back(inner);linked.emplace(next,std::move(text));}}
    auto prefab=decode_prefab(bytes,linked);if(!inspect)require(ids.size()==1&&ids.at("$source")==prefab.id,"Prefab source identity");result.identities.push_back(prefab.id);result.product_count=1;
    if(!inspect)result.products.push_back({prefab.id,"prefab","prefab.json",json(bytes,1024*1024).dump(),{}});return result;
}
}
}
