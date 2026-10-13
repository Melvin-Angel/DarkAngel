#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/world.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace darkangel {
// A Scene Entry Point is authored on a scene object: a place the party can arrive at, with the object's position and
// facing. Its destination identifier is the object's stable identity; the name is a label for pickers and logs and
// need not be unique.
//   name  1 to 48 characters
struct EntryDefinition {std::string name;};
// Decodes and validates the "entry" component of a scene object.
EntryDefinition decode_entry(std::string_view json);
// A Scene Transition Trigger is authored on a scene object: a box that sends the whole party to another scene.
//   scene       the destination scene asset
//   entry       the entry point's stable identity in that scene; left out, the scene's own player start
//   activation  "enter": any living party member walking into the box; "interact": a member using it on purpose
//   prompt      what the HUD offers for "interact", 1 to 48 characters
//   radius      how near a member must be to use it, 0.5 to 10 m
//   half        the box's half extents, each above 0 and at most 200 m
// Server-only: a client can only ask to interact; authority decides, and the request it raises names no player.
enum class TransitionActivation:std::uint8_t{Enter,Interact};
struct TransitionDefinition {AssetId scene;StableId entry;TransitionActivation activation{};std::string prompt;double radius{2};MotorVec half{};};
void validate_transition(const TransitionDefinition&);
// Decodes and validates the "transition" component of a scene object.
TransitionDefinition decode_transition(std::string_view json);
}
