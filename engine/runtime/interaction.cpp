#include <darkangel/interaction.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
}
void validate_interactable(const InteractableDefinition& definition){
    require(!definition.prompt.empty()&&definition.prompt.size()<=48,"An interactable's prompt is 1 to 48 characters");
    require(world_name_valid(definition.event)&&(definition.requires_tag.empty()||world_name_valid(definition.requires_tag)),"World event and tag names are lowercase letters, digits, '_' and '.', up to 48 characters");
    require(std::isfinite(definition.radius)&&definition.radius>=.5&&definition.radius<=10,"An interactable's radius is 0.5 to 10 m");
}
InteractableDefinition decode_interactable(std::string_view text){
    require(text.size()<=4096,"Interactable component size bound");const auto value=Json::parse(text);require(value.is_object(),"Interactable component type");
    for(const auto& [key,field]:value.items())require(key=="prompt"||key=="event"||key=="radius"||key=="once"||key=="requires_tag","Interactable component has an unknown field");
    require(value.contains("prompt")&&value.at("prompt").is_string()&&value.contains("event")&&value.at("event").is_string(),"An interactable needs a prompt and an event");
    InteractableDefinition result;result.prompt=value.at("prompt").get<std::string>();result.event=value.at("event").get<std::string>();
    if(value.contains("radius")){require(value.at("radius").is_number(),"Interactable radius type");result.radius=value.at("radius").get<double>();}
    if(value.contains("once")){require(value.at("once").is_boolean(),"Interactable once type");result.once=value.at("once").get<bool>();}
    if(value.contains("requires_tag")){require(value.at("requires_tag").is_string(),"Interactable required tag type");result.requires_tag=value.at("requires_tag").get<std::string>();require(!result.requires_tag.empty(),"An interactable's required tag is a name; leave the field out for none");}
    validate_interactable(result);return result;
}
const char* interaction_result_text(InteractionResult result){
    switch(result){case InteractionResult::Accepted:return "Accepted";case InteractionResult::Unknown:return "No such interactable";case InteractionResult::OutOfReach:return "Out of reach";case InteractionResult::Unavailable:return "Not available";case InteractionResult::Dead:return "The player is dead";case InteractionResult::UsedUp:return "Used up";}
    return "Unknown result";
}
WorldInteractable::WorldInteractable(InteractableDefinition definition,MotorVec position):definition_(std::move(definition)),position_(position){
    validate_interactable(definition_);require(std::isfinite(position_.x)&&std::isfinite(position_.y)&&std::isfinite(position_.z),"Interactable position");
}
bool WorldInteractable::reaches(MotorVec player)const{
    const double dx=player.x-position_.x,dy=player.y-position_.y,dz=player.z-position_.z;return dx*dx+dy*dy+dz*dz<=definition_.radius*definition_.radius;
}
InteractionResult WorldInteractable::use(WorldEvents& world,MotorVec player,bool player_alive){
    if(!player_alive)return InteractionResult::Dead;if(!reaches(player))return InteractionResult::OutOfReach;
    if((definition_.once&&used_)||(!definition_.requires_tag.empty()&&!world.tag(definition_.requires_tag)))return InteractionResult::Unavailable;
    world.emit(definition_.event);++used_;return InteractionResult::Accepted;
}
}
