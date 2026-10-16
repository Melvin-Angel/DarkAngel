#include "frontend_ui.hpp"
#include "hud_overlay.hpp"
#include <darkangel/ui_assets.hpp>
#include <imgui.h>
#include <algorithm>
namespace darkangel::editor_app {
FrontendUi::FrontendUi(ImGuiHudRenderer& r,UiFont font,const std::filesystem::path& registry,const std::filesystem::path& cas,std::string document)
 :service(r,std::move(font),[registry,cas](auto id){auto cooked=load_cooked_ui_document(registry,cas,AssetId::parse(id));UiDocumentSource result;
 result.binding=cooked.definition.binding;result.surfaces=cooked.definition.surfaces;result.markup=cooked.definition.markup;result.style=cooked.definition.style;result.script=cooked.definition.script;
 for(auto& [family,bytes]:cooked.fonts)result.fonts.push_back({family,std::move(bytes)});
 for(auto& image:cooked.images)result.images.push_back({image.url,{image.width,image.height,std::move(image.rgba)}});return result;
 },[this](auto kind,auto value){if(kind=="play"&&value.empty()){start=true;return true;}return false;}),renderer(r){service.open(document,UiService::Input::Modal,"frontend",1280,720);}
bool FrontendUi::frame(ImDrawList* draw,float x,float y,float width,float height,bool focused){
 service.resize_screens(std::max(1,int(width)),std::max(1,int(height)));
 if(focused){
 auto key=[&](ImGuiKey a,ImGuiKey b,UiKey k){if(ImGui::IsKeyPressed(a,false)||ImGui::IsKeyPressed(b,false))service.key(k);};
 key(ImGuiKey_UpArrow,ImGuiKey_GamepadDpadUp,UiKey::Up);key(ImGuiKey_DownArrow,ImGuiKey_GamepadDpadDown,UiKey::Down);
 key(ImGuiKey_Enter,ImGuiKey_GamepadFaceDown,UiKey::Confirm);key(ImGuiKey_Escape,ImGuiKey_GamepadFaceRight,UiKey::Cancel);
 const auto mouse=ImGui::GetIO().MousePos;service.pointer(int(mouse.x-x),int(mouse.y-y),ImGui::IsMouseClicked(0),ImGui::IsMouseReleased(0));}
 service.update();service.render([&](auto& view,float left,float top,float w,float h){renderer.begin(draw,{x+left,y+top},{w,h});view.render();renderer.end();});return start;
}
}
