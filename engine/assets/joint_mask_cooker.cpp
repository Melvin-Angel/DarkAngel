#include "assets_internal.hpp"
#include <darkangel/joint_mask.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/hash.hpp>
#include <cmath>
namespace darkangel::assets_detail {
Import import_joint_mask(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,65536);auto definition=decode_joint_mask_source(bytes);auto canonical=within(root,definition.canonical_source);auto rig_bytes=read(canonical,1024*1024);auto rig=decode_rig_source(rig_bytes);auto mask=compile_joint_mask(definition,rig);
    require(mask.id!=rig.id&&mask.id!=rig.runtime,"Mask/rig UUID collision");
    auto out=import_skeleton(root,canonical,{{"$source",rig.id},{"runtime",rig.runtime}},inspect);out.keys.clear();out.product_count=3;out.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    if(inspect)return out;require(ids.size()==1&&ids.at("$source")==mask.id,"Native mask UUID mismatch");
    Json manifest={{"schema",1},{"kind","joint_mask"},{"id",mask.id.text()},{"skeleton",mask.skeleton.text()},{"signature",mask.signature},{"generation",mask.generation},{"weights",mask.weights}};
    out.products.push_back({mask.id,"joint-mask","joint-mask.json",manifest.dump(),{rig.id}});return out;
}
}
namespace darkangel {
JointMaskDefinition load_cooked_joint_mask(const std::filesystem::path& path,const std::filesystem::path& cas,AssetId id){
    using namespace assets_detail;auto registry=json(read(path,65536));require(registry.at("schema")==1&&registry.at("root")==id.text()&&registry.at("assets").is_array()&&registry.at("assets").size()==3,"Mask frozen closure");
    std::map<AssetId,Json> records;for(auto record:registry.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate mask registry UUID");
    require(records.contains(id),"Missing mask product");const auto& record=records.at(id);require(record.at("kind")=="joint-mask"&&record.at("extension")=="joint-mask.json","Mask product type");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Mask artifact digest");auto bytes=read(cas/(hash+".joint-mask.json"),65536);require(sha256(bytes)==hash,"Mask artifact digest mismatch");auto data=json(bytes);
    require(data.is_object()&&data.size()==7&&data.at("schema")==1&&data.at("kind")=="joint_mask"&&data.at("id")==id.text()&&data.at("weights").is_array()&&data.at("weights").size()<=256,"Cooked mask schema/identity");
    JointMaskDefinition result{id,AssetId::parse(data.at("skeleton").get<std::string>()),data.at("signature").get<std::string>(),data.at("generation").get<std::string>(),data.at("weights").get<std::vector<float>>()};
    require(result.generation.size()==64&&result.generation.find_first_not_of("0123456789abcdef")==result.generation.npos,"Mask generation");auto rig=load_cooked_rig(path,cas,result.skeleton);require(records.contains(rig.definition.runtime)&&result.signature==rig.definition.signature&&result.weights.size()==rig.definition.joints.size(),"Cooked mask compatibility");for(float weight:result.weights)require(std::isfinite(weight)&&weight>=0&&weight<=1,"Cooked mask weight bounds");return result;
}
}
