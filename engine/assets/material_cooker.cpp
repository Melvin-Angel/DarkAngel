#include "assets_internal.hpp"
#include <darkangel/animation_assets.hpp>
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/assembly.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace darkangel {
using namespace assets_detail;
std::string_view texture_importer(TextureSemantic semantic){return semantic==TextureSemantic::Color?"texture-color-v1":semantic==TextureSemantic::Data?"texture-data-v1":"texture-normal-v1";}
std::optional<TextureSemantic> texture_semantic(std::string_view importer){if(importer=="texture-color-v1")return TextureSemantic::Color;if(importer=="texture-data-v1")return TextureSemantic::Data;if(importer=="texture-normal-v1")return TextureSemantic::Normal;return std::nullopt;}
std::string_view texture_semantic_label(TextureSemantic semantic){return semantic==TextureSemantic::Color?"Colour (sRGB)":semantic==TextureSemantic::Data?"Data (linear)":"Normal map (linear)";}
std::string_view alpha_mode_name(AlphaMode mode){return mode==AlphaMode::Opaque?"opaque":mode==AlphaMode::Cutout?"cutout":"transparent";}
const std::vector<MaterialDefinition>& material_definitions(){
    static const std::vector<MaterialDefinition> families{
        {std::string(material_family_lit_color),{"color","roughness","metallic","double_sided"},{{"texture",TextureSemantic::Color}},{AlphaMode::Opaque}},
        {std::string(material_family_lit_surface),{"color","roughness","metallic","double_sided","normal_scale","occlusion_strength","emissive","alpha_mode","alpha_cutoff"},
            {{"texture",TextureSemantic::Color},{"normal_texture",TextureSemantic::Normal},{"surface_texture",TextureSemantic::Data},{"emissive_texture",TextureSemantic::Color}},{AlphaMode::Opaque,AlphaMode::Cutout,AlphaMode::Transparent}},
        {std::string(material_family_toon_standard),{"color","roughness","metallic","double_sided","normal_scale","occlusion_strength","toon_thresholds","toon_softness","shadow_tint","ambient_color","ambient_strength","local_light_strength","triplanar","triplanar_scale","emissive","alpha_mode","alpha_cutoff"},
            {{"texture",TextureSemantic::Color},{"normal_texture",TextureSemantic::Normal},{"surface_texture",TextureSemantic::Data},{"emissive_texture",TextureSemantic::Color}},{AlphaMode::Opaque,AlphaMode::Cutout,AlphaMode::Transparent}},
        {std::string(material_family_toon_character),{"color","roughness","metallic","double_sided","normal_scale","occlusion_strength","toon_thresholds","toon_softness","shadow_tint","ambient_color","ambient_strength","local_light_strength","triplanar","triplanar_scale","emissive","alpha_mode","alpha_cutoff","hit_flash_color","hit_flash_strength","status_color","status_strength","dissolve","dissolve_edge","dissolve_edge_width"},
            {{"texture",TextureSemantic::Color},{"normal_texture",TextureSemantic::Normal},{"surface_texture",TextureSemantic::Data},{"emissive_texture",TextureSemantic::Color},{"dissolve_texture",TextureSemantic::Color}},{AlphaMode::Opaque,AlphaMode::Cutout,AlphaMode::Transparent}},
        {std::string(material_family_toon_terrain),{"color","roughness","metallic","double_sided","toon_thresholds","toon_softness","shadow_tint","ambient_color","ambient_strength","local_light_strength","triplanar","triplanar_scale","terrain_blend_sharpness","terrain_slope_start","terrain_slope_end","terrain_height_blend"},
            {{"texture",TextureSemantic::Color},{"terrain_layer1",TextureSemantic::Color},{"terrain_layer2",TextureSemantic::Color}},{AlphaMode::Opaque,AlphaMode::Cutout}}};
    return families;
}
const MaterialDefinition& material_definition(std::string_view family){for(const auto& definition:material_definitions())if(definition.family==family)return definition;throw std::runtime_error("Unsupported material family: "+std::string(family));}
std::vector<std::pair<MaterialTextureRef,TextureSemantic>> MaterialInstanceDefinition::textures() const{
    std::vector<std::pair<MaterialTextureRef,TextureSemantic>> result;if(texture!=AssetId{})result.push_back({{texture,texture_source},TextureSemantic::Color});
    if(normal_texture)result.push_back({normal_texture,TextureSemantic::Normal});if(surface_texture)result.push_back({surface_texture,TextureSemantic::Data});if(emissive_texture)result.push_back({emissive_texture,TextureSemantic::Color});
    if(dissolve_texture)result.push_back({dissolve_texture,TextureSemantic::Color});if(terrain_layer1)result.push_back({terrain_layer1,TextureSemantic::Color});if(terrain_layer2)result.push_back({terrain_layer2,TextureSemantic::Color});return result;}
MaterialInstanceDefinition decode_material_instance(std::string_view text){
    require(text.size()<=16384,"Material source size bound");auto value=json(text,16384);
    require(value.is_object()&&value.at("schema")==1&&value.at("kind")=="material","Material schema/type");
    MaterialInstanceDefinition result;result.family=value.at("family").get<std::string>();const auto& definition=material_definition(result.family);const bool surface=result.family==material_family_lit_surface;const bool toon=result.family==material_family_toon_standard||result.family==material_family_toon_character||result.family==material_family_toon_terrain;const bool surface_maps=surface||result.family==material_family_toon_standard||result.family==material_family_toon_character;const bool character=result.family==material_family_toon_character;const bool terrain=result.family==material_family_toon_terrain;
    for(const auto& [key,unused]:value.items()){bool known=key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="family"||key=="sources";for(const auto& parameter:definition.parameters)known|=key==parameter;for(const auto& slot:definition.textures)known|=key==slot.key;
        require(known,"Material field "+key+" is not part of the "+result.family+" family");}
    result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"Material identity");
    require(value.at("name").is_string(),"Material name type");result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"Material name bounds");
    const auto& color=value.at("color");require(color.is_array()&&color.size()==4,"Material color is four numbers");
    for(std::size_t i=0;i<4;++i){require(color[i].is_number(),"Material color is four numbers");auto c=color[i].get<double>();require(std::isfinite(c)&&c>=0&&c<=1,"Material color components are 0 to 1");result.color[i]=float(c);}
    auto ranged=[&](const char* key,double low,double high,double fallback){if(!value.contains(key))return float(fallback);require(value.at(key).is_number(),"Material factors are numbers");auto n=value.at(key).get<double>();require(std::isfinite(n)&&n>=low&&n<=high,std::string("Material ")+key+" is out of range");return float(n);};
    require(value.contains("roughness")&&value.contains("metallic"),"Material roughness and metallic");result.roughness=ranged("roughness",0,1,1);result.metallic=ranged("metallic",0,1,0);
    require(value.at("double_sided").is_boolean(),"Material double_sided is a flag");result.double_sided=value.at("double_sided").get<bool>();
    const auto& sources=value.value("sources",Json::object());require(sources.is_object(),"Material sources");
    // A texture slot holds an AssetID (or null) and needs its source locator under the same key in "sources".
    auto texture_ref=[&](const char* key){MaterialTextureRef ref;if(!value.contains(key)||value.at(key).is_null())return ref;ref.id=AssetId::parse(value.at(key).get<std::string>());require(ref.id!=AssetId{}&&sources.contains(key)&&sources.at(key).is_string(),std::string("A material texture needs its source locator: sources.")+key);ref.source=sources.at(key).get<std::string>();return ref;};
    require(value.contains("texture"),"Material base-colour texture field (null for none)");{auto base=texture_ref("texture");result.texture=base.id;result.texture_source=base.source;}
    if(surface_maps){
        result.normal_texture=texture_ref("normal_texture");result.surface_texture=texture_ref("surface_texture");result.emissive_texture=texture_ref("emissive_texture");
        result.normal_scale=ranged("normal_scale",0,2,1);result.occlusion_strength=ranged("occlusion_strength",0,1,1);result.alpha_cutoff=ranged("alpha_cutoff",0,1,.5);
        if(value.contains("emissive")){const auto& emissive=value.at("emissive");require(emissive.is_array()&&emissive.size()==3,"Material emissive is three numbers");for(std::size_t i=0;i<3;++i){require(emissive[i].is_number(),"Material emissive is three numbers");auto e=emissive[i].get<double>();require(std::isfinite(e)&&e>=0&&e<=16,"Material emissive components are 0 to 16");result.emissive[i]=float(e);}}
        const auto mode=value.value("alpha_mode",std::string("opaque"));result.alpha_mode=mode=="opaque"?AlphaMode::Opaque:mode=="cutout"?AlphaMode::Cutout:mode=="transparent"?AlphaMode::Transparent:throw std::runtime_error("Material alpha_mode is opaque, cutout or transparent");}
    if(toon){
        auto array=[&](const char* key,std::size_t count,float low,float high,const auto& fallback){if(!value.contains(key))return fallback;const auto& v=value.at(key);require(v.is_array()&&v.size()==count,std::string("Material ")+key+" has the wrong size");auto result=fallback;for(std::size_t i=0;i<count;++i){require(v[i].is_number(),std::string("Material ")+key+" must contain numbers");const auto n=v[i].get<double>();require(std::isfinite(n)&&n>=low&&n<=high,std::string("Material ")+key+" is out of range");result[i]=float(n);}return result;};
        result.toon_thresholds=array("toon_thresholds",3,0,1,result.toon_thresholds);require(result.toon_thresholds[0]<=result.toon_thresholds[1]&&result.toon_thresholds[1]<=result.toon_thresholds[2],"Material toon thresholds must be ordered");
        result.shadow_tint=array("shadow_tint",3,0,1,result.shadow_tint);result.ambient_color=array("ambient_color",3,0,16,result.ambient_color);
        result.toon_softness=ranged("toon_softness",0,.5,.035);result.ambient_strength=ranged("ambient_strength",0,16,1);result.local_light_strength=ranged("local_light_strength",0,16,1);if(value.contains("triplanar")){require(value.at("triplanar").is_boolean(),"Material triplanar is a flag");result.triplanar=value.at("triplanar").get<bool>();}result.triplanar_scale=ranged("triplanar_scale",.001,100,1);
        result.emissive=array("emissive",3,0,16,result.emissive);result.emissive_texture=texture_ref("emissive_texture");const auto mode=value.value("alpha_mode",std::string("opaque"));result.alpha_mode=mode=="opaque"?AlphaMode::Opaque:mode=="cutout"?AlphaMode::Cutout:mode=="transparent"?AlphaMode::Transparent:throw std::runtime_error("Material alpha_mode is opaque, cutout or transparent");result.alpha_cutoff=ranged("alpha_cutoff",0,1,.5);
        if(character){result.hit_flash_color=array("hit_flash_color",4,0,16,result.hit_flash_color);result.status_color=array("status_color",4,0,16,result.status_color);result.hit_flash_strength=ranged("hit_flash_strength",0,16,0);result.status_strength=ranged("status_strength",0,16,0);result.dissolve=ranged("dissolve",0,1,0);result.dissolve_texture=texture_ref("dissolve_texture");result.dissolve_edge=array("dissolve_edge",3,0,16,result.dissolve_edge);result.dissolve_edge_width=ranged("dissolve_edge_width",0,1,.05);}
        if(terrain){result.terrain_layer1=texture_ref("terrain_layer1");result.terrain_layer2=texture_ref("terrain_layer2");result.terrain_blend_sharpness=ranged("terrain_blend_sharpness",.01,32,4);result.terrain_slope_start=ranged("terrain_slope_start",0,1,.35);result.terrain_slope_end=ranged("terrain_slope_end",0,1,.75);require(result.terrain_slope_start<=result.terrain_slope_end,"Material terrain slope range must be ordered");result.terrain_height_blend=ranged("terrain_height_blend",0,1,.5);}
    }
    std::set<std::string> named;if(result.texture!=AssetId{})named.insert("texture");if(result.normal_texture)named.insert("normal_texture");if(result.surface_texture)named.insert("surface_texture");if(result.emissive_texture)named.insert("emissive_texture");if(result.dissolve_texture)named.insert("dissolve_texture");if(result.terrain_layer1)named.insert("terrain_layer1");if(result.terrain_layer2)named.insert("terrain_layer2");
    for(const auto& [key,unused]:sources.items())require(named.contains(key),"Material source locators name its textures only");
    return result;
}
namespace assets_detail {
// A texture a material slot names: an imported texture of the project whose semantic is the one the slot requires, checked
// against its own import identity.
Import material_texture(const std::filesystem::path& root,const MaterialTextureRef& texture,TextureSemantic semantic,bool inspect){
    auto path=within(root,texture.source);auto extension=path.extension();require(extension==".png"||extension==".jpg"||extension==".jpeg","A material texture is an imported .png or .jpg");
    auto metadata_path=path;metadata_path+=".daimport";auto metadata=json(read(metadata_path,1024*1024),1024*1024);
    require(AssetId::parse(metadata.at("id").get<std::string>())==texture.id,"Material texture source identity");
    require(metadata.at("importer")==std::string(texture_importer(semantic)),"Material texture semantic mismatch: this slot needs a "+std::string(texture_semantic_label(semantic))+" texture");
    auto imported=import_texture(root,path,{{"$source",texture.id}},inspect,semantic);imported.inputs[metadata_path.lexically_relative(root).generic_string()]=sha256(read(metadata_path,1024*1024));return imported;
}
Import material_texture(const std::filesystem::path& root,const MaterialInstanceDefinition& material,bool inspect){return material_texture(root,{material.texture,material.texture_source},TextureSemantic::Color,inspect);}
Json material_product_fields(const MaterialInstanceDefinition& material){
    Json fields={{"family",material.family},{"color",material.color},{"roughness",material.roughness},{"metallic",material.metallic},{"double_sided",material.double_sided},{"texture",material.texture==AssetId{}?Json():Json(material.texture.text())}};
    if(material.family==material_family_lit_surface){
        if(material.normal_texture){fields["normal_texture"]=material.normal_texture.id.text();fields["normal_scale"]=material.normal_scale;}
        if(material.surface_texture){fields["surface_texture"]=material.surface_texture.id.text();fields["occlusion_strength"]=material.occlusion_strength;}
        if(material.emissive!=std::array<float,3>{0,0,0}||material.emissive_texture){fields["emissive"]=material.emissive;if(material.emissive_texture)fields["emissive_texture"]=material.emissive_texture.id.text();}
        if(material.alpha_mode!=AlphaMode::Opaque){fields["alpha_mode"]=alpha_mode_name(material.alpha_mode);fields["alpha_cutoff"]=material.alpha_cutoff;}}
    if(material.family==material_family_toon_standard||material.family==material_family_toon_character||material.family==material_family_toon_terrain){
        if((material.family==material_family_toon_standard||material.family==material_family_toon_character)&&material.normal_texture){fields["normal_texture"]=material.normal_texture.id.text();fields["normal_scale"]=material.normal_scale;}if((material.family==material_family_toon_standard||material.family==material_family_toon_character)&&material.surface_texture){fields["surface_texture"]=material.surface_texture.id.text();fields["occlusion_strength"]=material.occlusion_strength;}
        fields["toon_thresholds"]=material.toon_thresholds;fields["toon_softness"]=material.toon_softness;fields["shadow_tint"]=material.shadow_tint;fields["ambient_color"]=material.ambient_color;fields["ambient_strength"]=material.ambient_strength;fields["local_light_strength"]=material.local_light_strength;fields["triplanar"]=material.triplanar;fields["triplanar_scale"]=material.triplanar_scale;
        fields["emissive"]=material.emissive;if(material.emissive_texture)fields["emissive_texture"]=material.emissive_texture.id.text();if(material.alpha_mode!=AlphaMode::Opaque){fields["alpha_mode"]=alpha_mode_name(material.alpha_mode);fields["alpha_cutoff"]=material.alpha_cutoff;}
        if(material.family==material_family_toon_character){fields["hit_flash_color"]=material.hit_flash_color;fields["hit_flash_strength"]=material.hit_flash_strength;fields["status_color"]=material.status_color;fields["status_strength"]=material.status_strength;fields["dissolve"]=material.dissolve;fields["dissolve_edge"]=material.dissolve_edge;fields["dissolve_edge_width"]=material.dissolve_edge_width;if(material.dissolve_texture)fields["dissolve_texture"]=material.dissolve_texture.id.text();}
        if(material.family==material_family_toon_terrain){if(material.terrain_layer1)fields["terrain_layer1"]=material.terrain_layer1.id.text();if(material.terrain_layer2)fields["terrain_layer2"]=material.terrain_layer2.id.text();fields["terrain_blend_sharpness"]=material.terrain_blend_sharpness;fields["terrain_slope_start"]=material.terrain_slope_start;fields["terrain_slope_end"]=material.terrain_slope_end;fields["terrain_height_blend"]=material.terrain_height_blend;}}
    return fields;
}
void read_material_fields(CookedMaterial& material,const Json& fields){
    material.family=fields.value("family",std::string(material_family_toon_standard));material.color=fields.at("color").get<std::array<float,4>>();material.roughness=fields.at("roughness").get<float>();material.metallic=fields.at("metallic").get<float>();material.double_sided=fields.at("double_sided").get<bool>();
    if(fields.contains("texture")&&!fields.at("texture").is_null()){material.texture=AssetId::parse(fields.at("texture").get<std::string>());material.has_texture=true;}
    // Optional surface features (R5 A3); products written before them have none and draw exactly as before.
    auto finite=[](float v,float low,float high,const char* what){require(std::isfinite(v)&&v>=low&&v<=high,what);return v;};
    if(fields.contains("normal_texture")){material.normal_texture=AssetId::parse(fields.at("normal_texture").get<std::string>());material.has_normal=true;material.normal_scale=finite(fields.value("normal_scale",1.f),0,2,"Cooked normal scale");}
    if(fields.contains("surface_texture")){material.surface_texture=AssetId::parse(fields.at("surface_texture").get<std::string>());material.has_surface=true;material.occlusion_strength=finite(fields.value("occlusion_strength",1.f),0,1,"Cooked occlusion strength");}
    if(fields.contains("emissive")){material.emissive=fields.at("emissive").get<std::array<float,3>>();for(float e:material.emissive)finite(e,0,16,"Cooked emissive colour");}
    if(fields.contains("emissive_texture")){material.emissive_texture=AssetId::parse(fields.at("emissive_texture").get<std::string>());material.has_emissive_texture=true;}
    if(fields.contains("alpha_mode")){const auto mode=fields.at("alpha_mode").get<std::string>();require(mode=="cutout"||mode=="transparent","Cooked alpha mode");material.alpha_mode=mode=="cutout"?1:2;material.alpha_cutoff=finite(fields.value("alpha_cutoff",.5f),0,1,"Cooked alpha cutoff");}
    if(material.family==material_family_toon_standard||material.family==material_family_toon_character||material.family==material_family_toon_terrain){
        auto array=[&](const char* key,std::size_t count,float low,float high,auto& destination){if(!fields.contains(key))return;const auto& v=fields.at(key);require(v.is_array()&&v.size()==count,std::string("Cooked ")+key+" size");for(std::size_t i=0;i<count;++i)destination[i]=finite(v[i].get<float>(),low,high,key);};
        array("toon_thresholds",3,0,1,material.toon_thresholds);array("shadow_tint",3,0,1,material.shadow_tint);array("ambient_color",3,0,16,material.ambient_color);material.toon_softness=finite(fields.value("toon_softness",.035f),0,.5f,"Cooked toon softness");material.ambient_strength=finite(fields.value("ambient_strength",1.f),0,16,"Cooked ambient strength");material.local_light_strength=finite(fields.value("local_light_strength",1.f),0,16,"Cooked local light strength");material.triplanar=fields.value("triplanar",false);material.triplanar_scale=finite(fields.value("triplanar_scale",1.f),.001f,100,"Cooked triplanar scale");
        if(material.family==material_family_toon_character){array("hit_flash_color",4,0,16,material.hit_flash_color);array("status_color",4,0,16,material.status_color);material.hit_flash_strength=finite(fields.value("hit_flash_strength",0.f),0,16,"Cooked hit flash strength");material.status_strength=finite(fields.value("status_strength",0.f),0,16,"Cooked status strength");material.dissolve=finite(fields.value("dissolve",0.f),0,1,"Cooked dissolve");if(fields.contains("dissolve_texture")){material.dissolve_texture=AssetId::parse(fields.at("dissolve_texture").get<std::string>());material.has_dissolve_texture=true;}array("dissolve_edge",3,0,16,material.dissolve_edge);material.dissolve_edge_width=finite(fields.value("dissolve_edge_width",.05f),0,1,"Cooked dissolve edge width");}
        if(material.family==material_family_toon_terrain){if(fields.contains("terrain_layer1")){material.terrain_layer1=AssetId::parse(fields.at("terrain_layer1").get<std::string>());material.has_terrain_layer1=true;}if(fields.contains("terrain_layer2")){material.terrain_layer2=AssetId::parse(fields.at("terrain_layer2").get<std::string>());material.has_terrain_layer2=true;}material.terrain_blend_sharpness=finite(fields.value("terrain_blend_sharpness",4.f),.01f,32,"Cooked terrain blend sharpness");material.terrain_slope_start=finite(fields.value("terrain_slope_start",.35f),0,1,"Cooked terrain slope start");material.terrain_slope_end=finite(fields.value("terrain_slope_end",.75f),0,1,"Cooked terrain slope end");material.terrain_height_blend=finite(fields.value("terrain_height_blend",.5f),0,1,"Cooked terrain height blend");}
    }
}
std::vector<AssetId> material_textures(const CookedMaterial& material){std::vector<AssetId> result;if(material.has_texture)result.push_back(material.texture);if(material.has_normal)result.push_back(material.normal_texture);if(material.has_surface)result.push_back(material.surface_texture);if(material.has_emissive_texture)result.push_back(material.emissive_texture);if(material.has_dissolve_texture)result.push_back(material.dissolve_texture);if(material.has_terrain_layer1)result.push_back(material.terrain_layer1);if(material.has_terrain_layer2)result.push_back(material.terrain_layer2);return result;}
Import import_material(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,16384);auto material=decode_material_instance(bytes);if(!inspect)require(ids.size()==1&&ids.at("$source")==material.id,"Material source identity");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(material.id);result.product_count=1;
    // Standalone per-renderable bindings need the same frozen texture closure as model-level assignments.
    std::vector<AssetId> required;
    // Every slot is validated for its semantic; a texture used by two slots (for example base colour and emissive) is one product.
    for(const auto& [ref,semantic]:material.textures()){auto texture=material_texture(root,ref,semantic,inspect);for(const auto& [path,hash]:texture.inputs)result.inputs[path]=hash;if(std::find(required.begin(),required.end(),ref.id)!=required.end())continue;required.push_back(ref.id);
        result.product_count+=texture.product_count?texture.product_count:1;for(auto& product:texture.products)result.products.push_back(std::move(product));}
    if(!inspect)result.products.push_back({material.id,"material_instance","material-instance.json",json(bytes,16384).dump(),required});return result;
}
}
RuntimeMaterial load_cooked_material_instance(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    const auto held=registry_json(registry);const auto& manifest=*held;require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=runtime_package_product_limit,"Material registry contract");
    const Json* selected{};std::set<AssetId> unique;for(const auto& record:manifest.at("assets")){const auto asset=AssetId::parse(record.at("id").get<std::string>());require(unique.insert(asset).second,"Duplicate material registry identity");if(asset==id)selected=&record;}
    require(selected&&selected->at("kind")=="material_instance"&&selected->at("extension")=="material-instance.json","Material registry product type");const auto digest=selected->at("sha256").get<std::string>();require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==digest.npos,"Material product digest");
    const auto bytes=read(cas/(digest+".material-instance.json"),16384);require(sha256(bytes)==digest,"Material product hash mismatch");const auto definition=decode_material_instance(bytes);require(definition.id==id,"Material product identity mismatch");
    RuntimeMaterial result;auto& value=result.value;value.id=id;assets_detail::read_material_fields(value,assets_detail::material_product_fields(definition));
    std::string generation=digest;for(auto texture:assets_detail::material_textures(value)){if(std::any_of(result.textures.begin(),result.textures.end(),[&](const auto& loaded){return loaded.first==texture;}))continue;result.textures.emplace_back(texture,load_cooked_texture(registry,cas,texture));for(const auto& record:manifest.at("assets"))if(record.at("id")==texture.text())generation+=record.at("sha256").get<std::string>();}result.generation=sha256(generation);return result;
}
void validate_cooked_material_bindings(const std::filesystem::path& registry,const std::filesystem::path& cas,const SpawnPlan& plan){
    require(plan.materials.size()<=scene_object_limit,"Material binding object limit");std::map<AssetId,std::set<AssetId>> available;std::set<AssetId> loaded;
    for(const auto& [object,bindings]:plan.materials){require(plan.models.contains(object)&&bindings.size()<=128,"Material bindings require a model and bounded slots");const auto model=plan.models.at(object);
        if(!available.contains(model)){
            auto& slots=available[model];
            const auto manifest=registry_json(registry);
            const auto found=std::find_if(manifest->at("assets").begin(),manifest->at("assets").end(),[&](const auto& record){return record.at("id")==model.text();});
            require(found!=manifest->at("assets").end(),"Material binding model is missing from the cooked registry");
            if(found->at("kind")=="human-binding"){
                const auto cooked=load_cooked_skinned_model(registry,cas,model);
                for(const auto& mesh:cooked.meshes)slots.insert(mesh.id);
            }else{
                const auto cooked=load_cooked_model(registry,cas,model);
                for(const auto& material:cooked.materials)slots.insert(material.id);
            }
        }
        for(const auto& [slot,material]:bindings){require(available.at(model).contains(slot),"A material slot was removed; repair the preserved assignment before Play");if(loaded.insert(material).second)load_cooked_material_instance(registry,cas,material);}}
}

}
