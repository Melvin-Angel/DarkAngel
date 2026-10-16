#pragma once
#include <darkangel/scene_transition.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/static_collision.hpp>
#include "cue_debug.hpp"
#include "native_input.hpp"
#include "primitive_cue_sink.hpp"
#include "vfx_cue_sink.hpp"
#include "exercises/script_runner.hpp"
#include <darkangel/camera.hpp>
#include <darkangel/cue_assets.hpp>
#include <darkangel/editor_document.hpp>
#include <darkangel/input.hpp>
#include <darkangel/world_ledger.hpp>
#ifdef DAE_AUDIO
#include "audio_workspace.hpp"
#endif
#include <nlohmann/json.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct ImDrawList;
namespace darkangel {
class CharacterSceneSession;
struct RuntimeSkinnedModel;
class PlayerUi;
class UiService;
class VfxService;
struct VfxView;
namespace editor_app {
class Controller;
class Shell;
class VfxWorkspace;
class AudioWorkspace;
class AudioCueSink;
class PlaySaves;
class RuntimeInventory;
class RuntimeAltar;
class PlayUi;
class FrontendUi;
class ImGuiHudRenderer;
class EditorPartyHost;
class EditorPartyGuest;
struct NavigationTool;
struct HudResources;
struct EditorOptions;
struct ViewArea;
struct CharacterPreviewResources;
struct PreparedSpawn;
namespace render {
struct UploadedModel;
}

// The editor's scene provider: Play's own preparation, for the scene the party travels to.
struct EditorSceneProvider final : darkangel::SceneProvider {
    std::function<void(AssetId)> on_begin;
    std::function<darkangel::SceneLoadStatus()> on_poll;
    std::function<std::unique_ptr<darkangel::PreparedScene>()> on_take;
    std::function<void()> on_cancel;
    void begin(AssetId scene) override {
        on_begin(scene);
    }
    darkangel::SceneLoadStatus poll() override {
        return on_poll();
    }
    std::unique_ptr<darkangel::PreparedScene> take() override {
        return on_take();
    }
    void cancel() override {
        on_cancel();
    }
};

// What Play's cue presentation holds, read by the Gameplay Inspector's Cues tab and the cue exercise.
struct PlayCueState {
    const std::unique_ptr<CuePlayer>& player;
    const std::optional<std::vector<CueDefinition>>& cues;
    const std::size_t& closure;
    const RecordingCueSink& visual;
    const RecordingCueSink& audio;
    const PrimitiveCueSink& primitives;
    const VfxCueSink& effects;
    const std::map<std::string, std::string>& effect_generations;
    const std::unique_ptr<VfxService>& vfx;
    const std::string& vfx_failed;
    // Null in builds without audio.
    const AudioWorkspace* audio_workspace{};
};

// What Play needs from the rest of the editor. The workspaces, tools, saves and HUD resources outlive the controller.
struct PlayContext {
    // Preparing Play rewrites the registry, store and Play selection options the session is built from.
    EditorOptions& options;
    Controller& editor;
    Shell& shell;
    VfxWorkspace& vfx;
    // Null in builds without audio, navigation or a runtime HUD.
    AudioWorkspace* audio{};
    NavigationTool* navigation{};
    HudResources* hud{};
    CueRecorders& recorders;
    std::unique_ptr<PlaySaves>& saves;
    RuntimeInventory& inventory;
    RuntimeAltar& altar;
    // The window procedure's native input adapter, replaced with the controls.
    NativeInput*& window_input;
    // Closes the editor window (a guest whose host ended the party).
    std::function<void()> close_window;
    // Packages and uploads what a fresh Play needs; the device side belongs to the editor.
    std::function<void()> reload_model;
    // The scene models the editor has uploaded; a travel preparation packages them again.
    const std::map<AssetId, render::UploadedModel>& scene_models;
};

// Play in the editor: start and stop, the running character session and the transition coordinator that owns it, the
// party host or guest, save hook-up, Play controls and scripted input, cameras, cue presentation, the runtime HUD and the
// party diagnostic receipt. Authoring UI reads it through narrow views; it never draws authoring panels.
class PlayController {
  public:
    // The open scene's static collision as Play would build it now (authored, or the placements' static layer); null without Play
    // resources. Viewport placement and navigation query it.
    std::shared_ptr<const darkangel::CollisionDefinition> static_collision();
    explicit PlayController(PlayContext);
    ~PlayController();
    PlayController(const PlayController&) = delete;
    PlayController& operator=(const PlayController&) = delete;

