#pragma once
#include <set>
#include <functional>
#include <darkangel/camera.hpp>
#include <optional>
#include "editor_controller.hpp"
#include "prefab_commands.hpp"
#include "effect_preview.hpp"
#include "scene_camera.hpp"
#include <DirectXMath.h>
#include <imgui.h>
#include <optional>
#include <array>
#include <darkangel/input.hpp>
namespace darkangel::editor_app {
enum class Workspace {Scene,Assets,Character,Player,Npc,Ability,Animation,Game,Tools,Settings,Vfx,Audio,Items,Prefabs,Ui};
enum class ToolView {Console,InputBindings,ScriptSource,RuntimePhases,AuthoringHistory,Navigation,Saves};
struct ViewArea {float x{280},y{90},width{700},height{500};};
class Shell {
public:
    bool client_only{};
    std::string client_status;
    ViewArea draw(Controller&,unsigned width,unsigned height,const char* backend,std::uint64_t generation,double frame_seconds,ImTextureID scene_texture);
    void gizmo(Controller&,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection);
    // Level design: things that can be placed in the scene, sorted into categories from the asset catalogue itself
    // (NPC definitions, and static models grouped by their folder). New placeable asset kinds join here.
    // gameplay: 0 an asset, 1 an encounter, 2 an encounter volume, 3 a trigger volume, 4 an interactable, 5 a spline, 6 a spline point.
    struct PlaceItem {AssetId id;std::string name,category;bool npc{};int gameplay{};};
    std::vector<PlaceItem> place_items;std::size_t place_signature{};std::string place_category;char place_filter[64]{};
    // Height of the ground plane a drop lands on, and the scene camera of the latest frame for turning a drop position into a point on it.
    double place_height{.5};DirectX::XMFLOAT4X4 scene_view{},scene_projection{},game_view_second{},game_projection_second{};bool scene_camera{};
    SceneNavigation scene_navigation;
    AssetId navigation_scene;
    std::map<AssetId,SceneCamera> scene_cameras;
    std::function<std::optional<SceneBounds>(StableId)> scene_object_bounds;
    std::optional<SceneBounds> scene_selection_bounds(Controller&) const;
    void sync_scene_navigation(Controller&);
    void draw_scene_navigation_toolbar(Controller&);
    void update_scene_navigation(Controller&, double seconds);
    void scene_camera_matrices(float width, float height);
    // What the scene view submitted on its last frame: placements drawn, placements outside the view, and draw calls.
    unsigned scene_visible{}, scene_culled{}, scene_draws{};
    // The last frame's time by section (EditorApp::run): exercises and update, UI, Play, scene render, effects, present.
    std::array<double, 6> frame_sections{};
    void refresh_place_items(Controller&);
    // Places one item as a new scene object at a ground position and selects it; one document edit, so Undo removes it.
    // Without a height the object lands on the scene collision below (x, z), or at place_height where there is none.
    StableId place(Controller&,const PlaceItem&,double x,double z,double yaw=0,std::optional<double> y={});
    // First point where a segment meets the scene's cooked static collision, if the session can answer. Set by the host.
    std::function<std::optional<std::array<double,3>>(const std::array<double,3>& from,const std::array<double,3>& to)> place_surface;
    double surface_height(double x,double z)const;
    // The scene object whose bounds a segment through the scene meets first, if any. Set by the host, which knows the models.
    std::function<StableId(const std::array<double,3>& from,const std::array<double,3>& to)> pick_object;
    // Screen centres of a few named controls as last drawn, so a gate can drive them with real mouse events.
    std::map<std::string,ImVec2> probes;
    void probe(std::string key){const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();probes[std::move(key)]={(low.x+high.x)*.5f,(low.y+high.y)*.5f};}
    // Deletes a placed object of the scene's own source; one document edit, so Undo brings it back.
    void remove_object(Controller&,StableId);
    void open_import(const std::filesystem::path&,std::optional<AssetImportType> type={});
    void validate_import(Controller&);
    void commit_import(Controller&);
    bool import_validated() const{return bool(import_prepared);}
    void choose_asset_import();
    void draw_import(Controller&);
    void draw_assets(Controller&,bool expanded=false);
    void draw_authoring_history(Controller&);
    void draw_authoring(Controller&,unsigned,unsigned,float);
    // The one place a mask definition is edited: a movable, dockable window that stays open across workspaces.
    // The Player and NPC tabs open it for a mask in a slot or in their browser; a later Items tab can do the same.
    void draw_mask_editor(Controller&);AssetId mask_editor,mask_editor_raised;
    std::function<void()> open_frontend;
    AssetId graph_preview_asset,graph_character,graph_tag_registry;TagId graph_new_tag{};nlohmann::json graph_preview_tags=nlohmann::json::array();std::string graph_preview_hash;float graph_speed{},graph_forward{},graph_lateral{};std::function<void(AssetId,AssetId)> prepare_graph_pose;std::function<void()> draw_graph_info;bool show_blend_geometry{true};bool show_graph_connections{true};unsigned graph_node{},graph_new_input{},graph_point{},graph_point_node{};double graph_new_x{},graph_new_y{};int graph_new_kind{},graph_triangle[3]{0,1,2};void prepare_effect_preview(Controller&,AssetId);void draw_input_authoring(Controller&);AssetId input_authoring_asset;char input_copy_name[65]{"input_profile_copy"};char input_binding_filter[65]{};
    void draw_action_lanes(Controller&,AssetId,const nlohmann::json&);
    void cancel_composer_drag(Controller&);
    ActionBlock composer_drag_block;AssetId composer_drag_asset;
    bool composer_dragging{},composer_drag_changed{};int composer_drag_edge{};float composer_drag_x{};std::uint64_t composer_drag_revision{};
    void draw_action_preview(const nlohmann::json&);
    void draw_pose_preview();
    std::function<void(AssetId,AssetId)> prepare_character_pose;
    AssetId character_preview_asset,character_clip;std::string character_preview_hash;
    std::function<void()> draw_character_info;
    bool show_kit_slots{true};AssetId kit_copy_origin,last_created_kit;
    char kit_duplicate_name[65]{"new_kit"};
    char character_name[65]{"new_character"},player_name[65]{"new_player"},npc_name[65]{"new_npc"},npc_faction[33]{"hostile"},behavior_name[65]{"new_behavior"};
    std::function<void(AssetId,unsigned)> prepare_composer;
    std::function<void(AssetId)> prepare_action_pose;
    std::function<void(double)> scrub_composer;
    std::function<std::string()> composer_info;
    ImTextureID composer_texture{};bool composer_visible{},composer_playing{},composer_loop{};
    double composer_frame_seconds{};float composer_rate{1},composer_yaw{},composer_pitch{.1f},composer_distance{2.7f};
    AssetId composer_clip;unsigned composer_duration{};std::string composer_error;
    // Previews prepare themselves. Opening a target prepares at once; draft edits refresh after a short pause in editing.
    // A failed preparation keeps the previous preview and is not retried until its inputs change.
    struct PreviewRefresh {
        std::string pending,failed,error;double since{};bool forced{};
        bool due(const std::string& wanted,bool stale,bool immediate);
        void succeeded(){failed.clear();error.clear();pending.clear();}
        void fail(const std::string& wanted,const char* what){failed=wanted;error=what;pending.clear();}
    };
    PreviewRefresh character_refresh,graph_refresh,action_refresh,effect_refresh;
    AssetId composer_preview_action;std::string composer_preview_hash;
    // Set by a caller that gives the pose preview its own pane, so the image takes the pane's height.
    bool pose_preview_fill{};
    void preview_status(PreviewRefresh&,bool stale,const char* refresh_id);
    // Drops kept previews and their cook verification, so the next preparation reads the source files again. Set by the host.
    std::function<void()> forget_previews;
    // True while Play runs: previews need the stopped editor and resume when Play stops.
    bool previews_paused{};
    std::unique_ptr<EffectPreview> effect_preview;AssetId effect_prepare_failed_asset;std::string effect_prepare_error;AssetId effect_preview_asset;std::uint64_t effect_preview_source{1};
    std::map<std::uint32_t,std::array<char,128>> reference_filters;
    char attribute_name[97]{"NewStatistic"};int attribute_kind{};char reaction_name[65]{};char mask_name[65]{};
    // Player camera authoring preview: the scene is rendered from one drafted virtual camera.
    int camera_preview{0};float camera_preview_yaw{},camera_preview_pitch{12};bool camera_preview_visible{};bool camera_pane{};std::optional<CameraRigDefinition> camera_preview_rig;std::string camera_live;
    int composer_new_kind{};
    AssetId composer_return_ability;
    AssetId composer_asset;unsigned composer_block{};double composer_tick{},composer_zero{};
    AssetId character_selection,player_selection,npc_selection,create_skin,create_character;
    AssetId creation_ability_template,creation_effect_template;int creation_gameplay_kind{};bool creation_initialized{};bool creation_copy_action{true};char creation_ability_name[65]{"new_ability"},creation_effect_name[65]{"new_effect"};
    AssetId authored_selection,binding_ability,binding_effect;int binding_block{2};double binding_power{5};
    char history_source_filter[128]{};bool history_pending_only{};std::optional<NativeSourceComparison> last_source_comparison;std::optional<NativeValidationSummary> last_validation;
    char author_name[65]{"new_ability"};std::string author_diagnostic;
    Workspace workspace{Workspace::Scene},previous_workspace{Workspace::Scene};ToolView tool{ToolView::Console};bool project_settings{},inventory_open{};
    void select_workspace(Workspace);
    void open_native_asset(Controller&,AssetId);
    // Reveal shows the asset in Assets even when the current search or kind filter would hide it, and scrolls to it.
    void reveal_asset(AssetId id){selected_asset=id;asset_filter[0]=0;asset_kind=0;reveal_scroll=true;select_workspace(Workspace::Assets);}
    // Opens any asset in the tab that owns it: native gameplay sources through open_native_asset, sounds/profiles/effects
    // through the host. False when no editor owns the asset type (Reveal in Assets is then the route).
    bool open_asset(Controller&,AssetId);
    bool asset_has_owner(std::string_view extension) const;
    std::function<bool(AssetId,std::string_view extension)> open_host_asset;
    // Open / Edit and Reveal in Assets for a reference shown in any tab; F12 on the hovered reference opens its owner.
    void reference_routes(Controller&,AssetId,bool shortcut=true);
    bool pick_reference(const char*,AssetId&,Controller&,std::map<std::uint32_t,std::array<char,128>>&,const char*,const nlohmann::json* attributes=nullptr,const char* importer="clip-gltf-v1",bool looping_only=false);
    void open_tool(ToolView);
    void draw_game(Controller&,unsigned,unsigned,float,double,ImTextureID);
    void draw_auxiliary(Controller&,unsigned,unsigned,float);
    // Ctrl+1..9 / Ctrl+0 choose a workspace tab; Ctrl+PageUp/PageDown step through them. Not while typing, dragging or in Game input.
    void workspace_shortcuts(Controller&);
    // A tab switch chosen by the author (tab bar or shortcut): Ability and Animation restore their own last selection.
    void switch_tab(Workspace);
    // Ability and Animation share authored_selection for routes; each tab remembers its own last selection for tab switches.
    AssetId ability_memory,animation_memory;
    void draw_console(Controller&);
    void initialize_preferences(const std::filesystem::path& project,bool enabled);
    void save_preferences();
    void scale_workspace_layouts(float dpi_scale);
    void reset_layouts();
    void draw_workspace_navigation();
    void draw_editor_preferences();
    void toggle_viewport_maximize();
    bool scene_maximized{},game_maximized{};
    float ui_scale{1};
    std::array<bool,13> kit_composition_visible{};
    InputManager* input{};std::vector<InputEvent> input_events;
    double input_processing_us{};
    bool controls_acquired{},jump{};
    std::function<std::string()> gameplay_metrics;
    // Tools > Navigation: supplied by the host where the navigation library is built.
    std::function<void()> draw_navigation_tool;
    // Tools > Saves: supplied by the host, which owns Play and the save slot.
    std::function<void()> draw_saves_tool;
    // The VFX tab's left panel: supplied by the host, which owns the visual effect service. The preview beside it is the scene render target.
    std::function<void()> draw_vfx_panel;
    // The Audio tab: supplied by the host, which owns the audio service.
    std::function<void()> draw_audio_panel;
    void draw_items(Controller&,unsigned,unsigned,float);
    // R4 Prefabs workspace (prefab_workspace.cpp): browse, create and edit prefab definitions (.daprefab). Its drafts are the
    // ordinary native authoring drafts, saved by Save gameplay changes; saved definitions refresh the scenes that link them.
    void draw_prefabs(Controller&,unsigned,unsigned,float,ImTextureID scene_texture);
    // Scene Inspector section for a selected member of a linked prefab instance: placement, overrides, Revert, Apply
    // Selected, Swap, Duplicate, Delete and Open Prefab. True when it drew the object (the ordinary component forms are skipped).
    bool draw_prefab_instance(Controller&,StableId object);
    // Create Prefab from Selection for one of the scene's own objects.
    void draw_create_prefab(Controller&,StableId object);
    // R5 (material_workspace.cpp): material slots of a selected imported model (assign, create editable material, reset to
    // imported) and the form of a material instance draft. Both live in Assets.
    void draw_model_materials(Controller&,const AssetInfo& model);
    // R5 A3: what an imported texture's texels mean (colour, data or normal map), and changing it (refused while a consumer
    // needs the current meaning).
    void draw_texture_semantic(Controller&,const AssetInfo& texture);
    void draw_material_form(Controller&,AssetId material);
    void draw_material_assignments(Controller&,const nlohmann::json&,const std::function<void(AssetId,AssetId)>&,const std::function<void(AssetId)>& revert={});
    void draw_object_materials(Controller&,StableId);
    char material_name[65]{"NewMaterial"};
    AssetId material_reimport_model;
    AssetService::ModelReimportChanges material_reimport_changes;
    std::map<std::string,std::string> material_reimport_remap;
    std::string material_reimport_hash;
    // R6.1 UI workspace (ui_workspace.cpp): UI documents (.daui) with markup/style editors, sample values and a live RmlUi
    // preview that pauses during Play. ui_renderer is the HUD's ImGui render interface, set by the app.
    void draw_ui(Controller&,unsigned,unsigned,float);
    // Inspection for gates: whether the preview holds RmlUi, an element's text, a click on an element, the command log.
    bool ui_preview_alive()const;std::string ui_preview_problems()const;std::string ui_preview_text(const std::string& id)const;bool ui_preview_click(const std::string& id);std::vector<std::string> ui_preview_commands()const;
    AssetId ui_selection;char ui_name[65]{"NewDocument"};int ui_template{};void* ui_renderer{};std::shared_ptr<struct UiWorkspaceState> ui_state;
    AssetId prefab_selection,prefab_swap_target;char prefab_name[65]{"NewPrefab"},prefab_filter[65]{};bool prefab_swap_defaults{};std::set<std::string> prefab_override_selection;
    MaterialSwap prefab_material_swap;std::string prefab_material_swap_key;
    // R4 Prefabs workspace preview: the selected definition (draft) resolved on its own, or in context at one of the open scene's
    // instances with the surrounding scene models shown but not editable. prefab_workspace.cpp builds it when its inputs change;
    // the scene viewport draws it, from an orbit camera, while the Prefabs tab is open.
    struct PrefabPreviewModel {StableId object;AssetId model;Transform pose;bool context{};std::map<AssetId,AssetId> materials;};
    struct PrefabPreview {std::string key,error;std::vector<PrefabPreviewModel> models;std::array<double,3> center{};double radius{2};float yaw{.7f},pitch{.35f};bool visible{},in_context{};};
    PrefabPreview prefab_preview;
    // Consumers of the selected prefab (scanned when the selection or a Save changes them), rename and variant names, the nested
    // prefab chosen to add, the objects captured for a multi-object Create Prefab from Selection, and expanded outliner instances.
    std::string mount_delete; // an instance the outliner deletes after drawing its rows
    AssetId prefab_consumers_for;std::vector<PrefabConsumer> prefab_consumer_list;bool prefab_consumers_stale{true};
    char prefab_rename[65]{},prefab_variant_name[65]{"NewVariant"};AssetId prefab_nested_choice;
    std::vector<StableId> prefab_capture;std::set<std::string> outliner_expanded;
    AssetId item_selection,item_create_mask;int item_create_kind{};char item_name[129]{"New item"},item_filter[129]{};
    std::function<void()> draw_gameplay_inspector;bool focus_gameplay_inspector{},select_effect_debug{};
    // Host hook drawn over the Game viewport image while Play is active (runtime HUD).
    std::function<void(ImDrawList*,ViewArea)> game_overlay;
    // The project's scenes and their entry points, as the Scene transition inspector lists them.
    struct TravelScene {AssetId id;std::string path;std::vector<std::pair<StableId,std::string>> entries;};
    // Local players of the next Play (1 to 4), and the second player's view while a Play has one: its image, area and
    // overlay. controls_player is the player the keyboard and mouse drive: the one whose viewport was clicked.
    int players{1};bool second_view{};ImTextureID game_image_second{};ViewArea area_second;std::function<void(ImDrawList*,ViewArea)> game_overlay_second;int controls_player{};unsigned game_docked_views{};
    // World-space debug drawing over the Scene and Game viewports (NPC senses).
    std::function<void(ImDrawList*,ViewArea)> debug_overlay;
    std::optional<Transform> draft;
    bool reload_model{},reload_shader{};
    bool native_controls{},controls_focus{},walk{};float forward{},lateral{},turn{};
private:
    Workspace navigation_workspace{Workspace::Scene};
    std::filesystem::path preferences_file;
    std::string preferences_status;
    std::string restored_layout;
    double preferences_saved_at{};
    float layout_dpi{1},restored_layout_dpi{1};
    ImGuiID scene_viewport_dock{},game_viewport_dock{};
    char console_filter[128]{};
    bool console_info{true},console_warning{true},console_error{true};
    AssetId script_asset;bool docked{},game_docked{};ViewArea area;ImDrawList* viewport_draw{};
    std::vector<float> frame_ms;
    EditScope scope{EditScope::Placement};std::uint64_t gesture_revision{};StableId gesture_object;
    int operation{};bool dragging{},cancelled{},show_script{};char script[8192]{};char filter[128]{};
    bool import_open{};std::filesystem::path import_file;int import_type{-1};int import_semantic{}; // texture meaning (R5 A3): 0 colour, 1 data, 2 normal
    char canonical[512]{"animation/canonical_human.daskeleton"};bool import_loop{};
    PreparedAssetImport import_prepared;std::string import_error,import_status;
    char asset_filter[128]{};int asset_kind{};AssetId selected_asset;bool reveal_scroll{};
};
// The audio profile this scene names (its one object with an "audio" component), and setting it: an ordinary undoable scene edit.
AssetId scene_audio_profile(const Controller&);
// Points a placed scene transition trigger at a destination scene and entry point (nil: the scene's own start) as one scene edit.
void set_scene_transition(Controller&,StableId object,AssetId scene,StableId entry,bool interact,std::string_view prompt="Travel",double radius=2);
void set_scene_audio_profile(Controller&,AssetId profile);
}
