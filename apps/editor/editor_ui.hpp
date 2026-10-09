#pragma once
#include "editor_controller.hpp"
#include <DirectXMath.h>
#include <imgui.h>
#include <optional>
#include <darkangel/input.hpp>
namespace darkangel::editor_app {
enum class Workspace {Scene,Assets,Ability,Game,Tools,Settings};
enum class ToolView {Console,InputBindings,ScriptSource,RuntimePhases};
struct ViewArea {float x{280},y{90},width{700},height{500};};
class Shell {
public:
    ViewArea draw(Controller&,unsigned width,unsigned height,const char* backend,std::uint64_t generation,double frame_seconds,ImTextureID scene_texture);
    void gizmo(Controller&,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection);
    void open_import(const std::filesystem::path&,std::optional<AssetImportType> type={});
    void validate_import(Controller&);
    void commit_import(Controller&);
    bool import_validated() const{return bool(import_prepared);}
    void choose_asset_import();
    void draw_import(Controller&);
    void draw_assets(Controller&,bool expanded=false);
    void draw_authoring(Controller&,unsigned,unsigned,float);
    void draw_action_lanes(Controller&,AssetId,const nlohmann::json&);
    void cancel_composer_drag(Controller&);
    ActionBlock composer_drag_block;AssetId composer_drag_asset;
    bool composer_dragging{},composer_drag_changed{};int composer_drag_edge{};float composer_drag_x{};std::uint64_t composer_drag_revision{};
    void draw_action_preview(const nlohmann::json&);
    std::function<void(AssetId,unsigned)> prepare_composer;
    std::function<void(double)> scrub_composer;
    std::function<std::string()> composer_info;
    ImTextureID composer_texture{};bool composer_visible{};
    AssetId composer_clip;unsigned composer_duration{};std::string composer_error;
    int composer_new_kind{};
    AssetId composer_asset;unsigned composer_block{};double composer_tick{},composer_zero{};
    AssetId authored_selection,binding_ability,binding_effect;int binding_block{2};double binding_power{5};
    char author_name[65]{"new_ability"};std::string author_diagnostic;
    Workspace workspace{Workspace::Scene},previous_workspace{Workspace::Scene};ToolView tool{ToolView::Console};bool project_settings{};
    void select_workspace(Workspace);
    void open_tool(ToolView);
    void draw_game(Controller&,unsigned,unsigned,float,double,ImTextureID);
    void draw_auxiliary(Controller&,unsigned,unsigned,float);
    void draw_console(Controller&);
    InputManager* input{};std::vector<InputEvent> input_events;
    double input_processing_us{};
    bool controls_acquired{},jump{};
    std::function<std::string()> gameplay_metrics;
    std::optional<Transform> draft;
    bool reload_model{},reload_shader{};
    bool native_controls{},controls_focus{},walk{};float forward{},lateral{},turn{};
private:
    AssetId script_asset;bool docked{},game_docked{};ViewArea area;ImDrawList* viewport_draw{};
    std::vector<float> frame_ms;
    EditScope scope{EditScope::Placement};std::uint64_t gesture_revision{};StableId gesture_object;
    int operation{};bool dragging{},cancelled{},show_script{};char script[8192]{};char filter[128]{};
    bool import_open{};std::filesystem::path import_file;int import_type{-1};
    char canonical[512]{"animation/canonical_human.daskeleton"};bool import_loop{};
    PreparedAssetImport import_prepared;std::string import_error,import_status;
    char asset_filter[128]{};int asset_kind{};AssetId selected_asset;
};
}
