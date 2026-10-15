#pragma once
#include "play_controller.hpp"
#include <darkangel/world.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace darkangel {
class CharacterSceneSession;
namespace editor_app {
class Controller;
class Shell;
class RuntimeInventory;
struct CharacterPreviewResources;
// What the Gameplay Inspector asks the viewports to draw, and which of its tabs to bring forward.
struct GameplayDebugSelection {
    // Actors whose senses are drawn; senses_all draws every actor's.
    std::set<StableId> senses_shown;
    bool senses_all=false;
    // Encounter and trigger volumes in the viewports during Play.
    bool world_debug=false;
    // Selects the Cues tab on the next draw.
    bool select_cues=false;
};
// The Gameplay Inspector reads the running session and acts only through authoring commands: opening the active ability
// asset, emitting a world event, granting an item through the authority path. It has no device, networking or saves.
struct GameplayInspectorContext {
    // Asset and placement names, the console, and the asset an inspection opens.
    Controller& editor;
    // Opens native assets and asks for the Actors tab.
    Shell& shell;
    CharacterSceneSession* const& session;
    const std::unique_ptr<CharacterPreviewResources>& resources;
    const StableId& player;
    PlayCueState cues;
    // The inventory section; null in builds without the runtime HUD.
    RuntimeInventory* inventory{};
    std::function<std::optional<StableId>()> hud_target;
};
class GameplayInspector {
  public:
    explicit GameplayInspector(GameplayInspectorContext);
    // Player, World, Cues and Actors tabs.
    void draw();
    // The Game tab's readout of the running session.
    std::string metrics();
    GameplayDebugSelection selection;

  private:
    GameplayInspectorContext context_;
    char event_name_[49]{};
};
} // namespace editor_app
} // namespace darkangel
