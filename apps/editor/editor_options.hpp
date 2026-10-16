#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace darkangel::editor_app {
struct EditorOptions {
    std::filesystem::path party_report, world_place, exercise_scale;
    bool play_from_save{}, play{}, exercise_scene_camera{}, exercise_editor_ux{}, exercise_live_previews{}, exercise_prefabs{}, exercise_materials{}, exercise_ui{}, exercise_ui_play{};
    std::string join, listen;
    unsigned player{1}, local_profile{4};
    bool party_mismatch{};
    bool items_workspace{};
    bool exercise_transition{}, exercise_travel_place{};
    std::string travel_scene, travel_entry, player_script;
    double travel_hold{-1};
    unsigned players{1};
    std::string input_script;
    bool exercise_two_players{};
    std::vector<std::uint32_t> graph_preview_tags;
    std::filesystem::path registry, cas, capture, agent_descriptor, scene, sources, asset_cache, import_preview, exercise_import;
    std::string agent_project;
    bool agent_authoring{};
    std::string character_clips, character_player, character_collision, vfx_select;
    std::string graph_character, pose_clip, authoring_asset, player_asset, character_preview_clip, mask_tune, mask_editor;
    double pose_tick{}, composer_tick{};
    bool navigation_tool{}, exercise_place{}, exercise_navigation_bake{}, exercise_navigation_overlay{}, exercise_soft_lock{},
        exercise_ranged{}, exercise_upper_body{}, exercise_reaction{}, exercise_death{}, exercise_attacked{}, exercise_senses{},
        select_spawn{}, exercise_place_mouse{}, exercise_mask_field{}, exercise_save_all{}, exercise_encounter{}, exercise_interaction{},
        exercise_spline{}, select_encounter{}, exercise_cook_sweep{}, exercise_mask_switch{}, exercise_cues{}, no_audio{}, vfx_workspace{},
        exercise_vfx_edit{}, exercise_cue_form{}, exercise_reject{}, audio_workspace{}, exercise_audio{}, exercise_dodge{}, exercise_aim{},
        exercise_camera_preview{}, exercise_camera_look{}, exercise_graph_result{}, exercise_blend_space{}, exercise_graph_canvas{},
        exercise_independent_ability{}, exercise_history_source_filter{}, exercise_history_source_compare{}, exercise_source_compare{},
        exercise_player_kit_play{}, exercise_effect_preview_corrected{}, exercise_effect_preview_reprepare{},
        exercise_effect_preview_reset{}, exercise_effect_preview_failure{}, exercise_effect_source_preview{},
        exercise_play_player_navigation{}, exercise_player_kit_context{}, exercise_player_kit_copy{}, exercise_binding_links{},
        exercise_kit_copy_undo{}, exercise_pending_selection{}, exercise_creation_vertical{}, exercise_history_validation{},
        exercise_effect_revision{}, exercise_validation_error{}, exercise_validation{}, exercise_native_navigation{},
        exercise_creation_drafts{}, exercise_history_view{}, exercise_graph_preview{}, exercise_graph_authoring{}, input_workspace{},
        exercise_player_input{}, exercise_game_debug{}, exercise_player_defaults{}, exercise_character_preview{}, exercise_effect_preview{},
        exercise_composer{}, composer_playback{};
    std::string model, backend{"d3d12"};
    unsigned frames{}, width{1280}, height{800};
    bool character_workspace{}, player_workspace{}, npc_workspace{}, exercise_actor_workflow{}, animation_workspace{},
        exercise_animation_workflow{}, ability_workspace{}, exercise_authoring{}, authoring_replay{}, hidden{}, exercise{}, camera_close{},
        skin_reference{}, exercise_character{}, exercise_combat{}, exercise_heavy{}, exercise_workspaces{}, assets_workspace{},
        game_workspace{}, tools_workspace{}, settings_workspace{}, capture_workspace{};
};

EditorOptions parse_editor_options(int argc, char** argv);
} // namespace darkangel::editor_app
