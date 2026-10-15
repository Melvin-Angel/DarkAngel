#pragma once
#include "exercise.hpp"
#include "../play_controller.hpp"
#include <cstdint>
#include <functional>
#include <memory>

namespace darkangel {
class CharacterSceneSession;
namespace editor_app {
class Controller;
class Shell;
struct EditorOptions;
struct CharacterPreviewResources;
struct GameplayDebugSelection;
namespace exercises {
// Play exercises that Play starts on the first frame, in the Game workspace.
bool starts_play(const EditorOptions&);
// The two-player exercise plays both players in this process instead of hosting a network party.
bool local_party(const EditorOptions&);
// Views exercises open at startup: the reaction exercise's Gameplay Inspector Actors tab and the camera preview's camera.
void select_startup_views(const EditorOptions&, Shell&);

// Play exercises that focus the Gameplay Inspector, observe senses or select what the viewports draw, and their end
// checks of what Play's cue presentation and the viewports produced.
struct PlayExerciseContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    CharacterSceneSession* const& session;
    const std::unique_ptr<CharacterPreviewResources>& resources;
    GameplayDebugSelection& debug;
    PlayCueState cues;
    std::function<StableId()> first_actor;
    // What the viewports drew on the last frame.
    const unsigned& senses_drawn;
    const unsigned& volumes_drawn;
    const unsigned& splines_drawn;
    const unsigned& scatter_drawn;
};
class PlayExercises final : public Exercise {
  public:
    explicit PlayExercises(PlayExerciseContext);
    // Before the frame's other exercises.
    void frame(unsigned frame) override;
    // Right after the Play trigger: the refused activation.
    void launched(unsigned frame);
    // Drawing checks (splines, encounter volumes), with the other presentation checks before the media checks.
    void check_drawing();
    // The cue and spawn-senses checks, after the media checks and before the gameplay checks.
    void finish() override;
    bool done() const override {
        return true;
    }
    std::string_view result() const override {
        return {};
    }
    // What the senses exercise observed, for the gameplay checks.
    bool senses_early{};
    std::uint64_t senses_noticed{};
    double senses_noticed_z{};

  private:
    PlayExerciseContext context_;
};
std::unique_ptr<PlayExercises> make_play_exercises(PlayExerciseContext);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
