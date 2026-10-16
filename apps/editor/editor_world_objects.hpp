#pragma once
#include "editor_ui.hpp"
#include <nlohmann/json.hpp>

namespace darkangel::editor_app {
// Scene-edit primitives of the Scene tab (defined in editor_ui.cpp): one structural edit of the scene's own source, one
// Undo step, saved with the scene.
StableId scene_add_object(Controller&,nlohmann::json entity,const Transform&,const char* message);
void scene_set_component(Controller&,StableId,const char* key,nlohmann::json value,const char* message);
std::vector<Shell::TravelScene> scene_travel_list(const Controller&);
// Inspector sections for the M8 world objects of the selected object (pickup, altar, door or platform). True when an
// edit was committed this frame, so the caller stops drawing the now stale plan.
bool draw_world_object_components(Controller&,StableId object);
// Placeable world objects. Gameplay place-item numbers: 9 pickup, 10 altar, 11 door or gate, 12 movable platform.
constexpr int place_pickup=9,place_altar=10,place_door=11,place_platform=12;
StableId place_world_object(Controller&,int gameplay,const Transform& at);
// R4: Place item for a saved prefab, and placing one as a linked instance (its first member is selected and returned).
constexpr int place_prefab_item=20;
StableId place_prefab_instance(Controller&,AssetId prefab,const Transform& at);
// Defaults of the world-object components an instance or a prefab can add (the same values the Place items use).
nlohmann::json world_object_defaults(Controller&,const std::string& component);
// The Encounter section's persistence: when a completed encounter comes back (a boss: never).
bool encounter_policy_field(nlohmann::json& encounter);
}
