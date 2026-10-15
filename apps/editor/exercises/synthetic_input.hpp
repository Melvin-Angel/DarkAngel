#pragma once
#include <darkangel/input.hpp>
#include <array>
#include <optional>

namespace darkangel::editor_app {
struct EditorOptions;
namespace exercises {
// Play exercises that replace the input devices with a scripted control pattern and advance Play, the look input and
// the camera by fixed 1/60 s steps, so their results do not depend on rendering speed.
bool fixed_step_play(const EditorOptions&);
// Whether a control is held on a frame of a fixed-step exercise. The samples go through the editor's InputManager
// like device samples, so bindings and semantic actions are exercised unchanged.
bool synthetic_control_down(const EditorOptions&, unsigned frame, InputControl);
// The heavy exercise also steps party travel and the second player's camera by fixed 1/60 s steps.
bool fixed_presentation_step(const EditorOptions&);
// The look exercise's mouse movement on the frames it moves the camera (right and up), in pixels.
std::optional<std::array<float, 2>> scripted_look(const EditorOptions&, unsigned frame);
} // namespace exercises
} // namespace darkangel::editor_app
