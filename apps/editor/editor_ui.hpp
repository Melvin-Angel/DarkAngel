#pragma once
#include "editor_controller.hpp"
#include <DirectXMath.h>
#include <imgui.h>
#include <optional>
namespace darkangel::editor_app {
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
    bool assets_workflow{};
    std::optional<Transform> draft;
    bool reload_model{},reload_shader{};
    bool native_controls{},controls_focus{},walk{};float forward{},lateral{},turn{};
private:
    AssetId script_asset;bool docked{};ViewArea area;ImDrawList* viewport_draw{};
    EditScope scope{EditScope::Placement};std::uint64_t gesture_revision{};StableId gesture_object;
    int operation{};bool dragging{},cancelled{},show_script{};char script[8192]{};char filter[128]{};
    bool import_open{};std::filesystem::path import_file;int import_type{-1};
    char canonical[512]{"animation/canonical_human.daskeleton"};bool import_loop{};
    PreparedAssetImport import_prepared;std::string import_error,import_status;
    char asset_filter[128]{};int asset_kind{};AssetId selected_asset;
};
}
