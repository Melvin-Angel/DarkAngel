#pragma once
#include "editor_options.hpp"
#include <darkangel/assets.hpp>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace darkangel {
#ifdef DAE_AGENT_ENDPOINTS
class AgentEndpoint;
class EditorDocument;
#endif
namespace editor_app {
class RenderSystem;
class Controller;
class Shell;
class VfxWorkspace;
class AudioWorkspace;
class PlaySaves;
class RuntimeInventory;
class RuntimeAltar;
class PlayController;
class GameplayInspector;
class PosePreviews;
class ViewportTools;
struct NavigationTool;
struct HudResources;
struct CueRecorders;
namespace render {
class SceneResources;
class SceneViewport;
}
// The editor process. EditorApp creates the services in dependency order, wires their callbacks to one another and runs
// the frame loop by calling them; each service owns its state and receives only what it needs. Destruction is the
// reverse of creation: Play and the inspectors go before the workspaces they use, every device resource before the
// render system, and the render system before COM.
class EditorApp {
  public:
    explicit EditorApp(EditorOptions);
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;
    // The frame loop, then the end-of-run checks in their fixed order (preview, presentation, gameplay). Returns the exit code.
    int run();

  private:
    struct ProcessSetup;
    // Workspace, tool and asset selections the command line asks for at startup.
    void select_startup_workspace();
    void select_startup_assets();
    // Packages a fresh model generation and the Play resources made from it; committed at GPU idle.
    void reload_model();
    void reload_shader(const std::string& text);
#ifdef DAE_AGENT_ENDPOINTS
    void poll_agent();
#endif

    // Created in this order, destroyed in reverse (see ~EditorApp).
    EditorOptions args;
    AssetId id;
    ModelStore store;
    ModelGeneration lease;
    std::unique_ptr<ProcessSetup> process_;
    std::unique_ptr<RenderSystem> render_;
    std::unique_ptr<render::SceneResources> scene_;
    std::unique_ptr<Controller> editor_;
    std::unique_ptr<Shell> shell_;
    std::size_t import_count{};
    std::string import_baseline;
#ifdef DAE_CHARACTER_SCENE
    std::unique_ptr<CueRecorders> cue_recorders_;
    std::unique_ptr<VfxWorkspace> vfx_;
#ifdef DAE_AUDIO
    std::unique_ptr<AudioWorkspace> audio_;
#endif
#ifdef DAE_NAVIGATION
    std::unique_ptr<NavigationTool> navigation_;
#endif
    std::unique_ptr<PlaySaves> play_saves_;
#ifdef DAE_HUD
    std::unique_ptr<HudResources> hud_;
    std::unique_ptr<RuntimeInventory> inventory_;
    std::unique_ptr<RuntimeAltar> altar_;
#endif
    std::unique_ptr<PlayController> play_;
    std::unique_ptr<GameplayInspector> inspector_;
    std::unique_ptr<PosePreviews> previews_;
    std::unique_ptr<ViewportTools> tools_;
#endif
#ifdef DAE_AGENT_ENDPOINTS
    std::unique_ptr<AgentEndpoint> agent_;
    EditorDocument* agent_document_{};
#endif
    std::unique_ptr<render::SceneViewport> viewport_;
    std::chrono::steady_clock::time_point last_;
};
} // namespace editor_app
} // namespace darkangel
