#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>

namespace darkangel::editor_app {
std::optional<SceneBounds> Shell::scene_selection_bounds(Controller& editor) const {
    const auto entity=editor.document->world().find(editor.selected);
    if (!editor.document->world().valid(entity)) return {};
    if (scene_object_bounds) if (auto result=scene_object_bounds(editor.selected)) return result;
    const auto& pose=editor.document->world().read(entity).transform;
    return SceneBounds{{pose.x,pose.y,pose.z},std::max(.1,std::abs(pose.scale))};
}
void Shell::sync_scene_navigation(Controller& editor) {
    const auto root=editor.document->root();
    if (root!=navigation_scene) {
        if (navigation_scene!=AssetId{}) scene_cameras[navigation_scene]=scene_navigation.camera;
        navigation_scene=root;
        const auto found=scene_cameras.find(root);
        scene_navigation.camera=found==scene_cameras.end()?SceneCamera{}:found->second;
        scene_navigation.cancel();
    }
    if (workspace!=Workspace::Scene||editor.preview.active()) scene_navigation.cancel();
}
void Shell::draw_scene_navigation_toolbar(Controller& editor) {
    auto& camera=scene_navigation.camera;
    ImGui::BeginDisabled(editor.preview.active()!=nullptr||dragging);
    if (ImGui::Button(camera.mode==SceneCamera::Mode::Orbit?"Orbit":"Fly"))
        camera.set_mode(camera.mode==SceneCamera::Mode::Orbit?SceneCamera::Mode::Fly:SceneCamera::Mode::Orbit,scene_selection_bounds(editor));
    probe("scene-camera-mode");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch Orbit / Fly (Shift+F in viewport); keeps the current view");
    toolbar_next(95);
    const auto bounds=scene_selection_bounds(editor);
    ImGui::BeginDisabled(!bounds);
    if (ImGui::Button("Focus (F)")) camera.focus(*bounds,area.width/std::max(1.f,area.height));
    probe("scene-camera-focus");
    ImGui::EndDisabled();
    const bool compact=ImGui::GetWindowWidth()<560*ImGui::GetStyle().FontScaleDpi;
    const char* help="Orbit: right drag or Alt+left drag\nFly: hold right mouse + WASD, Q/E down/up, Shift faster\nMiddle drag: pan | Wheel: zoom/dolly | Fly + right mouse + wheel: speed\nF: frame selection | Shift+F: switch Orbit/Fly | Esc: release navigation\nNavigation starts inside the viewport, away from the gizmo.";
    bool settings_visible=!compact;
    if(compact){toolbar_next(115);if(ImGui::Button("Navigation..."))ImGui::OpenPopup("Scene navigation settings");settings_visible=ImGui::BeginPopup("Scene navigation settings");}
    else{toolbar_next(100);ImGui::TextDisabled("Navigation ?");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",help);}
    if(settings_visible){
        float speed=float(camera.speed);
        if(!compact)toolbar_next(220);ImGui::SetNextItemWidth(140*ImGui::GetStyle().FontScaleDpi);
        if(ImGui::SliderFloat("Speed",&speed,float(SceneCamera::min_speed),float(SceneCamera::max_speed),"%.1f m/s",ImGuiSliderFlags_Logarithmic|ImGuiSliderFlags_AlwaysClamp)&&std::isfinite(speed))camera.speed=std::clamp(double(speed),SceneCamera::min_speed,SceneCamera::max_speed);
        probe("scene-camera-speed");
        toolbar_next(100);if(ImGui::SmallButton("Reset speed"))camera.speed=5;
        if(compact){ImGui::Separator();ImGui::TextUnformatted(help);ImGui::EndPopup();}
    }
    ImGui::EndDisabled();
}
void Shell::scene_camera_matrices(float width,float height) {
    using namespace DirectX;
    const auto& camera=scene_navigation.camera;
    const auto f=camera.forward();
    const auto& p=camera.eye;
    XMStoreFloat4x4(&scene_view,XMMatrixLookToRH(XMVectorSet(float(p[0]),float(p[1]),float(p[2]),1),
        XMVectorSet(float(f[0]),float(f[1]),float(f[2]),0),XMVectorSet(0,1,0,0)));
    XMStoreFloat4x4(&scene_projection,XMMatrixPerspectiveFovRH(XM_PIDIV4,std::max(1.f,width)/std::max(1.f,height),.05f,10000.f));
    scene_camera=true;
}
void Shell::update_scene_navigation(Controller& editor,double seconds) {
    // The scene matrices are valid only for the Scene authoring route. Do not
    // leave a stopped Scene camera marked as active while Play or an isolated
    // preview owns the display target.
    scene_camera=false;
    const auto& io=ImGui::GetIO();
    SceneNavigationInput sample;
    sample.enabled=workspace==Workspace::Scene&&!editor.preview.active();
    sample.hovered=ImGui::IsItemHovered();
    sample.focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // A press on the image focuses this viewport, including on its first frame after another panel.
    if (sample.hovered&&(ImGui::IsMouseClicked(0)||ImGui::IsMouseClicked(1)||ImGui::IsMouseClicked(2))) {
        ImGui::SetWindowFocus();sample.focused=true;
    }
    sample.text=io.WantTextInput;
    sample.popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)||ImGui::GetDragDropPayload()!=nullptr;
    sample.blocked=dragging||ImGuizmo::IsUsing();
    sample.over_gizmo=ImGuizmo::IsOver();
    sample.focus_lost=io.AppFocusLost;
    sample.left=ImGui::IsMouseDown(0);sample.right=ImGui::IsMouseDown(1);sample.middle=ImGui::IsMouseDown(2);
    sample.left_pressed=ImGui::IsMouseClicked(0);sample.right_pressed=ImGui::IsMouseClicked(1);sample.middle_pressed=ImGui::IsMouseClicked(2);
    sample.alt=io.KeyAlt;sample.shift=io.KeyShift;
    sample.escape=ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    const bool frame=ImGui::IsKeyPressed(ImGuiKey_F,false)&&!io.KeyCtrl&&!io.KeyAlt&&!io.KeySuper;
    sample.toggle_mode=frame&&io.KeyShift;sample.focus_selection=frame&&!io.KeyShift;
    sample.dx=io.MouseDelta.x;sample.dy=io.MouseDelta.y;sample.wheel=io.MouseWheel;
    sample.lateral=double(ImGui::IsKeyDown(ImGuiKey_D))-double(ImGui::IsKeyDown(ImGuiKey_A));
    sample.vertical=double(ImGui::IsKeyDown(ImGuiKey_E))-double(ImGui::IsKeyDown(ImGuiKey_Q));
    sample.forward=double(ImGui::IsKeyDown(ImGuiKey_W))-double(ImGui::IsKeyDown(ImGuiKey_S));
    sample.seconds=seconds;sample.height=area.height;sample.aspect=area.width/std::max(1.f,area.height);
    scene_navigation.update(sample,scene_selection_bounds(editor));
    if (sample.enabled) scene_camera_matrices(area.width,area.height);
}
} // namespace darkangel::editor_app
