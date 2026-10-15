#pragma once
#include <functional>
#include "editor_ui.hpp"
#include <darkangel/world.hpp>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>

struct ImDrawList;
namespace darkangel {
class CharacterSceneSession;
class PhysicsWorld;
struct CollisionDefinition;
namespace editor_app {
class Controller;
struct NavigationTool;
struct CharacterPreviewResources;
struct GameplayDebugSelection;
namespace render {
class SceneResources;
}
// The Scene and Game viewports' authoring tools: picking scene objects, the placement surface on the scene's cooked
// collision, and the debug overlay of volumes, splines, markers, the navigation mesh and NPC senses. During Play the
// overlay reads the host's session; it draws only.
struct ViewportToolsContext {
    Controller& editor;
    Shell& shell;
    render::SceneResources& scene;
    CharacterSceneSession* const& session;
    const std::unique_ptr<CharacterPreviewResources>& resources;
    // The open scene's effective static collision (PlayController::static_collision).
    std::function<std::shared_ptr<const CollisionDefinition>()> static_collision;
    // Null in builds without navigation.
    NavigationTool* navigation{};
    GameplayDebugSelection& debug;
};
class ViewportTools {
  public:
    explicit ViewportTools(ViewportToolsContext);
    ~ViewportTools();
    ViewportTools(const ViewportTools&) = delete;
    ViewportTools& operator=(const ViewportTools&) = delete;
    // The Shell's pick, placement surface and debug overlay.
    void install();
    // Viewport picking: the first scene object whose bounds the segment meets.
    StableId pick(const std::array<double, 3>& from, const std::array<double, 3>& to);
    // Placement surface: the nearest hit of a ray against the scene's cooked static collision.
    std::optional<std::array<double, 3>> place(const std::array<double, 3>& from, const std::array<double, 3>& to);
    void draw_overlay(ImDrawList* draw, ViewArea area);

    // What the overlay drew on its last pass.
    unsigned senses_drawn = 0, volumes_drawn = 0, navigation_drawn = 0, splines_drawn = 0;

  private:
    // The placement surface's query world, built again when the collision changes.
    std::unique_ptr<darkangel::PhysicsWorld> place_world;
    std::shared_ptr<const darkangel::CollisionDefinition> place_collision;
    std::function<std::shared_ptr<const CollisionDefinition>()> static_collision_;
    Controller& editor;
    Shell& shell;
    render::SceneResources& scene_;
    CharacterSceneSession* const& character;
    const std::unique_ptr<CharacterPreviewResources>& character_resources;
    NavigationTool* navigation_;
    std::set<StableId>& senses_shown;
    bool& senses_all;
    bool& world_debug;
};
} // namespace editor_app
} // namespace darkangel