    // Play resources for --character-clips, and Controller's Play callbacks. skins: how many skins the editor loaded;
    // skin: the one Play's characters use.
    void start(std::size_t skins, const RuntimeSkinnedModel* skin);
    // How Play packages, bakes and prepares the scene it starts or travels to.
    void install_preparation();
    // Frame 0: the --world-place gate authoring, --play-from-save and the Play trigger.
    void launch(unsigned frame);
    // After the UI: the controls' device samples (or a fixed-step exercise's pattern) through the InputManager.
    void poll_input(unsigned frame);
    // Play's frame: look and movement, scripts, the second local player, the party, the session, travel and saves.
    void frame(unsigned frame, double elapsed_seconds);
    // The Game camera this frame, or null when Play is not shown there.
    const CameraPose* game_camera(double elapsed_seconds);
    // The second local player's camera.
    const CameraPose& second_camera(double elapsed_seconds);
    // Play's visual effects, trails and decals, advanced on the session's tick clock and drawn against the scene depth.
    void present_effects(const VfxView& shown);
    // Whether a scripted quit can end the run now: it waits for a coherent final view.
    bool quit_ready() const;
    void party_receipt(unsigned frame);

    // Reloading resources: candidates are made while the old ones are still used, then committed at GPU idle.
    struct Reload {
        std::unique_ptr<CharacterPreviewResources> resources;
        std::unique_ptr<InputManager> inputs;
        std::unique_ptr<NativeInput> native;
    };
    Reload prepare_reload(std::size_t skins, const RuntimeSkinnedModel* skin);
    void commit_reload(Reload);

#ifdef DAE_HUD
    UiService* authored_ui();
    UiService* frontend_service();
#endif
    // Exercises act on the scene's first spawned actor.
    StableId first_actor();
    // HUD target: the soft-locked actor if there is one, else the nearest live hostile in front of the camera.
    std::optional<StableId> choose_hud_target();
    PlayCueState cue_state() const;

    std::unique_ptr<InputManager> inputs;
    std::unique_ptr<NativeInput> native_input;
    std::optional<std::vector<darkangel::CueDefinition>> play_cues;
    std::size_t play_cue_closure{};
    std::map<std::string, AssetId> play_effects;
    std::map<std::string, std::string> play_effect_generations;
    darkangel::editor_app::PrimitiveCueSink cue_primitives;
    std::map<AssetId, darkangel::TrailDefinition> play_trails;
    std::map<AssetId, darkangel::DecalDefinition> play_decals;
    darkangel::editor_app::VfxCueSink cue_effects;
    std::uint64_t vfx_tick{};
    std::unique_ptr<darkangel::CuePlayer> cue_player;
    // Further local players of this Play, the second one's camera, and the scripts that drive the players.
    std::unique_ptr<CameraDirector> director_second;
    std::vector<StableId> companions;
    darkangel::editor_app::exercises::ScriptRunner companion_script, owner_script;
    darkangel::editor_app::exercises::ScriptSignals script_signals;
    std::uint64_t guest_arrived_epoch{};
    // Presentation-only camera brain for the Play actor; rebuilt for each Play session.
    std::unique_ptr<CameraDirector> director;
    std::vector<std::string> camera_held;
    std::vector<TagId> camera_tags;
    // Play's scene session belongs to the transition coordinator, which replaces it when the party travels; character is the
    // running session. The world ledger lives as long as one Play. While a Play travels, the editor's document, Play resources
    // and uploaded models are those of globals_scene, and the other scenes' documents (the authoring one among them) are parked.
    darkangel::WorldLedger play_ledger;
    EditorSceneProvider scene_provider;
    std::function<void(std::string)> save_play;
#ifdef DAE_EDITOR_GNS
    std::unique_ptr<darkangel::editor_app::EditorPartyHost> party_host;
    std::unique_ptr<darkangel::editor_app::EditorPartyGuest> party_guest;
#endif
    std::unique_ptr<darkangel::SceneTransitionCoordinator> travel;
    CharacterSceneSession* character{};
    StableId character_player;
    using SpawnController = std::function<std::shared_ptr<darkangel::ActorController>(const darkangel::editor_app::PreparedSpawn&,
                                                                                      const darkangel::CollisionDefinition&)>;
    std::function<SpawnController()> make_controller;
    AssetId globals_scene, authoring_scene;
    std::filesystem::path authoring_path;
    std::map<AssetId, std::unique_ptr<darkangel::EditorDocument>> parked_documents;
    std::map<AssetId, std::filesystem::path> parked_paths;
    bool travel_preparing = false, travel_was_busy = false;
    struct TravelLoad {
        AssetId scene;
        unsigned step{};
        bool parked{};
        std::filesystem::path path;
        std::unique_ptr<darkangel::EditorDocument> document;
        std::unique_ptr<darkangel::PreparedScene> prepared;
    } travel_load;
    std::unique_ptr<darkangel::SceneSession::Prepared> travel_preview;
    unsigned travel_arrivals{}, travel_failures{}, travel_overlay_frames{};
    std::string travel_failure;
    std::unique_ptr<darkangel::editor_app::CharacterPreviewResources> character_resources;
    CharacterSceneInput character_input;
    std::vector<InputEvent> combat_events;
    // Navigation and behaviour facts of the latest Play session, for readouts.
    unsigned navigation_polygons{};
    std::string behavior_running;
    // HUD target: a presentation choice that writes no gameplay state and is separate from soft lock and from any lock-on.
    std::optional<StableId> hud_target;
    unsigned interactions_asked = 0;
    bool interact_pressed = false, interact_held = false, interaction_offered = false;

