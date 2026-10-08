#pragma once
#include <imgui.h>
namespace darkangel::editor_app {
enum class Icon { Save,Play,Pause,Stop,Step,Reload,Object,Folder,Info,Warning,Error };
void initialize_theme(float dpi_scale);
void set_theme_dpi(float dpi_scale);
bool icon_button(Icon,const char* label,const char* tooltip=nullptr);
void icon_text(Icon,const char* label);
inline const ImVec4 primary{.11f,.64f,.92f,1};
inline const ImVec4 attention{1,.57f,.21f,1};
}
