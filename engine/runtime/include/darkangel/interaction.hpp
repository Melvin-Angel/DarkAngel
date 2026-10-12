#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/world_events.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace darkangel {
// An interactable is authored on a scene object: something the player uses on purpose. A
// successful use emits a world event; what happens next is whatever listens for it. It can be
// used once or every time, and it can require a world tag (a lever that is locked until an
// encounter is over). Server-only state: a client asks, authority decides from its own state.
//   prompt        what the HUD offers, 1 to 48 characters
//   event         the world event a successful use emits
//   radius        how near the player's position must be, 0.5 to 10 m
//   once          used up after the first success
//   requires_tag  a world tag that must be held; empty for none
struct InteractableDefinition {std::string prompt,event,requires_tag;double radius{2};bool once{true};};
void validate_interactable(const InteractableDefinition&);
// Decodes and validates the "interactable" component of a scene object.
InteractableDefinition decode_interactable(std::string_view json);
// How authority answered one request.
enum class InteractionResult:std::uint8_t{Accepted,Unknown,OutOfReach,Unavailable,Dead};
const char* interaction_result_text(InteractionResult);
class WorldInteractable {
public:
    WorldInteractable(InteractableDefinition,MotorVec position);
    const InteractableDefinition& definition()const{return definition_;}
    MotorVec position()const{return position_;}
    // The player's position is within the radius.
    bool reaches(MotorVec player)const;
    // One request from a player, resolved from authoritative state: emits the event when the player is alive
    // and in reach, the interactable is not used up and its required tag is held.
    InteractionResult use(WorldEvents& world,MotorVec player,bool player_alive);
    unsigned used()const{return used_;}
private:InteractableDefinition definition_;MotorVec position_{};unsigned used_{};
};
}
