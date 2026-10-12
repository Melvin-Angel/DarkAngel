#include "assets_internal.hpp"
#include <darkangel/primitive_assets.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel {
namespace {
using namespace assets_detail;
// The products of one kind in a registry, each checked against its digest and decoded.
template<class Definition,class Decode>std::vector<Definition> products(const std::filesystem::path& registry,const std::filesystem::path& cas,const char* kind,const char* extension,Decode decode){
    const auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Primitive registry schema/limit");std::set<AssetId> unique;std::vector<Definition> found;
    for(const auto& record:manifest.at("assets")){const auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate primitive registry UUID");if(record.at("kind")!=kind)continue;require(record.at("extension")==extension,"Primitive product type mismatch");
        const auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Primitive product digest");const auto bytes=read(cas/(hash+"."+extension),4096);require(sha256(bytes)==hash,"Primitive product hash mismatch");
        auto definition=decode(bytes);require(definition.id==asset,"Cooked primitive identity mismatch");found.push_back(std::move(definition));}
    return found;
}
}
namespace assets_detail {
// A weapon trail or a decal is a leaf source: validated fields, stored as they are.
Import import_primitive(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,4096);const bool trail=source.extension()==".datrail";const AssetId id=trail?decode_trail(bytes).id:decode_decal(bytes).id;
    Import result;result.identities.push_back(id);result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    if(!inspect){require(ids.size()==1&&ids.at("$source")==id,"Primitive source UUID mismatch");result.products.push_back({id,trail?"trail":"decal",trail?"trail.json":"decal.json",json(bytes,4096).dump(),{}});}return result;
}
}
std::vector<TrailDefinition> load_cooked_trails(const std::filesystem::path& registry,const std::filesystem::path& cas){return products<TrailDefinition>(registry,cas,"trail","trail.json",[](const std::string& bytes){return decode_trail(bytes);});}
std::vector<DecalDefinition> load_cooked_decals(const std::filesystem::path& registry,const std::filesystem::path& cas){return products<DecalDefinition>(registry,cas,"decal","decal.json",[](const std::string& bytes){return decode_decal(bytes);});}
}
