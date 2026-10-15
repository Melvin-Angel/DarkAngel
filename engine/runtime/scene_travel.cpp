#include <darkangel/json_fields.hpp>
#include <darkangel/check.hpp>
#include <darkangel/scene_travel.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
using darkangel::require;
}
EntryDefinition decode_entry(std::string_view text){
    require(text.size()<=4096,"Entry component size bound");const auto value=Json::parse(text);require(value.is_object()&&value.size()==1&&value.contains("name")&&value.at("name").is_string(),"A scene entry point has a name and nothing else");
    EntryDefinition entry;entry.name=value.at("name").get<std::string>();require(!entry.name.empty()&&entry.name.size()<=48,"A scene entry point's name is 1 to 48 characters");return entry;
}
void validate_transition(const TransitionDefinition& transition){
    require(transition.scene!=AssetId{},"A scene transition names its destination scene");
    require(transition.activation==TransitionActivation::Enter||transition.activation==TransitionActivation::Interact,"Scene transition activation");
    require(transition.activation==TransitionActivation::Enter?transition.prompt.empty():(!transition.prompt.empty()&&transition.prompt.size()<=48),"A scene transition used on purpose has a prompt of 1 to 48 characters; one entered by walking in has none");
    require(std::isfinite(transition.radius)&&transition.radius>=.5&&transition.radius<=10,"A scene transition's radius is 0.5 to 10 m");
    require(transition.half.x>0&&transition.half.y>0&&transition.half.z>0&&transition.half.x<=200&&transition.half.y<=200&&transition.half.z<=200,"Scene transition half extents are above 0, up to 200 m");
}
TransitionDefinition decode_transition(std::string_view text){
    require(text.size()<=4096,"Transition component size bound");const auto value=Json::parse(text);require(value.is_object(),"Transition component type");
    json_fields::known(value,{"scene","entry","activation","prompt","radius","half"},"Transition component has an unknown field");
    require(value.contains("scene")&&value.at("scene").is_string()&&value.contains("activation")&&value.at("activation").is_string()&&value.contains("half"),"A scene transition needs a scene, an activation and half extents");
    TransitionDefinition transition;transition.scene=AssetId::parse(value.at("scene").get<std::string>());
    if(value.contains("entry")){require(value.at("entry").is_string(),"Scene transition entry type");transition.entry=StableId::parse(value.at("entry").get<std::string>());require(bool(transition.entry),"A scene transition's entry is an entry point's identity; leave the field out for the scene's own start");}
    const auto activation=value.at("activation").get<std::string>();require(activation=="enter"||activation=="interact","A scene transition is activated by \"enter\" or \"interact\"");transition.activation=activation=="enter"?TransitionActivation::Enter:TransitionActivation::Interact;
    if(value.contains("prompt")){require(value.at("prompt").is_string(),"Scene transition prompt type");transition.prompt=value.at("prompt").get<std::string>();}
    if(value.contains("radius")){require(value.at("radius").is_number(),"Scene transition radius type");transition.radius=value.at("radius").get<double>();}
    const auto& half=value.at("half");require(half.is_array()&&half.size()==3&&half[0].is_number()&&half[1].is_number()&&half[2].is_number(),"Scene transition half extents are three numbers");transition.half={half[0].get<double>(),half[1].get<double>(),half[2].get<double>()};
    validate_transition(transition);return transition;
}
}
