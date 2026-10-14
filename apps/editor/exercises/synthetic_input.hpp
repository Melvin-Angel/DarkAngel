#pragma once
#include <darkangel/input.hpp>

namespace darkangel::editor_app {
struct EditorOptions;
namespace exercises {
// Play exercises that replace the input devices with a scripted control pattern and advance Play, the look input and
// the camera by fixed 1/60 s steps, so their results do not depend on rendering speed.
bool fixed_step_play(const EditorOptions&);
// Whether a control is held on a frame of a fixed-step exercise. The samples go through the editor's InputManager
// like device samples, so bindings and semantic actions are exercised unchanged.
bool synthetic_control_down(const EditorOptions&, unsigned frame, InputControl);
} // namespace exercises
} // namespace darkangel::editor_app
