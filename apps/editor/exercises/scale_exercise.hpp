#pragma once
#include "exercise.hpp"
#include <memory>

namespace darkangel::editor_app {
class Controller;
class Shell;
struct EditorOptions;
namespace exercises {
// Scene-scale baseline (SCENE_SCALE_PLAN.md phase 0), enabled by --exercise-scale <config.json>. On the open scene it times authoring
// frames, placing a model the scene already uses, moving a placement, placing a model it does not use yet, preparing Play and
// Play frames, with process memory around them, then writes the report the config names. It measures only: it never saves, so
// the scene on disk is unchanged. A step that fails records its error and the steps that depend on it are skipped.
struct ScaleContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
};
std::unique_ptr<Exercise> make_scale(ScaleContext);
} // namespace exercises
} // namespace darkangel::editor_app
