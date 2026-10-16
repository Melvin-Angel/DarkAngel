#include "assets_internal.hpp"
#include <darkangel/assembly.hpp>
#include <darkangel/prefab_assets.hpp>
#include <darkangel/hash.hpp>
namespace darkangel {
using namespace assets_detail;
namespace {
// A placement identity no scene uses: validating a prefab resolves it as the only source of a throwaway scene.
const StableId validation_placement=StableId::parse("7f0000000000000000000000000000a4");
}
PrefabDefinition decode_prefab(std::string_view text){
    require(text.size()<=1024*1024,"Prefab source size bound");auto value=json(text,1024*1024);
    require(value.is_object()&&value.at("schema")==1&&value.at("kind")=="prefab","Prefab schema/type");
    for(const auto& [key,unused]:value.items())require(key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="entities"||key=="exports","Prefab fields: schema, kind, asset, name, entities and exports");
    PrefabDefinition result;result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"Prefab identity");
    require(value.at("name").is_string(),"Prefab name type");result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"Prefab name bounds");
    const auto& entities=value.at("entities");require(entities.is_object()&&!entities.empty()&&entities.size()<=prefab_member_limit,"A prefab holds one to 64 objects");
    for(const auto& [local,entity]:entities.items()){result.members.push_back(local);
        for(const char* role:{"spawn","encounter","volume","spline","spline_point","entry","transition","audio","collision"})require(!entity.contains(role)||entity[role].is_null(),"Prefabs hold models and world objects (altar, door or platform, pickup, interactable, trigger); scene roles stay in the scene");}
    // The prefab must resolve on its own exactly as a mounted assembly would: every component decoder and reference check.
    resolve_assembly(result.id,validation_placement,{{result.id,value.dump()}});
    return result;
}
namespace assets_detail {
Import import_prefab(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,1024*1024);auto prefab=decode_prefab(bytes);if(!inspect)require(ids.size()==1&&ids.at("$source")==prefab.id,"Prefab source identity");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(prefab.id);result.product_count=1;
    if(!inspect)result.products.push_back({prefab.id,"prefab","prefab.json",json(bytes,1024*1024).dump(),{}});return result;
}
}
}
