#pragma once
#include "exercise.hpp"
#include <cstddef>
#include <memory>
#include <string>

namespace darkangel::editor_app {
class Controller;
class Shell;
struct EditorOptions;
namespace exercises {
// Scene and form authoring exercises that run before the frame's UI: cooking, Place panel and outliner commands
// (also driven with synthetic ImGui mouse and key events), File > Save All, workspace switching, the import dialog,
// GUI ability/effect authoring and party travel placement. They act only through Controller and Shell commands.
struct SceneEditingContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    const std::string& scene_baseline;
    std::size_t import_count{};
};
std::unique_ptr<Exercise> make_scene_editing(SceneEditingContext);
} // namespace exercises
} // namespace darkangel::editor_app
