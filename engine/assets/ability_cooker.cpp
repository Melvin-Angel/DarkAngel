#include "assets_internal.hpp"
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
        field.visibility=visibility=="public"?AttributeVisibility::Public:visibility=="owner"?AttributeVisibility::Owner:AttributeVisibility::Server;result.fields.push_back(std::move(field));
    }
    AttributeSet checked(result.definitions());return result;
}
AssetId reference(const Json& value){return AssetId::parse(value.get<std::string>());}
std::shared_ptr<const AbilityDefinition> ability(const Json& source,const AttributeAsset& schema,const std::shared_ptr<const ActionDefinition>& action){
    fields(source,{"schema","kind","asset","action","attributes","costs","cooldown_group","cooldown_ticks","activate_on","minimum_held_us","cancel_on_release","interruptible"});
    require(source.at("schema")==1&&source.at("kind")=="ability","Ability asset schema/type");
    require(reference(source.at("action"))==action->id&&reference(source.at("attributes"))==schema.id,"Ability dependency identity");
    AbilityDefinition result;result.id=reference(source.at("asset"));result.action=action;auto& costs=source.at("costs");require(costs.is_array()&&costs.size()<=8,"Ability asset cost count");
    for(const auto& cost:costs){fields(cost,{"attribute","amount"});require(cost.at("amount").is_number(),"Ability amount type");result.costs.push_back({static_cast<AttributeId>(integer(cost.at("attribute"),UINT32_MAX)),cost.at("amount").get<double>()});}
    result.cooldown_group=static_cast<std::uint32_t>(integer(source.at("cooldown_group"),UINT32_MAX));result.cooldown_ticks=integer(source.at("cooldown_ticks"),36000);result.minimum_held_us=integer(source.at("minimum_held_us"),10000000);
    auto edge=source.at("activate_on").get<std::string>();require(edge=="pressed"||edge=="hold"||edge=="released"||edge=="tapped","Ability input edge");result.activate_on=edge=="pressed"?InputEdge::Pressed:edge=="hold"?InputEdge::Hold:edge=="released"?InputEdge::Released:InputEdge::Tapped;
    result.cancel_on_release=source.at("cancel_on_release").get<bool>();result.interruptible=source.at("interruptible").get<bool>();
    // Include gameplay dependency generations, not just the parent source hash.
    result.generation=sha256(source.dump()+schema.generation+action->generation);
    return freeze_ability_definition(result,AttributeSet(schema.definitions()));
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
std::vector<AttributeDefinition> AttributeAsset::definitions()const{std::vector<AttributeDefinition> result;for(const auto& field:fields)result.push_back(field.definition);return result;}
AttributeAsset load_cooked_attributes(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){return attributes(Registry(registry,cas).load(id,"attributes","attributes.json"));}
CookedAbility load_cooked_ability(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    Registry records(registry,cas);auto source=records.load(id,"ability","ability.json");auto schema=attributes(records.load(reference(source.at("attributes")),"attributes","attributes.json"));
    auto action=std::make_shared<const ActionDefinition>(decode_action_source(records.load(reference(source.at("action")),"action","action.json").dump()));
    require(source.at("action_generation")==action->generation&&source.at("attributes_generation")==schema.generation,"Combat frozen dependency generation mismatch");
    source.erase("action_generation");source.erase("attributes_generation");
    return {ability(source,schema,action),std::move(schema)};
}
namespace assets_detail {
Import import_ability(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,65536);auto data=json(bytes,65536);Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);auto id=reference(data.at("asset"));
    if(!inspect)require(ids.size()==1&&ids.at("$source")==id,"Combat source UUID mismatch");
    if(source.extension()==".daattributes"){
        attributes(data);result.product_count=1;if(!inspect)result.products.push_back({id,"attributes","attributes.json",data.dump(),{}});return result;
    }
    // Source-only locators accompany typed UUIDs; the cooked parent strips paths.
    require(data.contains("sources"),"Ability source dependency locators");auto locations=data.at("sources");fields(locations,{"action","attributes"});data.erase("sources");
    auto action_path=within(root,locations.at("action").get<std::string>()),schema_path=within(root,locations.at("attributes").get<std::string>());
    require(action_path.extension()==".daaction"&&schema_path.extension()==".daattributes","Ability source dependency type");
    auto action_bytes=read(action_path,65536),schema_bytes=read(schema_path,65536);
    auto action=std::make_shared<const ActionDefinition>(decode_action_source(action_bytes));auto schema=attributes(json(schema_bytes,65536));ability(data,schema,action);
    require(id!=action->id&&id!=schema.id&&schema.id!=action->id,"Combat closure duplicate UUID");
    result.inputs[action_path.lexically_relative(root).generic_string()]=sha256(action_bytes);result.inputs[schema_path.lexically_relative(root).generic_string()]=sha256(schema_bytes);result.product_count=3;
    data["action_generation"]=action->generation;data["attributes_generation"]=schema.generation;
    if(!inspect){result.products.push_back({id,"ability","ability.json",data.dump(),{action->id,schema.id}});result.products.push_back({action->id,"action","action.json",json(action_bytes).dump(),{}});result.products.push_back({schema.id,"attributes","attributes.json",json(schema_bytes).dump(),{}});}
    return result;
}
}
}
