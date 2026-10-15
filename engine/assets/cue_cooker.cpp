#include "assets_internal.hpp"
#include <darkangel/cue_assets.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel::assets_detail {
// A cue's effect and sound are resolved to identities through their import sidecars, which are inputs of the cue. Their products are not
// carried in the cue's closure (cooking an effect runs the Effekseer tool); whoever packages a cue adds the two identities as roots.
Import import_cue(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,4096);auto data=json(bytes,4096);require(data.is_object()&&!data.contains("visual_asset")&&!data.contains("audio_asset")&&!data.contains("trail_asset")&&!data.contains("decal_asset"),"A cue source names its effect and sound by path; the cooker resolves them");auto cue=decode_cue(bytes);
    Import result;result.identities.push_back(cue.id);result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    const auto resolve=[&](const std::string& name,bool visual){if(name.empty())return std::string{};const auto file=within(root,name);const auto extension=file.extension();
        require(visual?(extension==".efkproj"||extension==".efkefc"):(extension==".wav"||extension==".dasound"),visual?"A cue's effect is an Effekseer effect of the project (.efkproj or .efkefc)":"A cue's sound is a sound (.wav) or a sound definition (.dasound) of the project");
        // A sound definition is a native source: its identity is in the file itself.
        if(extension==".dasound"){require(std::filesystem::is_regular_file(file),"A cue names a sound that is not in the project");const auto text=read(file,8192);const auto definition=json(text,8192);require(definition.is_object()&&definition.value("kind",std::string{})=="sound"&&definition.contains("asset"),"A cue's sound definition is not a sound definition");result.inputs[file.lexically_relative(root).generic_string()]=sha256(text);return AssetId::parse(definition.at("asset").get<std::string>()).text();}
        auto sidecar=file;sidecar+=".daimport";require(std::filesystem::is_regular_file(file)&&std::filesystem::is_regular_file(sidecar),visual?"A cue names an effect that is not in the project":"A cue names a sound that is not in the project");
        const auto text=read(sidecar,1024*1024);const auto metadata=json(text);require(metadata.at("schema")==1&&metadata.at("importer")==(visual?"effekseer-v1":"sound-wav-v1"),visual?"A cue's effect was not imported as an effect":"A cue's sound was not imported as a sound");
        result.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(text);return AssetId::parse(metadata.at("id").get<std::string>()).text();};
    data["visual_asset"]=resolve(cue.visual,true);data["audio_asset"]=resolve(cue.audio,false);
    // A trail and a decal are native sources: their identity is in the file itself, and the file is an input of the cue.
    const auto native=[&](const std::string& name,const char* extension,const char* kind,const char* error){const auto file=within(root,name);require(file.extension()==extension&&std::filesystem::is_regular_file(file),error);const auto text=read(file,4096);const auto definition=json(text,4096);require(definition.is_object()&&definition.value("kind",std::string{})==kind&&definition.contains("asset"),error);result.inputs[file.lexically_relative(root).generic_string()]=sha256(text);return AssetId::parse(definition.at("asset").get<std::string>()).text();};
    if(!cue.trail.empty())data["trail_asset"]=native(cue.trail,".datrail","trail","A cue's trail is a weapon trail of the project (.datrail)");else data.erase("trail");
    if(!cue.decal.empty())data["decal_asset"]=native(cue.decal,".dadecal","decal","A cue's decal is a decal of the project (.dadecal)");else data.erase("decal");
    if(!inspect){require(ids.size()==1&&ids.at("$source")==cue.id,"Cue source UUID mismatch");result.products.push_back({cue.id,"cue","cue.json",data.dump(),{}});}return result;}
}
namespace darkangel {
std::vector<CueDefinition> load_cooked_cues(const std::filesystem::path& registry,const std::filesystem::path& cas){
    using namespace assets_detail;const auto manifest_held=registry_json(registry);const auto& manifest=*manifest_held;require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=runtime_package_product_limit,"Cue registry schema/limit");std::set<AssetId> unique;std::set<std::string> keys;std::vector<CueDefinition> cues;
    for(const auto& record:manifest.at("assets")){auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate cue registry UUID");if(record.at("kind")!="cue")continue;require(record.at("extension")=="cue.json","Cue product type mismatch");
        auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Cue product digest");auto bytes=read(cas/(hash+".cue.json"),4096);require(sha256(bytes)==hash,"Cue product hash mismatch");
        auto cue=decode_cue(bytes);require(cue.id==asset,"Cooked cue identity mismatch");require(keys.insert(cue.key).second,"Two cues answer to the same key");cues.push_back(std::move(cue));}
    return cues;
}
}
