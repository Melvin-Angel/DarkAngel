#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/combat_kit.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Native behaviour tree topology, authored as JSON. Node kinds are a closed native set:
//   composites  sequence, selector (one to eight children)
//   conditions  opponent_alive, opponent_visible, in_range {range}, idle
//   actions     face, approach {range}, attack {slot}, recover {ticks}, investigate, script {name, source}
// script is a leaf written in Luau, inline in the tree: the chunk returns function(view, state), which returns a status
// ("success", "failure", "running") and optionally a command table (move_x, move_z, face, press, say). It decides
// through the same seam as the native leaves and runs in a sandbox on authority; a fault fails the leaf, never the session.
// investigate walks to the last known position of the hostile the NPC lost most recently and
// succeeds on arrival; it fails when the NPC remembers none. No owned tree uses it yet.
// approach walks until the opponent is within range and in line of sight.
// The tree only decides; every leaf acts through the controller seam (move, face, press),
// so a behaviour cannot bypass ability validation. Blackboard and sensing stay native: the
// opponent is the nearest live hostile the NPC's senses detect, and opponent_visible adds
// line of sight to it.
// preferred: the range comes from the NPC's tuning ("range": "preferred") instead of the tree.
struct BehaviorNode {std::string type;double range{};bool preferred{};unsigned ticks{};CombatSlot slot{};std::vector<BehaviorNode> children;std::string name,source;};
// Bounds of a script leaf: its display name, its source, and how many one tree may hold.
inline constexpr std::size_t behavior_script_name_limit=48,behavior_script_source_limit=4096,behavior_script_limit=8;
// Compiles a script leaf's Luau source to bytecode. Throws with the compiler's message when it does not compile.
std::string compile_behavior_script(std::string_view source);
// The slot a behaviour names ("light", "heavy", ...), for trees and script leaves alike. Throws on an unknown name.
CombatSlot behavior_slot(std::string_view name);
// Per-NPC numbers a tree reads, so one tree serves differently tuned NPCs: the range that
// "preferred" nodes use, and a multiplier on every recover node's ticks.
struct BehaviorTuning {double preferred_range{1.4},recovery_scale{1};};
void validate_behavior_tuning(const BehaviorTuning&);
struct BehaviorDefinition {AssetId id;std::string name,generation;BehaviorNode root;};
// Validates shape and bounds (depth 8, 64 nodes, known kinds and parameters). The cooked
// product and the source share this decoder; generation is filled by the loader.
BehaviorDefinition decode_behavior(std::string_view json);
// The first authored tree: attack an opponent in reach then recover, otherwise approach it.
std::string default_behavior_tree_json();
}
