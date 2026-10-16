#include "assets_internal.hpp"
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/assembly.hpp>
#include <cmath>
#include <set>
namespace darkangel {
using namespace assets_detail;
MaterialInstanceDefinition decode_material_instance(std::string_view text){
    require(text.size()<=16384,"Material source size bound");auto value=json(text,16384);
    require(value.is_object()&&value.at("schema")==1&&value.at("kind")=="material","Material schema/type");
    for(const auto& [key,unused]:value.items())require(key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="family"||key=="color"||key=="roughness"||key=="metallic"||key=="double_sided"||key=="texture"||key=="sources","Material fields: schema, kind, asset, name, family, color, roughness, metallic, double_sided, texture and sources");
    MaterialInstanceDefinition result;result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"Material identity");
    require(value.at("name").is_string(),"Material name type");result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"Material name bounds");
    result.family=value.at("family").get<std::string>();require(result.family==material_family_lit_color,"Unsupported material family; the static renderer draws lit-color-v1");
    const auto& color=value.at("color");require(color.is_array()&&color.size()==4,"Material color is four numbers");
    for(std::size_t i=0;i<4;++i){require(color[i].is_number(),"Material color is four numbers");auto c=color[i].get<double>();require(std::isfinite(c)&&c>=0&&c<=1,"Material color components are 0 to 1");result.color[i]=float(c);}
    auto unit=[&](const char* key){require(value.at(key).is_number(),"Material factors are numbers");auto n=value.at(key).get<double>();require(std::isfinite(n)&&n>=0&&n<=1,"Material roughness and metallic are 0 to 1");return float(n);};
    result.roughness=unit("roughness");result.metallic=unit("metallic");require(value.at("double_sided").is_boolean(),"Material double_sided is a flag");result.double_sided=value.at("double_sided").get<bool>();
    const auto& sources=value.value("sources",Json::object());require(sources.is_object(),"Material sources");
    if(!value.at("texture").is_null()){result.texture=AssetId::parse(value.at("texture").get<std::string>());require(result.texture!=AssetId{}&&sources.contains("texture")&&sources.at("texture").is_string(),"A material texture needs its source locator");result.texture_source=sources.at("texture").get<std::string>();}
    for(const auto& [key,unused]:sources.items())require(key=="texture"&&!result.texture_source.empty(),"Material source locators name the texture only");
    return result;
}
namespace assets_detail {
// The texture a material names: an imported colour texture of the project, checked against its own import identity.
Import material_texture(const std::filesystem::path& root,const MaterialInstanceDefinition& material,bool inspect){
    auto path=within(root,material.texture_source);auto extension=path.extension();require(extension==".png"||extension==".jpg"||extension==".jpeg","A material texture is an imported .png or .jpg");
    auto metadata_path=path;metadata_path+=".daimport";auto metadata=json(read(metadata_path,1024*1024),1024*1024);
    require(metadata.at("importer")=="texture-color-v1"&&AssetId::parse(metadata.at("id").get<std::string>())==material.texture,"Material texture source type/identity");
    auto imported=import_texture(root,path,{{"$source",material.texture}},inspect);imported.inputs[metadata_path.lexically_relative(root).generic_string()]=sha256(read(metadata_path,1024*1024));return imported;
}
Import import_material(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,16384);auto material=decode_material_instance(bytes);if(!inspect)require(ids.size()==1&&ids.at("$source")==material.id,"Material source identity");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(material.id);result.product_count=1;
    // Standalone per-renderable bindings need the same frozen texture closure as model-level assignments.
    std::vector<AssetId> required;
    if(material.texture!=AssetId{}){auto texture=material_texture(root,material,inspect);for(const auto& [path,hash]:texture.inputs)result.inputs[path]=hash;required.push_back(material.texture);
        result.product_count+=texture.product_count?texture.product_count:1;for(auto& product:texture.products)result.products.push_back(std::move(product));}
    if(!inspect)result.products.push_back({material.id,"material_instance","material-instance.json",json(bytes,16384).dump(),required});return result;
}
}
RuntimeMaterial load_cooked_material_instance(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    const auto held=registry_json(registry);const auto& manifest=*held;require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=runtime_package_product_limit,"Material registry contract");
    const Json* selected{};std::set<AssetId> unique;for(const auto& record:manifest.at("assets")){const auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate material registry identity");if(asset==id)selected=&record;}
    require(selected&&selected->at("kind")=="material_instance"&&selected->at("extension")=="material-instance.json","Material registry product type");const auto digest=selected->at("sha256").get<std::string>();require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==digest.npos,"Material product digest");
    const auto bytes=read(cas/(digest+".material-instance.json"),16384);require(sha256(bytes)==digest,"Material product hash mismatch");const auto definition=decode_material_instance(bytes);require(definition.id==id,"Material product identity mismatch");
    RuntimeMaterial result;auto& value=result.value;value.id=id;value.color=definition.color;value.roughness=definition.roughness;value.metallic=definition.metallic;value.double_sided=definition.double_sided;value.texture=definition.texture;value.has_texture=definition.texture!=AssetId{};
    std::string generation=digest;if(value.has_texture){result.textures.emplace_back(value.texture,load_cooked_texture(registry,cas,value.texture));for(const auto& record:manifest.at("assets"))if(record.at("id")==value.texture.text())generation+=record.at("sha256").get<std::string>();}result.generation=sha256(generation);return result;
}
void validate_cooked_material_bindings(const std::filesystem::path& registry,const std::filesystem::path& cas,const SpawnPlan& plan){
    require(plan.materials.size()<=scene_object_limit,"Material binding object limit");std::map<AssetId,std::set<AssetId>> available;std::set<AssetId> loaded;
    for(const auto& [object,bindings]:plan.materials){require(plan.models.contains(object)&&bindings.size()<=128,"Material bindings require a model and bounded slots");const auto model=plan.models.at(object);
        if(!available.contains(model)){const auto cooked=load_cooked_model(registry,cas,model);auto& slots=available[model];for(const auto& material:cooked.materials)slots.insert(material.id);}
        for(const auto& [slot,material]:bindings){require(available.at(model).contains(slot),"A material slot was removed; repair the preserved assignment before Play");if(loaded.insert(material).second)load_cooked_material_instance(registry,cas,material);}}
}

}
