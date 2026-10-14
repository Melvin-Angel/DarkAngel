#pragma once
#include "../editor_ui.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <string>

namespace darkangel::editor_app {
// Authoring automation for gates: places the plan's world objects (pickups, altars, doors, platforms, entries,
// transitions) through the Scene tab's own Place and Inspector edit paths, then saves the scene. Returns the placed
// objects by their plan keys.
std::map<std::string, StableId> place_world_plan(Controller&, Shell&, const nlohmann::json& plan);
} // namespace darkangel::editor_app
