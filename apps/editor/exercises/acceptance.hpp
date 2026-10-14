#pragma once
#include "exercise.hpp"
#include <darkangel/asset_id.hpp>
#include <functional>
#include <memory>
#include <string>

namespace darkangel {
class ModelStore;
namespace editor_app {
class Controller;
struct EditorOptions;
namespace exercises {
// The original editor acceptance: Play isolation, Step, undo/redo, save and reopen, a refused model candidate and a refused
// shader candidate, all through the editor's own commands and its model store and shader reload.
struct AcceptanceContext {
    const EditorOptions& options;
    Controller& editor;
    ModelStore& store;
    AssetId model;
    std::function<void()> reload_model;
    std::function<void(const std::string&)> reload_shader;
};
std::unique_ptr<Exercise> make_acceptance(AcceptanceContext);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
