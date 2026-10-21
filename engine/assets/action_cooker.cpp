#include "assets_internal.hpp"
#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
#include <set>
#include <algorithm>
#include <darkangel/joint_mask.hpp>
#include <darkangel/effect_assets.hpp>
namespace darkangel::assets_detail {
Import import_action(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    static thread_local std::set<std::filesystem::path> loading;
    const auto identity=std::filesystem::absolute(source).lexically_normal();require(loading.size()<8&&loading.insert(identity).second,"Composer/effect dependency cycle or depth limit");
    struct Guard{std::set<std::filesystem::path>& paths;std::filesystem::path path;~Guard(){paths.erase(path);}} guard{loading,identity};
    auto bytes=read(source,65536);auto data=json(bytes,65536);auto timeline=data;const bool bound=data.at("schema")==2;
    Json binding;if(bound){binding=data.at("motion");require(binding.is_object()&&binding.size()==3,"Action source clip binding fields");timeline.erase("motion");timeline.erase("mask");timeline["schema"]=1;}
    auto definition=decode_action_source(timeline.dump());Import out;out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);out.identities.push_back(definition.id);require(!data.contains("mask")||(bound&&definition.upper_body),"Action mask needs a bound upper-body action");out.product_count=1;
    if(!inspect)require(ids.size()==1&&ids.at("$source")==definition.id,"Native action UUID mismatch");
    std::vector<AssetId> dependencies;
    if(bound){
#if defined(DAE_ANIMATION)
        auto policy=binding.at("policy").get<std::string>();require(policy=="motor"||policy=="none","Action source root policy");require(!(definition.upper_body&&policy=="motor"),"Upper-body action cannot own root");
        auto clip_id=AssetId::parse(binding.at("clip").get<std::string>());auto path=within(root,binding.at("source").get<std::string>());require(path.extension()==".glb","Action clip source type");auto sidecar=path;sidecar+=".daimport";auto metadata_bytes=read(sidecar,65536);auto metadata=json(metadata_bytes,65536);require(metadata.at("schema")==1&&metadata.at("importer")=="clip-gltf-v1"&&metadata.at("id")==clip_id.text()&&metadata.at("subassets").is_object()&&metadata.at("subassets").size()==1,"Action clip import profile/identity");
        auto runtime=AssetId::parse(metadata.at("subassets").at("runtime").get<std::string>());auto canonical=within(root,metadata.at("canonical_source").get<std::string>());require(canonical.extension()==".daskeleton","Action canonical source type");auto rig=decode_rig_source(read(canonical,1024*1024));std::set<AssetId> unique{clip_id,runtime,rig.id,rig.runtime,definition.id};require(unique.size()==5,"Action binding dependency identity conflict");
        dependencies={clip_id,runtime,rig.id,rig.runtime};out.identities.insert(out.identities.end(),dependencies.begin(),dependencies.end());out.product_count=5;
        auto clip=import_clip(root,path,{{"$source",clip_id},{"runtime",runtime}},canonical,metadata.at("loop").get<bool>(),inspect);out.inputs.insert(clip.inputs.begin(),clip.inputs.end());out.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(metadata_bytes);
        std::optional<Import> mask;
        if(data.contains("mask")){
            const auto& reference=data.at("mask");require(reference.is_object()&&reference.size()==2,"Action mask reference fields");auto mask_id=AssetId::parse(reference.at("asset").get<std::string>());auto mask_path=within(root,reference.at("source").get<std::string>());require(mask_path.extension()==".damask"&&unique.insert(mask_id).second,"Action mask source type/identity");
            auto mask_definition=decode_joint_mask_source(read(mask_path,65536));require(mask_definition.id==mask_id&&mask_definition.skeleton==rig.id,"Action mask source identity/rig mismatch");auto compiled=compile_joint_mask(mask_definition,rig);
            require(std::any_of(compiled.weights.begin(),compiled.weights.end(),[](float w){return w>0;}),"Action mask must select joints");for(unsigned joint=0;joint<rig.joints.size();++joint)if(rig.joints[joint].parent<0)require(compiled.weights[joint]==0,"Upper-body mask must preserve rig roots");
            mask=import_joint_mask(root,mask_path,{{"$source",mask_id}},inspect);out.inputs.insert(mask->inputs.begin(),mask->inputs.end());dependencies.push_back(mask_id);out.identities.push_back(mask_id);++out.product_count;
        }
        if(!inspect){Json frozen=Json::object(),manifest;std::string archive_generation;for(auto& product:clip.products){frozen[product.id.text()]=sha256(product.bytes);if(product.id==clip_id)manifest=json(product.bytes);if(product.id==runtime)archive_generation=sha256(product.bytes);out.products.push_back(std::move(product));}if(mask)for(auto& product:mask->products)if(product.id==dependencies.back()){frozen[product.id.text()]=sha256(product.bytes);data["mask"]=json(product.bytes);out.products.push_back(std::move(product));}data["motion"]={{"clip",manifest},{"archive_generation",archive_generation},{"policy",policy}};data["frozen"]=std::move(frozen);decode_action_source(data.dump());}
#else
        throw std::runtime_error("Action clip binding cook requires explicit animation tools build");
#endif
    }
    // Effects carry source-only locators and are pinned by product digest in the
    // cooked block. Reactions may import presentation actions, but never cycles.
    std::map<AssetId,Product> products;for(auto& product:out.products)products.emplace(product.id,std::move(product));out.products.clear();
    std::set<AssetId> identities(out.identities.begin(),out.identities.end());
    for(auto& block:data["blocks"])if(block.contains("effects"))for(auto& reference:block["effects"]["entries"]){
        auto effect=AssetId::parse(reference.at("asset").get<std::string>());auto path=within(root,reference.at("source").get<std::string>());
        require(effect!=definition.id&&path.extension()==".daeffect","Composer effect reference type/identity");
        auto imported=import_effect(root,path,{{"$source",effect}},inspect);
        for(const auto& [input,digest]:imported.inputs){auto [entry,inserted]=out.inputs.emplace(input,digest);require(inserted||entry->second==digest,"Conflicting composer effect input");}
        identities.insert(imported.identities.begin(),imported.identities.end());dependencies.push_back(effect);out.product_count+=imported.product_count;
        if(!inspect){for(auto& product:imported.products){if(product.id==effect)reference["generation"]=sha256(product.bytes);auto [entry,inserted]=products.emplace(product.id,product);require(inserted||(entry->second.bytes==product.bytes&&entry->second.kind==product.kind&&entry->second.required==product.required),"Conflicting composer effect product");}require(reference.contains("generation"),"Missing composer effect product");reference.erase("source");}
    }
    for(auto& block:data["blocks"])if(block.contains("projectile")&&block["projectile"].contains("visual_model")){
        auto& shot=block["projectile"];auto model=AssetId::parse(shot.at("visual_model").get<std::string>());auto model_path=within(root,shot.at("model_source").get<std::string>());require(model_path.extension()==".glb"||model_path.extension()==".gltf","Composer projectile model source type");auto sidecar=model_path;sidecar+=".daimport";auto metadata_bytes=read(sidecar,1024*1024);auto metadata=json(metadata_bytes,1024*1024);require(metadata.at("importer")=="static-gltf-v1"&&AssetId::parse(metadata.at("id").get<std::string>())==model,"Composer projectile needs an adopted static model");std::map<std::string,AssetId> mapping{{"$source",model}};for(const auto& [key,value]:metadata.at("subassets").items())mapping.emplace(key,AssetId::parse(value.get<std::string>()));auto imported=import_gltf(root,model_path,mapping,inspect,false,true);for(const auto& [key,id]:mapping){(void)key;identities.insert(id);}out.product_count+=imported.product_count;out.inputs.insert(imported.inputs.begin(),imported.inputs.end());out.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(metadata_bytes);dependencies.push_back(model);if(!inspect){for(auto& product:imported.products){if(product.id==model)shot["model_generation"]=sha256(product.bytes);auto [entry,inserted]=products.emplace(product.id,product);require(inserted||entry->second.bytes==product.bytes,"Conflicting composer projectile model");}shot.erase("model_source");}
    }
    out.identities.assign(identities.begin(),identities.end());out.product_count=identities.size();std::sort(dependencies.begin(),dependencies.end());dependencies.erase(std::unique(dependencies.begin(),dependencies.end()),dependencies.end());
    if(!inspect){for(auto& [id,product]:products)out.products.push_back(std::move(product));decode_action_source(data.dump());out.products.push_back({definition.id,"action","action.json",data.dump(),std::move(dependencies)});}return out;
}
}
namespace darkangel {
ActionDefinition load_cooked_action(const std::filesystem::path& path,const std::filesystem::path& cas,AssetId id){
    static thread_local std::set<AssetId> loading; if(loading.size()>=8||!loading.insert(id).second)throw std::runtime_error("Cooked composer/effect dependency cycle or depth limit");struct Guard{std::set<AssetId>& ids;AssetId id;~Guard(){ids.erase(id);}} guard{loading,id};
    using namespace assets_detail;const auto registry_held=registry_json(path);const auto& registry=*registry_held;require(registry.at("schema")==1&&registry.at("assets").is_array()&&!registry.at("assets").empty()&&registry.at("assets").size()<=runtime_package_product_limit,"Action registry schema/count");std::map<AssetId,Json> records;for(const auto& record:registry.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate action registry identity");require(records.contains(AssetId::parse(registry.at("root").get<std::string>())),"Action registry root missing");
    auto load=[&](AssetId asset,const char* kind,const char* extension){const auto& record=records.at(asset);require(record.at("kind")==kind&&record.at("extension")==extension,"Action dependency product type");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Action dependency digest");auto bytes=read(cas/(hash+"."+extension),16*1024*1024);require(sha256(bytes)==hash,"Action dependency hash mismatch");return bytes;};
    auto bytes=load(id,"action","action.json");auto result=decode_action_source(bytes);require(result.id==id,"Action source/product identity mismatch");
    if(result.motion){auto data=json(bytes,65536);for(const auto& [key,digest]:data.at("frozen").items())require(records.at(AssetId::parse(key)).at("sha256")==digest,"Action frozen dependency generation mismatch");const auto& clip=result.motion->clip;auto clip_bytes=load(clip.id,"clip","clip.json");require(sha256(data.at("motion").at("clip").dump())==sha256(clip_bytes),"Action frozen clip descriptor mismatch");load(clip.runtime,"ozz-animation","ozzanim");auto rig_manifest=json(load(clip.skeleton,"skeleton","skeleton.json"));auto rig=decode_rig_source(rig_manifest.at("definition").dump());require(rig.id==clip.skeleton&&rig.signature==clip.signature&&rig.joints.size()==clip.joints&&data.at("frozen").contains(rig.runtime.text()),"Action canonical rig compatibility");load(rig.runtime,"ozz-skeleton","ozz");if(result.mask){require(load(result.mask->id,"joint-mask","joint-mask.json")==data.at("mask").dump(),"Action frozen mask descriptor mismatch");for(unsigned joint=0;joint<rig.joints.size();++joint)if(rig.joints[joint].parent<0)require(result.mask->weights[joint]==0,"Upper-body mask must preserve rig roots");}}
    const auto source=json(bytes,65536);for(auto& block:result.blocks){auto authored=std::find_if(source.at("blocks").begin(),source.at("blocks").end(),[&](const auto& value){return value.at("id")==block.id;});for(unsigned effect=0;effect<block.effects.size();++effect){auto& reference=block.effects[effect];const auto& pinned=authored->at("effects").at("entries")[effect];require(records.at(reference.asset).at("sha256")==pinned.at("generation"),"Composer frozen effect generation mismatch");auto loaded=load_cooked_effect(path,cas,reference.asset);reference.definition=std::move(loaded.definition);}if(block.projectile&&block.projectile->visual_model!=AssetId{})require(records.at(block.projectile->visual_model).at("sha256")==authored->at("projectile").at("model_generation"),"Composer frozen projectile model generation mismatch");}
    return result;
}
}
