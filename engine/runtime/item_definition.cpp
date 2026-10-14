#include <darkangel/inventory.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace darkangel {
namespace {
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
AssetId reference(const nlohmann::json& value){return value.is_null()?AssetId{}:AssetId::parse(value.get<std::string>());}
nlohmann::json reference(AssetId value){return value==AssetId{}?nlohmann::json(nullptr):nlohmann::json(value.text());}
}
ItemDefinition decode_item_definition(std::string_view text){
    require(text.size()<=16384,"Item source size bound");
    const auto data=nlohmann::json::parse(text,[](int depth,nlohmann::json::parse_event_t,nlohmann::json&){require(depth<=4,"Item source depth bound");return true;});
    require(data.is_object()&&data.size()==13+unsigned(data.contains("sources"))&&data.at("schema")==1&&data.at("kind")=="item","Item source schema/shape");
    if(data.contains("sources"))require(data.at("sources").is_object()&&data.at("sources").size()<=3,"Item source locator bound");
    require(data.at("stack_limit").is_number_unsigned(),"Item stack limit must be unsigned");
    const auto stack=data.at("stack_limit").get<std::uint64_t>();require(stack&&stack<=1000000,"Item stack limit bound");
    ItemDefinition result;result.id=AssetId::parse(data.at("asset").get<std::string>());result.name=data.at("name").get<std::string>();result.description=data.at("description").get<std::string>();result.classification=data.at("classification").get<std::string>();
    result.icon=reference(data.at("icon"));result.model=reference(data.at("model"));result.stack_limit=unsigned(stack);result.unique=data.at("unique").get<bool>();result.usable=data.at("usable").get<bool>();result.equippable=data.at("equippable").get<bool>();
    const auto& properties=data.at("properties");require(properties.is_object(),"Item classification properties must be an object");
    if(result.classification=="mask"){require(properties.size()==1,"Mask item property shape");result.mask=reference(properties.at("mask"));}
    else require(properties.empty(),"Unsupported item classification properties");
    // The catalog owns registered classification capability validation. The codec
    // checks universal bounds without preventing a caller from registering a new kind.
    require(result.id!=AssetId{}&&!result.name.empty()&&result.name.size()<=128&&result.description.size()<=4096&&!result.classification.empty()&&result.classification.size()<=32,"Item identity/text bound");
    require(!result.unique||result.stack_limit==1,"Unique item stack limit");
    require(result.classification=="mask"?(result.mask!=AssetId{}&&result.equippable&&result.stack_limit==1):(result.mask==AssetId{}),"Item mask reference rule");return result;
}
std::string encode_item_definition(const ItemDefinition& value){
    nlohmann::json properties=nlohmann::json::object();if(value.classification=="mask")properties["mask"]=reference(value.mask);
    auto result=nlohmann::json{{"schema",1},{"kind","item"},{"asset",value.id.text()},{"name",value.name},{"description",value.description},{"classification",value.classification},{"icon",reference(value.icon)},{"model",reference(value.model)},{"stack_limit",value.stack_limit},{"unique",value.unique},{"usable",value.usable},{"equippable",value.equippable},{"properties",properties}}.dump();
    decode_item_definition(result);return result;
}
}
