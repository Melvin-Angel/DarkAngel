#pragma once
#include "render_settings.hpp"
#include <string>

namespace darkangel::editor_app::render {
// Rendering and lights are native optional components, unlike the top-level
// gameplay roles. Writing a sibling field silently leaves the resolved world
// (the Lighting tab's next-frame input) unchanged.
inline void set_render_component(nlohmann::json& entity, const char* key, nlohmann::json value) {
    entity.erase(key); // Retire ignored fields left by older editor versions.
    if (value.is_null()) {
        if (entity.contains("optional")) {
            entity["optional"].erase(key);
            if (entity["optional"].empty()) entity.erase("optional");
        }
    } else {
        if (!entity.contains("optional")) entity["optional"] = nlohmann::json::object();
        entity["optional"][key] = std::move(value);
    }
}
inline ProjectRenderingSettings scene_as_project_defaults(ProjectRenderingSettings defaults,
                                                          const SceneRenderingSettings& scene) {
    defaults.scene = scene;
    defaults.scene.main_light = {}; // Object identities cannot cross scenes.
    defaults.material_toon = scene.toon;
    return defaults; // Preserve project tint/roughness/metallic and their opt-in.
}
}
