#include "assets_internal.hpp"
#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
#include <set>
namespace darkangel::assets_detail {
Import import_action(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,65536);auto data=json(bytes,65536);auto timeline=data;const bool bound=data.at("schema")==2;
    Json binding;if(bound){binding=data.at("motion");require(binding.is_object()&&binding.size()==3,"Action source clip binding fields");timeline.erase("motion");timeline["schema"]=1;}
    auto definition=decode_action_source(timeline.dump());Import out;out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);out.identities.push_back(definition.id);out.product_count=1;
    if(!inspect)require(ids.size()==1&&ids.at("$source")==definition.id,"Native action UUID mismatch");
    std::vector<AssetId> dependencies;
    if(bound){
#if defined(DAE_ANIMATION)
        auto policy=binding.at("policy").get<std::string>();require(policy=="motor"||policy=="none","Action source root policy");require(!(definition.upper_body&&policy=="motor"),"Upper-body action cannot own root");
        auto clip_id=AssetId::parse(binding.at("clip").get<std::string>());auto path=within(root,binding.at("source").get<std::string>());require(path.extension()==".glb","Action clip source type");auto sidecar=path;sidecar+=".daimport";auto metadata_bytes=read(sidecar,65536);auto metadata=json(metadata_bytes,65536);require(metadata.at("schema")==1&&metadata.at("importer")=="clip-gltf-v1"&&metadata.at("id")==clip_id.text()&&metadata.at("subassets").is_object()&&metadata.at("subassets").size()==1,"Action clip import profile/identity");
        auto runtime=AssetId::parse(metadata.at("subassets").at("runtime").get<std::string>());auto canonical=within(root,metadata.at("canonical_source").get<std::string>());require(canonical.extension()==".daskeleton","Action canonical source type");auto rig=decode_rig_source(read(canonical,1024*1024));std::set<AssetId> unique{clip_id,runtime,rig.id,rig.runtime,definition.id};require(unique.size()==5,"Action binding dependency identity conflict");
        dependencies={clip_id,runtime,rig.id,rig.runtime};out.identities.insert(out.identities.end(),dependencies.begin(),dependencies.end());out.product_count=5;
        auto clip=import_clip(root,path,{{"$source",clip_id},{"runtime",runtime}},canonical,metadata.at("loop").get<bool>(),inspect);out.inputs.insert(clip.inputs.begin(),clip.inputs.end());out.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(metadata_bytes);
        if(!inspect){Json frozen=Json::object(),manifest;std::string archive_generation;for(auto& product:clip.products){frozen[product.id.text()]=sha256(product.bytes);if(product.id==clip_id)manifest=json(product.bytes);if(product.id==runtime)archive_generation=sha256(product.bytes);out.products.push_back(std::move(product));}data["motion"]={{"clip",manifest},{"archive_generation",archive_generation},{"policy",policy}};data["frozen"]=std::move(frozen);decode_action_source(data.dump());}
#else
        throw std::runtime_error("Action clip binding cook requires explicit animation tools build");
#endif
    }
    if(!inspect)out.products.push_back({definition.id,"action","action.json",data.dump(),std::move(dependencies)});return out;
}
}
namespace darkangel {
ActionDefinition load_cooked_action(const std::filesystem::path& path,const std::filesystem::path& cas,AssetId id){
    using namespace assets_detail;auto registry=json(read(path,1024*1024));require(registry.at("schema")==1&&registry.at("assets").is_array()&&!registry.at("assets").empty()&&registry.at("assets").size()<=512,"Action registry schema/count");std::map<AssetId,Json> records;for(const auto& record:registry.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate action registry identity");require(records.contains(AssetId::parse(registry.at("root").get<std::string>())),"Action registry root missing");
    auto load=[&](AssetId asset,const char* kind,const char* extension){const auto& record=records.at(asset);require(record.at("kind")==kind&&record.at("extension")==extension,"Action dependency product type");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Action dependency digest");auto bytes=read(cas/(hash+"."+extension),16*1024*1024);require(sha256(bytes)==hash,"Action dependency hash mismatch");return bytes;};
    auto bytes=load(id,"action","action.json");auto result=decode_action_source(bytes);require(result.id==id,"Action source/product identity mismatch");
    if(result.motion){auto data=json(bytes,65536);for(const auto& [key,digest]:data.at("frozen").items())require(records.at(AssetId::parse(key)).at("sha256")==digest,"Action frozen dependency generation mismatch");const auto& clip=result.motion->clip;auto clip_bytes=load(clip.id,"clip","clip.json");require(sha256(data.at("motion").at("clip").dump())==sha256(clip_bytes),"Action frozen clip descriptor mismatch");load(clip.runtime,"ozz-animation","ozzanim");auto rig_manifest=json(load(clip.skeleton,"skeleton","skeleton.json"));auto rig=decode_rig_source(rig_manifest.at("definition").dump());require(rig.id==clip.skeleton&&rig.signature==clip.signature&&rig.joints.size()==clip.joints&&data.at("frozen").contains(rig.runtime.text()),"Action canonical rig compatibility");load(rig.runtime,"ozz-skeleton","ozz");}
    return result;
}
}
