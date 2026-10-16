#pragma once
#include <darkangel/ui_service.hpp>
#include <filesystem>
struct ImDrawList;
namespace darkangel::editor_app {
class ImGuiHudRenderer;
class FrontendUi {
public:
 FrontendUi(ImGuiHudRenderer&,UiFont,const std::filesystem::path&,const std::filesystem::path&,std::string document);
 // True when the authored Start command requests Play. The caller changes sessions after the callback batch.
 bool frame(ImDrawList*,float x,float y,float width,float height,bool focused);
 UiService service;
private:
 ImGuiHudRenderer& renderer;bool start{};
};
}
