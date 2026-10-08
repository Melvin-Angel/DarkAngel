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
    std::optional<Transform> draft;
    bool reload_model{},reload_shader{};
private:
    AssetId script_asset;bool docked{};ViewArea area;ImDrawList* viewport_draw{};
    EditScope scope{EditScope::Placement};std::uint64_t gesture_revision{};StableId gesture_object;
    int operation{};bool dragging{},cancelled{},show_script{};char script[8192]{};char filter[128]{};
};
}
