#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/combat_kit.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Native behaviour tree topology, authored as JSON. Node kinds are a closed native set:
//   composites  sequence, selector (one to eight children)
//   conditions  opponent_alive, in_range {range}, idle
//   actions     face, approach {range}, attack {slot}, recover {ticks}
// The tree only decides; every leaf acts through the controller seam (move, face, press),
// so a behaviour cannot bypass ability validation. Blackboard and sensing stay native.
struct BehaviorNode {std::string type;double range{};unsigned ticks{};CombatSlot slot{};std::vector<BehaviorNode> children;};
struct BehaviorDefinition {AssetId id;std::string name,generation;BehaviorNode root;};
// Validates shape and bounds (depth 8, 64 nodes, known kinds and parameters). The cooked
// product and the source share this decoder; generation is filled by the loader.
BehaviorDefinition decode_behavior(std::string_view json);
// The first authored tree: attack an opponent in reach then recover, otherwise approach it.
std::string default_behavior_tree_json();
}
