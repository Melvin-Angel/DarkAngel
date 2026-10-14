#include "registry.hpp"
#include "../editor_options.hpp"
#include <darkangel/check.hpp>
#include <array>

namespace darkangel::editor_app::exercises {
namespace {
struct Flag {
    std::string_view name;
    std::array<bool EditorOptions::*, 6> enabled;
};
constexpr auto flags = std::to_array<Flag>({
    {"--exercise-two-players", {&EditorOptions::exercise_two_players, &EditorOptions::exercise_heavy}},
    {"--exercise-transition", {&EditorOptions::exercise_transition, &EditorOptions::exercise_two_players, &EditorOptions::exercise_heavy}},
    {"--exercise-travel-place", {&EditorOptions::exercise_travel_place}},
    {"--exercise-place", {&EditorOptions::exercise_place}},
    {"--exercise-navigation-bake", {&EditorOptions::exercise_navigation_bake, &EditorOptions::navigation_tool}},
    {"--exercise-ranged", {&EditorOptions::exercise_ranged, &EditorOptions::exercise_heavy}},
    {"--exercise-navigation-overlay", {&EditorOptions::exercise_navigation_overlay}},
    {"--exercise-soft-lock", {&EditorOptions::exercise_soft_lock, &EditorOptions::exercise_heavy}},
    {"--exercise-upper-body", {&EditorOptions::exercise_upper_body, &EditorOptions::exercise_heavy}},
    {"--exercise-camera-look", {&EditorOptions::exercise_camera_look, &EditorOptions::exercise_heavy}},
    {"--exercise-aim", {&EditorOptions::exercise_aim, &EditorOptions::exercise_heavy}},
    {"--exercise-camera-preview", {&EditorOptions::exercise_camera_preview}},
    {"--exercise-dodge", {&EditorOptions::exercise_dodge, &EditorOptions::exercise_heavy}},
    {"--exercise-cues", {&EditorOptions::exercise_cues}},
    {"--exercise-reject", {&EditorOptions::exercise_reject}},
    {"--exercise-vfx-edit", {&EditorOptions::exercise_vfx_edit}},
    {"--exercise-cue-form", {&EditorOptions::exercise_cue_form}},
    {"--exercise-audio", {&EditorOptions::exercise_audio}},
    {"--exercise-mask-switch", {&EditorOptions::exercise_mask_switch, &EditorOptions::exercise_heavy}},
    {"--exercise-attacked", {&EditorOptions::exercise_attacked, &EditorOptions::exercise_heavy}},
    {"--exercise-senses", {&EditorOptions::exercise_senses, &EditorOptions::exercise_attacked, &EditorOptions::exercise_heavy}},
    {"--exercise-place-mouse", {&EditorOptions::exercise_place_mouse}},
    {"--exercise-mask-field", {&EditorOptions::exercise_mask_field}},
    {"--exercise-save-all", {&EditorOptions::exercise_save_all}},
    {"--exercise-spline", {&EditorOptions::exercise_spline}},
    {"--exercise-interaction", {&EditorOptions::exercise_interaction, &EditorOptions::exercise_attacked, &EditorOptions::exercise_heavy}},
    {"--exercise-encounter", {&EditorOptions::exercise_encounter, &EditorOptions::exercise_attacked, &EditorOptions::exercise_heavy}},
    {"--exercise-cook-sweep", {&EditorOptions::exercise_cook_sweep}},
    {"--exercise-death", {&EditorOptions::exercise_death, &EditorOptions::exercise_heavy}},
    {"--exercise-reaction", {&EditorOptions::exercise_reaction, &EditorOptions::exercise_upper_body, &EditorOptions::exercise_heavy}},
    {"--exercise-graph-result",
     {&EditorOptions::exercise_graph_result, &EditorOptions::exercise_graph_preview, &EditorOptions::animation_workspace}},
    {"--exercise-blend-space",
     {&EditorOptions::exercise_blend_space, &EditorOptions::exercise_graph_canvas, &EditorOptions::animation_workspace}},
    {"--exercise-graph-canvas", {&EditorOptions::exercise_graph_canvas, &EditorOptions::animation_workspace}},
    {"--exercise-independent-vertical",
     {&EditorOptions::exercise_independent_ability, &EditorOptions::exercise_creation_vertical, &EditorOptions::exercise_actor_workflow,
      &EditorOptions::ability_workspace}},
    {"--exercise-independent-ability", {&EditorOptions::exercise_independent_ability, &EditorOptions::ability_workspace}},
    {"--exercise-history-source-filter",
     {&EditorOptions::exercise_history_source_filter, &EditorOptions::exercise_native_navigation, &EditorOptions::player_workspace}},
    {"--exercise-history-source-compare",
     {&EditorOptions::exercise_history_source_compare, &EditorOptions::exercise_source_compare, &EditorOptions::ability_workspace}},
    {"--exercise-source-compare", {&EditorOptions::exercise_source_compare, &EditorOptions::ability_workspace}},
    {"--exercise-player-kit-play",
     {&EditorOptions::exercise_player_kit_play, &EditorOptions::exercise_player_kit_copy, &EditorOptions::player_workspace}},
    {"--exercise-effect-preview-corrected",
     {&EditorOptions::exercise_effect_preview_corrected, &EditorOptions::exercise_effect_preview_reprepare,
      &EditorOptions::exercise_effect_source_preview, &EditorOptions::ability_workspace}},
    {"--exercise-effect-preview-reprepare",
     {&EditorOptions::exercise_effect_preview_reprepare, &EditorOptions::exercise_effect_source_preview,
      &EditorOptions::ability_workspace}},
    {"--exercise-effect-preview-reset",
     {&EditorOptions::exercise_effect_preview_reset, &EditorOptions::exercise_effect_source_preview, &EditorOptions::ability_workspace}},
    {"--exercise-effect-preview-failure",
     {&EditorOptions::exercise_effect_preview_failure, &EditorOptions::exercise_effect_source_preview, &EditorOptions::ability_workspace}},
    {"--exercise-effect-source-preview", {&EditorOptions::exercise_effect_source_preview, &EditorOptions::ability_workspace}},
    {"--exercise-play-player-navigation",
     {&EditorOptions::exercise_play_player_navigation, &EditorOptions::exercise_player_kit_context,
      &EditorOptions::exercise_player_kit_copy, &EditorOptions::player_workspace}},
    {"--exercise-player-kit-context",
     {&EditorOptions::exercise_player_kit_context, &EditorOptions::exercise_player_kit_copy, &EditorOptions::player_workspace}},
    {"--exercise-player-kit-copy", {&EditorOptions::exercise_player_kit_copy, &EditorOptions::player_workspace}},
    {"--exercise-binding-links", {&EditorOptions::exercise_binding_links, &EditorOptions::ability_workspace}},
    {"--exercise-kit-copy-undo", {&EditorOptions::exercise_kit_copy_undo, &EditorOptions::ability_workspace}},
    {"--exercise-pending-selection", {&EditorOptions::exercise_pending_selection, &EditorOptions::ability_workspace}},
    {"--exercise-creation-vertical",
     {&EditorOptions::exercise_creation_vertical, &EditorOptions::exercise_actor_workflow, &EditorOptions::ability_workspace}},
    {"--exercise-game-debug", {&EditorOptions::exercise_game_debug, &EditorOptions::exercise_authoring, &EditorOptions::exercise_heavy}},
    {"--exercise-history-validation",
     {&EditorOptions::exercise_history_validation, &EditorOptions::exercise_validation, &EditorOptions::exercise_native_navigation,
      &EditorOptions::player_workspace}},
    {"--exercise-effect-revision", {&EditorOptions::exercise_effect_revision, &EditorOptions::ability_workspace}},
    {"--exercise-validation-error",
     {&EditorOptions::exercise_validation_error, &EditorOptions::exercise_validation, &EditorOptions::exercise_native_navigation,
      &EditorOptions::player_workspace}},
    {"--exercise-validation",
     {&EditorOptions::exercise_validation, &EditorOptions::exercise_native_navigation, &EditorOptions::player_workspace}},
    {"--exercise-native-navigation", {&EditorOptions::exercise_native_navigation, &EditorOptions::player_workspace}},
    {"--exercise-creation-drafts", {&EditorOptions::exercise_creation_drafts, &EditorOptions::character_workspace}},
    {"--exercise-history-view", {&EditorOptions::exercise_history_view, &EditorOptions::ability_workspace}},
    {"--exercise-graph-preview", {&EditorOptions::exercise_graph_preview, &EditorOptions::animation_workspace}},
    {"--exercise-graph-authoring",
     {&EditorOptions::exercise_graph_authoring, &EditorOptions::exercise_actor_workflow, &EditorOptions::animation_workspace}},
    {"--exercise-player-input",
     {&EditorOptions::exercise_player_input, &EditorOptions::exercise_actor_workflow, &EditorOptions::player_workspace}},
    {"--exercise-player-defaults",
     {&EditorOptions::exercise_player_defaults, &EditorOptions::exercise_actor_workflow, &EditorOptions::player_workspace}},
    {"--exercise-character-preview", {&EditorOptions::exercise_character_preview, &EditorOptions::character_workspace}},
    {"--exercise-actor-workflow", {&EditorOptions::exercise_actor_workflow, &EditorOptions::player_workspace}},
    {"--exercise-effect-preview", {&EditorOptions::exercise_effect_preview, &EditorOptions::ability_workspace}},
    {"--exercise-animation-workflow",
     {&EditorOptions::animation_workspace, &EditorOptions::exercise_animation_workflow, &EditorOptions::exercise_composer}},
    {"--exercise-composer-playback",
     {&EditorOptions::composer_playback, &EditorOptions::exercise_composer, &EditorOptions::ability_workspace}},
    {"--exercise-composer", {&EditorOptions::exercise_composer, &EditorOptions::ability_workspace}},
    {"--exercise-authored-replay", {&EditorOptions::authoring_replay, &EditorOptions::exercise_authoring, &EditorOptions::exercise_heavy}},
    {"--exercise-authoring", {&EditorOptions::exercise_authoring, &EditorOptions::exercise_heavy}},
    {"--exercise-workspaces", {&EditorOptions::exercise_workspaces}},
    {"--exercise-heavy", {&EditorOptions::exercise_heavy}},
    {"--exercise-combat", {&EditorOptions::exercise_combat}},
    {"--exercise-character", {&EditorOptions::exercise_character}},
    {"--exercise", {&EditorOptions::exercise}},
});
constexpr auto names = [] {
    std::array<std::string_view, flags.size() + 1> out{};
    for (std::size_t i = 0; i < flags.size(); ++i)
        out[i] = flags[i].name;
    out.back() = "--exercise-import";
    return out;
}();
} // namespace
std::span<const std::string_view> option_names() {
    return names;
}
bool consume_option(std::string_view name, int& at, int argc, char** argv, EditorOptions& out) {
    if (name == "--exercise-import") {
        require(at + 1 < argc, "Missing editor option value");
        out.exercise_import = argv[++at];
        return true;
    }
    for (const auto& flag : flags)
        if (flag.name == name) {
            for (const auto field : flag.enabled)
                if (field)
                    out.*field = true;
            return true;
        }
    return false;
}
} // namespace darkangel::editor_app::exercises