  private:
    std::shared_ptr<const darkangel::CollisionDefinition> scene_static_;
    darkangel::CollisionGeometryCache package_geometry_;
    const void* scene_static_document_ = nullptr;
    const void* scene_static_resources_ = nullptr;
    std::uint64_t scene_static_revision_ = 0;
    StableId scene_role(const std::string& value);
    std::map<StableId, AssetId> registry_spawns(const std::filesystem::path& path);
    std::set<AssetId> encounter_npcs();
    std::set<AssetId> registry_encounter_npcs(const std::filesystem::path& path);
    std::unique_ptr<CharacterPreviewResources> load_resources(const RuntimeSkinnedModel& skin);
    std::vector<darkangel::CueDefinition> load_play_cues();
    void prepare_gameplay(const SpawnPlan& plan, const World& world);
    void attach_session(const SpawnPlan& plan);
    void stop_gameplay();
    void update_gameplay(double seconds);
    AssetId authored_collision() const;
    std::filesystem::path find_scene(AssetId scene);
    void enter_scene(AssetId scene, std::unique_ptr<darkangel::EditorDocument> document, const std::filesystem::path& path);
    void drop_load();
    darkangel::SceneLoadStatus poll_load();
#ifdef DAE_HUD
    PlayUi& ensure_ui(bool second=false);
    void draw_hud(ImDrawList* draw, const ViewArea& area);
    std::unique_ptr<PlayUi> play_ui,play_ui_second;
    std::unique_ptr<RuntimeInventory> inventory_second;
    std::unique_ptr<RuntimeAltar> altar_second;
    std::unique_ptr<FrontendUi> frontend;
    const CharacterPreviewResources* ui_resources{};
    std::filesystem::path ui_registry;
    void draw_second_hud(ImDrawList* draw, const ViewArea& area);
#endif

    // References keep the names the moved code used, so every stored callback reaches the owner's object, never a local.
    EditorOptions& args;
    Controller& editor;
    Shell& shell;
    std::unique_ptr<VfxService>& vfx;
    std::string& vfx_failed;
#ifdef DAE_AUDIO
    AudioWorkspace& audio_workspace;
    std::unique_ptr<AudioService>& audio;
    std::map<std::string, AssetId>& play_sounds;
    AudioCueSink& cue_sounds;
    std::string& audio_failed;
    std::unique_ptr<AudioDirector>& audio_director;
    std::map<AssetId, AudioProfile>& play_profiles;
    std::map<StableId, AssetId>& encounter_profiles;
    std::optional<AudioMix>& audio_applied;
    bool& audio_in_play;
    AudioWorkspace::State& audio_tab;
#endif
    NavigationTool* navigation_;
#ifdef DAE_HUD
    ImGuiHudRenderer& hud_renderer;
    PlayerUi*& hud;
    bool& hud_failed;
    bool& hud_second_drawn;
#endif
    RecordingCueSink& cue_visual;
    RecordingCueSink& cue_audio;
    std::unique_ptr<PlaySaves>& play_saves;
    RuntimeInventory& runtime_inventory;
    RuntimeAltar& runtime_altar;
    NativeInput*& window_input;
    std::function<void()> close_window_, reload_model_;
    const std::map<AssetId, render::UploadedModel>& scene_models_;
    // What a stopped process last saw while it ran stays in its final receipt.
    nlohmann::json party_last_running, party_fullest;
};
} // namespace editor_app
} // namespace darkangel
