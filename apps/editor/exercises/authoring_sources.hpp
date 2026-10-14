#pragma once
#include "exercise.hpp"
#include <memory>
#include <string>

namespace darkangel {
class CharacterSceneSession;
namespace editor_app {
class Controller;
class Shell;
struct EditorOptions;
struct CharacterPreviewResources;
namespace exercises {
struct AuthoringSourceContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    const std::unique_ptr<CharacterPreviewResources>& resources;
    CharacterSceneSession* const& session;
    const std::string& scene_baseline;
};
std::unique_ptr<Exercise> make_authoring_sources(AuthoringSourceContext);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
