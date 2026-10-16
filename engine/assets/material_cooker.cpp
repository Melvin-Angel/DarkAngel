#include "assets_internal.hpp"
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <cmath>
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
    // The texture is validated (and its closure recorded) here; models that assign this material carry the texture product.
    if(material.texture!=AssetId{}){auto texture=material_texture(root,material,true);for(const auto& [path,hash]:texture.inputs)result.inputs[path]=hash;}
    if(!inspect)result.products.push_back({material.id,"material_instance","material-instance.json",json(bytes,16384).dump(),{}});return result;
}
}
}
