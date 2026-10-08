#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
namespace darkangel {
struct CollisionDefinition {AssetId id;unsigned schema{};std::vector<CollisionBox> boxes;};
// Source validation is shared by native authoring and cooking, without Jolt.
CollisionDefinition decode_collision_source(std::string_view);
}
