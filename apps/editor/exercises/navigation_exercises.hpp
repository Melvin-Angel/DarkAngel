#pragma once
#include "exercise.hpp"
#include <functional>
#include <memory>

namespace darkangel {
struct CollisionDefinition;
namespace editor_app {
class Shell;
struct NavigationTool;
struct EditorOptions;
namespace exercises {
// Tools > Navigation: a bake (and the viewport overlay) requested on the first frame through the tool's own command, then
// checked against what was drawn and against Play's reuse of the bake.
struct NavigationContext {
    const EditorOptions& options;
    Shell& shell;
    // Null in builds without navigation.
    NavigationTool* navigation{};
    std::function<const CollisionDefinition*()> collision;
    const unsigned& navigation_drawn;
};
std::unique_ptr<Exercise> make_navigation_exercises(NavigationContext);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
