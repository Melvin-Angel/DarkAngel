#pragma once
#include "exercise.hpp"
#include <memory>

namespace darkangel {
class CharacterSceneSession;
namespace editor_app {
class Controller;
class Shell;
class VfxWorkspace;
class AudioWorkspace;
struct EditorOptions;
namespace exercises {
// Audio tab and Play mixer edits, the cue form and an external VFX source change, through the workspaces' own commands,
// with their end-of-run checks.
struct MediaContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    VfxWorkspace& vfx;
    // Null in builds without audio.
    AudioWorkspace* audio{};
    CharacterSceneSession* const& session;
};
std::unique_ptr<Exercise> make_media_exercises(MediaContext);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
