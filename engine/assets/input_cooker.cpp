#include "assets_internal.hpp"
#include <darkangel/input.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel::assets_detail {
Import import_input(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){auto bytes=read(source,65536);auto profile=decode_input_source(bytes);Import result;result.identities.push_back(profile.id);result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);if(!inspect){require(ids.size()==1&&ids.at("$source")==profile.id,"Input source UUID mismatch");result.products.push_back({profile.id,"input","input.json",json(bytes).dump(),{}});}return result;}
}
namespace darkangel {
InputProfile load_cooked_input(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){using namespace assets_detail;auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Input registry schema/limit");std::set<AssetId> unique;const Json* selected{};for(const auto& record:manifest.at("assets")){auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate input registry UUID");if(asset==id)selected=&record;}require(selected&&selected->at("kind")=="input"&&selected->at("extension")=="input.json","Input product type mismatch");auto hash=selected->at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Input product digest");auto bytes=read(cas/(hash+".input.json"),65536);require(sha256(bytes)==hash,"Input product hash mismatch");auto profile=decode_input_source(bytes);require(profile.id==id,"Cooked input identity mismatch");return profile;}
}
