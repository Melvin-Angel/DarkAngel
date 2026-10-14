#pragma once
#include <darkangel/asset_id.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace darkangel {
class CharacterSceneSession;
class CameraDirector;
class SceneTransitionCoordinator;
class WorldLedger;
class Hud;
namespace editor_app {
class Controller;
class Shell;
class ImGuiHudRenderer;
struct EditorOptions;
struct CharacterPreviewResources;
namespace exercises {
class ScriptRunner;
// What the editor's frame loop counted while Play ran, for the checks that follow it.
struct GameplayObservations {
    const std::size_t& projectiles_seen;
    const unsigned& navigation_polygons;
    const std::string& behavior_running;
    const unsigned& senses_drawn;
    const unsigned& volumes_drawn;
    const unsigned& interactions_asked;
    const bool& interaction_offered;
    const bool& senses_early;
    const std::uint64_t& senses_noticed;
    const double& senses_noticed_z;
    const unsigned& travel_arrivals;
    const unsigned& travel_failures;
    const unsigned& travel_overlay_frames;
    const std::string& travel_failure;
    // The second local player's viewport texture exists and its HUD was drawn.
    bool second_view_drawn{};
};
// End-of-run checks for Play exercises: combat, abilities, camera, senses, encounters, interaction, party travel and
// two local players. They read confirmed session state and stop Play through Controller where the exercise ends it;
// later checks see the stopped session through the same session pointer.
struct GameplayResultContext {
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    const std::string& scene_baseline;
    CharacterSceneSession* const& session;
    const std::unique_ptr<CharacterPreviewResources>& resources;
    const std::unique_ptr<CameraDirector>& director;
    const std::unique_ptr<SceneTransitionCoordinator>& travel;
    const std::unique_ptr<Hud>& hud;
    const ImGuiHudRenderer* hud_renderer{};
    const WorldLedger& ledger;
    const AssetId& authoring_scene;
    ScriptRunner& companion_script;
    ScriptRunner& owner_script;
    std::function<StableId()> first_actor;
    std::function<std::optional<StableId>()> hud_target;
    GameplayObservations observed;
};
void check_gameplay_results(const GameplayResultContext&);
} // namespace exercises
} // namespace editor_app
} // namespace darkangel
