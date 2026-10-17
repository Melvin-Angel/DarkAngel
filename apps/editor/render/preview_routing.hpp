#pragma once

namespace darkangel::editor_app::render {

// Preview content is an isolated render route.  It may use the editor's display
// texture, but it must never inherit the scene's object list or gameplay camera.
struct PreviewRouting {
    static bool isolated_content(bool composer, bool character, bool graph, bool gameplay_active) {
        return !gameplay_active && (composer || character || graph);
    }

    static bool gameplay_camera_is_primary(bool gameplay_active, bool has_possessed_player) {
        return gameplay_active && has_possessed_player;
    }
};

} // namespace darkangel::editor_app::render
