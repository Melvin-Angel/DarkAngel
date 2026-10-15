#pragma once
#include <imgui.h>
namespace darkangel::editor_app {
enum class Icon { Save,Play,Pause,Stop,Step,Reload,Object,Folder,Info,Warning,Error };
void initialize_theme(float dpi_scale);
void set_theme_dpi(float dpi_scale);
bool icon_button(Icon,const char* label,const char* tooltip=nullptr);
bool icon_selectable(Icon,const char* label,bool selected);
void icon_text(Icon,const char* label);
// Semantic palette shared by every workspace. Dimensions below are logical pixels;
// initialize_theme/set_theme_dpi own scaling, including the system fonts.
namespace theme {
inline const ImVec4 background{8/255.f,13/255.f,20/255.f,1};
inline const ImVec4 panel{16/255.f,23/255.f,32/255.f,1};
inline const ImVec4 raised{24/255.f,34/255.f,46/255.f,1};
inline const ImVec4 border{40/255.f,53/255.f,68/255.f,1};
inline const ImVec4 accent{22/255.f,143/255.f,255/255.f,1};
inline const ImVec4 text{230/255.f,237/255.f,245/255.f,1};
inline const ImVec4 secondary{154/255.f,168/255.f,183/255.f,1};
inline constexpr float gap=8, compact_gap=4, field_width=140;
}
// Start another toolbar item on the same row only if it fits. Use inside any
// toolbar; unlike a scrolling toolbar, all actions remain keyboard reachable.
void toolbar_next(float logical_width);
void workflow_header(const char* title,const char* description);
inline const ImVec4 primary=theme::accent;
inline const ImVec4 attention{1,.57f,.21f,1};
}
