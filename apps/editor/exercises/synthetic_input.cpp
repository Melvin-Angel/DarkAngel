#include "synthetic_input.hpp"
#include "../editor_options.hpp"

namespace darkangel::editor_app::exercises {
bool fixed_step_play(const EditorOptions& args) {
    return args.exercise_character || args.exercise_combat || args.exercise_heavy || args.exercise_actor_workflow ||
           args.exercise_player_kit_play;
}
bool synthetic_control_down(const EditorOptions& args, unsigned frame, InputControl control) {
    return ((args.exercise_actor_workflow || args.exercise_player_kit_play)
                ? (control == input_control(args.exercise_player_input ? "Keyboard.K" : "Mouse.Left") && frame == 5)
            : args.exercise_interaction ? ((control == input_control("Keyboard.W") && frame >= 30 && frame < 70) ||
                                           (control == input_control("Keyboard.F") && frame >= 100 && frame < 104))
            : args.exercise_encounter   ? (control == input_control("Keyboard.W") && frame >= 30 && frame < 110)
            : args.exercise_senses      ? (control == input_control("Keyboard.W") && frame >= 60 && frame < 120)
            : args.exercise_camera_look || args.exercise_attacked || args.exercise_two_players || args.exercise_transition ? false
            : args.exercise_ranged      ? ((control == input_control("Keyboard.AltLeft") && frame >= 4) ||
                                           (control == input_control("Mouse.Left") && frame >= 10 && frame < 14))
            : args.exercise_soft_lock   ? ((control == input_control("Keyboard.D") && frame < 16) ||
                                           (control == input_control("Keyboard.W") && frame >= 18 && frame < 24) ||
                                           (control == input_control("Mouse.Left") && frame >= 20 && frame < 24))
            : args.exercise_mask_switch ? (control == input_control("Keyboard.3") && frame >= 6 && frame < 10)
            : args.exercise_aim         ? (control == input_control("Keyboard.AltLeft") && frame >= 8)
            : args.exercise_dodge       ? ((control == input_control("Keyboard.C") && frame >= 6 && frame < 12) ||
                                           (control == input_control("Keyboard.D") && frame < 12))
            : args.exercise_heavy       ? (control == input_control("Mouse.Right") && (args.exercise_death ? frame % 100 < 40 : frame < 40))
            : args.exercise_combat      ? (control == input_control("Mouse.Left") && (frame == 0 || frame == 60))
                                        : control == input_control("Keyboard.W") && (!args.exercise_workspaces || frame != 60));
}
bool fixed_presentation_step(const EditorOptions& args) {
    return args.exercise_heavy;
}
std::optional<std::array<float, 2>> scripted_look(const EditorOptions& args, unsigned frame) {
    if (args.exercise_camera_look && frame >= 8 && frame < 18)
        return std::array<float, 2>{6, -3};
    return std::nullopt;
}
} // namespace darkangel::editor_app::exercises
