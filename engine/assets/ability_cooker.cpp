#include "assets_internal.hpp"
#include <algorithm>
#include <darkangel/ability_assets.hpp>
#include <darkangel/hash.hpp>
#include <cmath>
#include <set>

namespace darkangel {
using namespace assets_detail;
namespace {
void fields(const Json& value,std::initializer_list<const char*> names){
    require(value.is_object()&&value.size()==names.size(),"Combat asset field count");
    for(auto name:names)require(value.contains(name),"Combat asset missing/unknown field");
}
std::uint64_t integer(const Json& value,std::uint64_t maximum){
    require(value.is_number_unsigned()&&value.get<std::uint64_t>()<=maximum,"Combat asset integer range/type");return value.get<std::uint64_t>();
}
AttributeAsset attributes(const Json& source){
    fields(source,{"schema","kind","asset","attributes"});require(source.at("schema")==1&&source.at("kind")=="attributes","Attribute asset schema/type");
    AttributeAsset result;result.id=AssetId::parse(source.at("asset").get<std::string>());result.generation=sha256(source.dump());
    auto& entries=source.at("attributes");require(entries.is_array()&&!entries.empty()&&entries.size()<=64,"Attribute asset count");
    for(const auto& entry:entries){
        fields(entry,{"id","name","kind","base","minimum","maximum","maximum_attribute","unit","visibility"});
        AttributeAssetField field;auto& d=field.definition;d.id=static_cast<AttributeId>(integer(entry.at("id"),UINT32_MAX));d.name=entry.at("name").get<std::string>();
        auto kind=entry.at("kind").get<std::string>();require(kind=="resource"||kind=="statistic","Attribute kind");d.kind=kind=="resource"?AttributeKind::Resource:AttributeKind::Statistic;
        for(auto key:{"base","minimum","maximum"})require(entry.at(key).is_number(),"Attribute scalar type");
        d.base=entry.at("base").get<double>();d.minimum=entry.at("minimum").get<double>();d.maximum=entry.at("maximum").get<double>();d.maximum_attribute=static_cast<AttributeId>(integer(entry.at("maximum_attribute"),UINT32_MAX));
        field.unit=entry.at("unit").get<std::string>();require(!field.unit.empty()&&field.unit.size()<=32,"Attribute unit");
        auto visibility=entry.at("visibility").get<std::string>();require(visibility=="server"||visibility=="owner"||visibility=="public","Attribute visibility");
        field.visibility=visibility=="public"?AttributeVisibility::Public:visibility=="owner"?AttributeVisibility::Owner:AttributeVisibility::Server;d.visibility=field.visibility;result.fields.push_back(std::move(field));
    }
    AttributeSet checked(result.definitions());return result;
}
AssetId reference(const Json& value){return AssetId::parse(value.get<std::string>());}
std::shared_ptr<const AbilityDefinition> ability(const Json& source,const AttributeAsset& schema,const std::shared_ptr<const ActionDefinition>& action,const TagAsset* tags=nullptr){
    auto base=source;base.erase("melee");base.erase("tags");base.erase("requirements");fields(base,{"schema","kind","asset","action","attributes","costs","cooldown_group","cooldown_ticks","activate_on","minimum_held_us","cancel_on_release","interruptible"});
    require(source.contains("tags")==source.contains("requirements")&&source.contains("tags")==bool(tags),"Ability tag binding/requirements pair");
    require(source.at("schema")==1&&source.at("kind")=="ability","Ability asset schema/type");
    require(reference(source.at("action"))==action->id&&reference(source.at("attributes"))==schema.id,"Ability dependency identity");
    AbilityDefinition result;result.id=reference(source.at("asset"));result.action=action;auto& costs=source.at("costs");require(costs.is_array()&&costs.size()<=8,"Ability asset cost count");
    for(const auto& cost:costs){fields(cost,{"attribute","amount"});require(cost.at("amount").is_number(),"Ability amount type");result.costs.push_back({static_cast<AttributeId>(integer(cost.at("attribute"),UINT32_MAX)),cost.at("amount").get<double>()});}
    result.cooldown_group=static_cast<std::uint32_t>(integer(source.at("cooldown_group"),UINT32_MAX));result.cooldown_ticks=integer(source.at("cooldown_ticks"),36000);result.minimum_held_us=integer(source.at("minimum_held_us"),10000000);
    auto edge=source.at("activate_on").get<std::string>();require(edge=="pressed"||edge=="hold"||edge=="released"||edge=="tapped","Ability input edge");result.activate_on=edge=="pressed"?InputEdge::Pressed:edge=="hold"?InputEdge::Hold:edge=="released"?InputEdge::Released:InputEdge::Tapped;
    result.cancel_on_release=source.at("cancel_on_release").get<bool>();result.interruptible=source.at("interruptible").get<bool>();
    if(tags){require(reference(source.at("tags"))==tags->id,"Ability tag registry dependency");result.tag_registry=tags->id;result.tag_generation=tags->generation;auto& requirements=source.at("requirements");fields(requirements,{"all","any","none"});auto list=[&](const char* name){auto& values=requirements.at(name);require(values.is_array()&&values.size()<=16,"Ability tag requirement count");std::vector<TagId> ids;for(auto value:values)ids.push_back(static_cast<TagId>(integer(value,UINT32_MAX)));return ids;};result.requirements={list("all"),list("any"),list("none")};}
    if(source.contains("melee")){
        require(source.at("melee").is_array()&&source.at("melee").size()<=8,"Ability melee source count");
        for(const auto& entry:source.at("melee")){
            fields(entry,{"block","offset","radius","power","evaluator","damage_type"});AbilityMelee profile;
            profile.block=static_cast<unsigned>(integer(entry.at("block"),UINT32_MAX));profile.evaluator=static_cast<std::uint32_t>(integer(entry.at("evaluator"),UINT32_MAX));profile.damage_type=static_cast<std::uint32_t>(integer(entry.at("damage_type"),UINT32_MAX));
            require(entry.at("radius").is_number()&&entry.at("power").is_number(),"Melee scalar type");profile.radius=entry.at("radius").get<double>();profile.power=entry.at("power").get<double>();
            profile.offset=entry.at("offset").get<std::array<double,3>>();result.melee.push_back(profile);
        }
    }
    // Include gameplay dependency generations, not just the parent source hash.
    result.generation=sha256(source.dump()+schema.generation+action->generation+(tags?tags->generation:""));
    auto dictionary=tags?tags->dictionary():nullptr;return freeze_ability_definition(result,AttributeSet(schema.definitions()),dictionary.get());
}
struct Registry {
    std::map<AssetId,Json> records;std::filesystem::path cas;
    Registry(const std::filesystem::path& path,const std::filesystem::path& storage):cas(storage){
        auto manifest=json(read(path,1024*1024));require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&!manifest.at("assets").empty()&&manifest.at("assets").size()<=512,"Combat registry schema/limit");
        for(const auto& record:manifest.at("assets"))require(records.emplace(reference(record.at("id")),record).second,"Duplicate combat registry identity");
        require(records.contains(reference(manifest.at("root"))),"Combat registry root missing");
    }
    Json load(AssetId id,const char* kind,const char* extension)const{
        auto found=records.find(id);require(found!=records.end(),"Missing combat dependency");auto& record=found->second;
        require(record.at("kind")==kind&&record.at("extension")==extension,"Combat dependency product type");auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Combat product digest");
        auto bytes=read(cas/(hash+"."+extension),65536);require(sha256(bytes)==hash,"Combat product hash mismatch");auto data=json(bytes,65536);require(reference(data.at("asset"))==id,"Combat product identity mismatch");return data;
    }
};
}
std::vector<AttributeDefinition> AttributeAsset::definitions()const{std::vector<AttributeDefinition> result;for(const auto& field:fields){auto definition=field.definition;definition.visibility=field.visibility;result.push_back(std::move(definition));}return result;}
AttributeAsset load_cooked_attributes(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){return attributes(Registry(registry,cas).load(id,"attributes","attributes.json"));}
AttributeAsset decode_attribute_asset(std::string_view bytes){return attributes(json(bytes,65536));}
CookedAbility load_cooked_ability(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    Registry records(registry,cas);auto source=records.load(id,"ability","ability.json");auto schema=attributes(records.load(reference(source.at("attributes")),"attributes","attributes.json"));
    auto action=std::make_shared<const ActionDefinition>(load_cooked_action(registry,cas,reference(source.at("action"))));
    require(source.at("action_generation")==action->generation&&source.at("attributes_generation")==schema.generation,"Combat frozen dependency generation mismatch");
    source.erase("action_generation");source.erase("attributes_generation");
    std::optional<TagAsset> tags;if(source.contains("tags")){tags=load_cooked_tags(registry,cas,reference(source.at("tags")));require(source.at("tags_generation")==tags->generation,"Ability frozen tag generation mismatch");source.erase("tags_generation");}else require(!source.contains("tags_generation"),"Unexpected ability tag generation");
    return {ability(source,schema,action,tags?&*tags:nullptr),std::move(schema),std::move(tags)};
}
namespace assets_detail {
Import import_ability(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,65536);auto data=json(bytes,65536);Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);auto id=reference(data.at("asset"));result.identities.push_back(id);
    if(!inspect)require(ids.size()==1&&ids.at("$source")==id,"Combat source UUID mismatch");
    if(source.extension()==".daattributes"){
        attributes(data);result.product_count=1;if(!inspect)result.products.push_back({id,"attributes","attributes.json",data.dump(),{}});return result;
    }
    // Source-only locators accompany typed UUIDs; the cooked parent strips paths.
    require(data.contains("sources"),"Ability source dependency locators");auto locations=data.at("sources");if(data.contains("tags"))fields(locations,{"action","attributes","tags"});else fields(locations,{"action","attributes"});data.erase("sources");
    auto action_path=within(root,locations.at("action").get<std::string>()),schema_path=within(root,locations.at("attributes").get<std::string>());
    require(action_path.extension()==".daaction"&&schema_path.extension()==".daattributes","Ability source dependency type");
    auto action_bytes=read(action_path,65536),schema_bytes=read(schema_path,65536);
    auto action_import=import_action(root,action_path,{{"$source",reference(data.at("action"))}},inspect);
    auto action_data=json(action_bytes);if(action_data.at("schema")==2&&inspect){action_data.erase("motion");action_data["schema"]=1;}
    if(!inspect)for(const auto& product:action_import.products)if(product.id==reference(data.at("action")))action_data=json(product.bytes);
    auto action=std::make_shared<const ActionDefinition>(decode_action_source(action_data.dump()));auto schema=attributes(json(schema_bytes,65536));std::optional<TagAsset> tags;Import tag_import;if(data.contains("tags")){auto path=within(root,locations.at("tags").get<std::string>());require(path.extension()==".datags","Ability tag locator type");tags=decode_tag_asset(read(path,65536));require(tags->id==reference(data.at("tags")),"Ability tag locator identity");tag_import=import_effect(root,path,{{"$source",tags->id}},inspect);}ability(data,schema,action,tags?&*tags:nullptr);
    require(id!=action->id&&id!=schema.id&&schema.id!=action->id,"Combat closure duplicate UUID");
    result.inputs[action_path.lexically_relative(root).generic_string()]=sha256(action_bytes);result.inputs[schema_path.lexically_relative(root).generic_string()]=sha256(schema_bytes);result.inputs.insert(action_import.inputs.begin(),action_import.inputs.end());result.product_count=action_import.product_count+2;result.identities.insert(result.identities.end(),action_import.identities.begin(),action_import.identities.end());result.identities.push_back(schema.id);
    if(tags){require(tags->id!=id&&tags->id!=schema.id&&std::find(action_import.identities.begin(),action_import.identities.end(),tags->id)==action_import.identities.end(),"Ability tag closure identity conflict");result.product_count++;result.identities.push_back(tags->id);result.inputs.insert(tag_import.inputs.begin(),tag_import.inputs.end());data["tags_generation"]=tags->generation;}
    data["action_generation"]=action->generation;data["attributes_generation"]=schema.generation;
    if(!inspect){std::vector<AssetId> required{action->id,schema.id};if(tags)required.push_back(tags->id);result.products.push_back({id,"ability","ability.json",data.dump(),std::move(required)});for(auto& product:action_import.products){require(product.id!=id&&product.id!=schema.id,"Ability action closure identity conflict");result.products.push_back(std::move(product));}result.products.push_back({schema.id,"attributes","attributes.json",json(schema_bytes).dump(),{}});for(auto& product:tag_import.products)result.products.push_back(std::move(product));}
    return result;
}
}
}
