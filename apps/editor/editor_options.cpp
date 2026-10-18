#include "editor_options.hpp"
#include "exercises/registry.hpp"
#include <darkangel/check.hpp>
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <darkangel/tags.hpp>
#include <cmath>
#include <stdexcept>

namespace darkangel::editor_app {
EditorOptions parse_editor_options(int argc, char** argv) {
    EditorOptions out;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (exercises::consume_option(arg, i, argc, argv, out))
            continue;
        if (arg == "--world-place" && i + 1 < argc) {
            out.world_place = argv[++i];
            continue;
        }
        if (arg == "--exercise-scale" && i + 1 < argc) {
            out.exercise_scale = argv[++i];
            continue;
        }
        if (arg == "--play-from-save") {
            out.play_from_save = true;
            continue;
        }
        if (arg == "--play") {
            out.play = true;
            continue;
        }
        if ((arg == "--camera-from" || arg == "--camera-target") && i + 3 < argc) {
            auto& destination = arg == "--camera-from" ? out.camera_from : out.camera_target;
            for (auto& coordinate : destination) coordinate = std::stod(argv[++i]);
            (arg == "--camera-from" ? out.camera_from_set : out.camera_target_set) = true;
            continue;
        }
        if (arg == "--items-workspace") {
            out.items_workspace = true;
            continue;
        }
        if (arg == "--party-report" && i + 1 < argc) {
            out.party_report = argv[++i];
            continue;
        }
        if (arg == "--join" && i + 1 < argc) {
            out.join = argv[++i];
            continue;
        }
        if (arg == "--listen" && i + 1 < argc) {
            out.listen = argv[++i];
            continue;
        }
        if (arg == "--player" && i + 1 < argc) {
            out.player = unsigned(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--local-profile" && i + 1 < argc) {
            out.local_profile = unsigned(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--party-mismatch") {
            out.party_mismatch = true;
            continue;
        }
        if (arg == "--players" && i + 1 < argc) {
            out.players = unsigned(std::stoul(argv[++i]));
            continue;
        }
        if (arg == "--input-script" && i + 1 < argc) {
            out.input_script = argv[++i];
            continue;
        }
        if (arg == "--travel-scene" && i + 1 < argc) {
            out.travel_scene = argv[++i];
            continue;
        }
        if (arg == "--travel-entry" && i + 1 < argc) {
            out.travel_entry = argv[++i];
            continue;
        }
        if (arg == "--player-script" && i + 1 < argc) {
            out.player_script = argv[++i];
            continue;
        }
        if (arg == "--travel-hold" && i + 1 < argc) {
            out.travel_hold = std::stod(argv[++i]);
            continue;
        }
        if (arg == "--navigation-tool") {
            out.navigation_tool = true;
            continue;
        }
        if (arg == "--no-audio") {
            out.no_audio = true;
            continue;
        }
        if (arg == "--vfx-workspace") {
            out.vfx_workspace = true;
            continue;
        }
        if (arg == "--audio-workspace") {
            out.audio_workspace = true;
            continue;
        }
        if (arg == "--select-spawn") {
            out.select_spawn = true;
            continue;
        }
        if (arg == "--select-encounter") {
            out.select_encounter = true;
            continue;
        }
        if (arg == "--character-workspace") {
            out.character_workspace = true;
            continue;
        }
        if (arg == "--input-workspace") {
            out.input_workspace = true;
            continue;
        }
        if (arg == "--player-workspace") {
            out.player_workspace = true;
            continue;
        }
        if (arg == "--npc-workspace") {
            out.npc_workspace = true;
            continue;
        }
        if (arg == "--animation-workspace") {
            out.animation_workspace = true;
            continue;
        }
        if (arg == "--ability-workspace") {
            out.ability_workspace = true;
            continue;
        }
        if (arg == "--game-workspace") {
            out.game_workspace = true;
            continue;
        }
        if (arg == "--tools-workspace") {
            out.tools_workspace = true;
            continue;
        }
        if (arg == "--settings-workspace") {
            out.settings_workspace = true;
            continue;
        }
        if (arg == "--assets-workspace") {
            out.assets_workspace = true;
            continue;
        }
        if (arg == "--capture-workspace") {
            out.capture_workspace = true;
            continue;
        }
        if (arg == "--skin-reference") {
            out.skin_reference = true;
            continue;
        }
        if (arg == "--camera-close") {
            out.camera_close = true;
            continue;
        }
        if (arg == "--agent-authoring") {
            out.agent_authoring = true;
            continue;
        }
        if (arg == "--hidden") {
            out.hidden = true;
            continue;
        }
        require(i + 1 < argc, "Missing editor option value");
        std::string value = argv[++i];
        if (arg == "--graph-preview-tag") {
            auto tag = std::stoull(value);
            require(tag && tag <= UINT32_MAX && out.graph_preview_tags.size() < 128, "Graph preview tag input bound");
            out.graph_preview_tags.push_back(static_cast<TagId>(tag));
        } else if (arg == "--graph-character")
            out.graph_character = value;
        else if (arg == "--character-preview-clip")
            out.character_preview_clip = value;
        else if (arg == "--player-asset")
            out.player_asset = value;
        else if (arg == "--mask-tune")
            out.mask_tune = value;
        else if (arg == "--mask-editor")
            out.mask_editor = AssetId::parse(value).text();
        else if (arg == "--composer-tick")
            out.composer_tick = std::stod(value);
        else if (arg == "--authoring-asset")
            out.authoring_asset = value;
        else if (arg == "--sources")
            out.sources = value;
        else if (arg == "--asset-cache")
            out.asset_cache = value;
        else if (arg == "--import-preview")
            out.import_preview = value;
        else if (arg == "--character-collision")
            out.character_collision = value;
        else if (arg == "--character-player")
            out.character_player = value;
        else if (arg == "--character-kit")
            out.character_clips = "kit:" + value;
        else if (arg == "--character-graph")
            out.character_clips = "graph:" + value;
        else if (arg == "--character-clips")
            out.character_clips = value;
        else if (arg == "--pose-clip")
            out.pose_clip = value;
        else if (arg == "--pose-tick")
            out.pose_tick = std::stod(value);
        else if (arg == "--vfx-select")
            out.vfx_select = value;
        else if (arg == "--scene")
            out.scene = value;
        else if (arg == "--width")
            out.width = static_cast<unsigned>(std::stoul(value));
        else if (arg == "--height")
            out.height = static_cast<unsigned>(std::stoul(value));
        else if (arg == "--agent-descriptor")
            out.agent_descriptor = value;
        else if (arg == "--agent-project")
            out.agent_project = StableId::parse(value).text();
        else if (arg == "--registry")
            out.registry = value;
        else if (arg == "--cas")
            out.cas = value;
        else if (arg == "--model")
            out.model = value;
        else if (arg == "--capture")
            out.capture = value;
        else if (arg == "--frames")
            out.frames = static_cast<unsigned>(std::stoul(value));
        else if (arg == "--backend")
            out.backend = value;
        else
            throw std::runtime_error("Unknown editor option");
    }
    require(out.join.empty() || out.listen.empty(), "A party player cannot also listen");
    require(out.player > 0 && out.player < 4 && out.local_profile <= 4, "Party player/profile slot bounds");
    require(out.join.empty() ||
                (!out.exercise && !out.exercise_heavy && !out.exercise_character && !out.exercise_combat && !out.agent_authoring),
            "A party guest runs only its client Play");
    require(out.exercise_import.empty() || (!out.sources.empty() && out.frames >= 3),
            "Import exercise requires a project mount and at least3 frames");
    require(out.sources.empty() == out.asset_cache.empty(), "Project asset browsing requires both --sources and --asset-cache");
    require(out.import_preview.empty() || !out.sources.empty(), "Import preview requires a project source mount");
    require(!out.registry.empty() && !out.cas.empty() && !out.model.empty(),
            "DarkAngelEditor requires --registry file --cas directory --model UUID");
    require(out.backend == "d3d12" || out.backend == "vulkan", "Unsupported backend");
    require(out.camera_from_set == out.camera_target_set, "Camera pose requires both --camera-from and --camera-target");
    require(!(out.camera_from_set && out.camera_close), "Choose either an explicit camera pose or --camera-close");
    for (double coordinate : out.camera_from) require(std::isfinite(coordinate) && std::abs(coordinate) <= 100000, "Camera position bounds");
    for (double coordinate : out.camera_target) require(std::isfinite(coordinate) && std::abs(coordinate) <= 100000, "Camera target bounds");
    if (out.camera_from_set) {
        const auto dx=out.camera_from[0]-out.camera_target[0], dy=out.camera_from[1]-out.camera_target[1], dz=out.camera_from[2]-out.camera_target[2];
        require(dx*dx+dy*dy+dz*dz>.01, "Camera position and target must differ");
    }
    require(out.width >= 640 && out.width <= 3840 && out.height >= 480 && out.height <= 2160, "Editor window size bounds");
    require(out.frames <= 10000, "Frame budget limit");
    require(std::isfinite(out.pose_tick) && out.pose_tick >= 0 && out.pose_tick <= 600, "Pose diagnostic tick limit");
    require(out.agent_descriptor.empty() == out.agent_project.empty() && (!out.agent_authoring || !out.agent_descriptor.empty()),
            "Agent requires explicit descriptor and project UUID");
#ifndef DAE_AGENT_ENDPOINTS
    require(out.agent_descriptor.empty(), "This build excludes agent endpoints");
#endif
#ifndef DAE_ANIMATION
    require(out.pose_clip.empty() && !out.skin_reference && out.pose_tick == 0, "Pose diagnostics require animation-enabled editor");
#endif
#ifndef DAE_CHARACTER_SCENE
    require(out.character_collision.empty() && out.character_clips.empty() && !out.exercise_character && !out.exercise_combat &&
                !out.exercise_heavy && !out.exercise_composer,
            "Character scene requires the animation/physics editor");
#endif
    require(!out.exercise_composer || (!out.authoring_asset.empty() && !out.sources.empty() && !out.character_clips.empty() &&
                                       std::isfinite(out.composer_tick) && out.composer_tick >= 0 && out.composer_tick <= 600),
            "Composer exercise needs an action, character, sources and bounded tick");
    require(!out.exercise_heavy || ((out.character_clips.starts_with("kit:") || out.character_clips.starts_with("player:")) &&
                                    (out.frames == 30 || out.frames == 210 || out.exercise_two_players || out.exercise_attacked ||
                                     out.exercise_ranged || (out.exercise_authoring && out.frames == 150)) &&
                                    !out.exercise_combat && !out.exercise_character),
            "Heavy exercise requires a cooked kit and 30 or210 frames");
    require(!out.exercise_combat || (out.character_clips.starts_with("kit:") && out.frames >= 30),
            "Combat exercise requires a cooked kit and at least30 frames");
    require(!out.exercise_workspaces || (out.exercise_character && out.frames >= 100),
            "Workspace exercise requires character exercise and at least100 frames");
    require(!out.exercise_character || (!out.character_clips.empty() && out.frames >= 30),
            "Character exercise requires configured clips and at least30 frames");
    require(out.character_collision.empty() || !out.character_clips.empty(), "Native collision requires a character scene");
    require(out.character_clips.empty() || (!out.scene.empty() && !out.skin_reference),
            "Character scene requires a saved scene and GPU skinning");
    return out;
}

} // namespace darkangel::editor_app
