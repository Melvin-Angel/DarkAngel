#include "assets_internal.hpp"
#include <darkangel/cue_assets.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel::assets_detail {
// A cue is a leaf source: validated fields, no dependencies yet (its visual and sound are names until the effect and sound assets exist).
Import import_cue(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){auto bytes=read(source,4096);auto cue=decode_cue(bytes);Import result;result.identities.push_back(cue.id);result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);if(!inspect){require(ids.size()==1&&ids.at("$source")==cue.id,"Cue source UUID mismatch");result.products.push_back({cue.id,"cue","cue.json",json(bytes,4096).dump(),{}});}return result;}
}
namespace darkangel {
std::vector<CueDefinition> load_cooked_cues(const std::filesystem::path& registry,const std::filesystem::path& cas){
    using namespace assets_detail;auto manifest=json(read(registry,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=512,"Cue registry schema/limit");std::set<AssetId> unique;std::set<std::string> keys;std::vector<CueDefinition> cues;
    for(const auto& record:manifest.at("assets")){auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate cue registry UUID");if(record.at("kind")!="cue")continue;require(record.at("extension")=="cue.json","Cue product type mismatch");
        auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Cue product digest");auto bytes=read(cas/(hash+".cue.json"),4096);require(sha256(bytes)==hash,"Cue product hash mismatch");
        auto cue=decode_cue(bytes);require(cue.id==asset,"Cooked cue identity mismatch");require(keys.insert(cue.key).second,"Two cues answer to the same key");cues.push_back(std::move(cue));}
    return cues;
}
}
