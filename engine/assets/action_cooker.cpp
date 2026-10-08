#include "assets_internal.hpp"
#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
namespace darkangel::assets_detail {
Import import_action(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){auto bytes=read(source,65536);auto definition=decode_action_source(bytes);Import out;out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);out.product_count=1;if(inspect)return out;require(ids.size()==1&&ids.at("$source")==definition.id,"Native action UUID mismatch");out.products.push_back({definition.id,"action","action.json",json(bytes).dump(),{}});return out;}
}
namespace darkangel {
ActionDefinition load_cooked_action(const std::filesystem::path& registry_path,const std::filesystem::path& cas,AssetId id){using namespace assets_detail;auto registry=json(read(registry_path,65536));require(registry.at("schema")==1&&registry.at("root")==id.text()&&registry.at("assets").is_array()&&registry.at("assets").size()==1,"Action frozen closure");auto record=registry.at("assets")[0];require(record.at("id")==id.text()&&record.at("kind")=="action"&&record.at("extension")=="action.json","Action product profile");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Action digest");auto bytes=read(cas/(hash+".action.json"),65536);require(sha256(bytes)==hash,"Action frozen digest mismatch");auto result=decode_action_source(bytes);require(result.id==id,"Action source/product identity mismatch");return result;}
}
