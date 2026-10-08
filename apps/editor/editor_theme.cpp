#define NOMINMAX
#include <Windows.h>
#include "editor_theme.hpp"
#include <filesystem>
#include <array>
#include <algorithm>
namespace darkangel::editor_app {
namespace {
ImFont* icons{};ImGuiStyle base;
constexpr std::array<ImWchar,11> codes{0xe74e,0xe768,0xe769,0xe71a,0xe893,0xe72c,0xe950,0xe8b7,0xe946,0xe7ba,0xea39};
// Microsoft Segoe Fluent glyph reference. Keep the system font separate from
// normal text and never package it. GDI checks actual installed glyph coverage.
bool covered(){auto dc=CreateCompatibleDC(nullptr);auto font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");auto previous=SelectObject(dc,font);WORD glyphs[11]{};auto result=GetGlyphIndicesW(dc,reinterpret_cast<const wchar_t*>(codes.data()),static_cast<int>(codes.size()),glyphs,GGI_MARK_NONEXISTING_GLYPHS);SelectObject(dc,previous);DeleteObject(font);DeleteDC(dc);if(result==GDI_ERROR)return false;for(auto g:glyphs)if(g==0xffff || g==0)return false;return true;}
void draw_icon(Icon icon,ImVec2 position,ImVec4 color){if(!icons)return;char utf8[4]{};auto cp=codes[static_cast<unsigned>(icon)];utf8[0]=static_cast<char>(0xe0|(cp>>12));utf8[1]=static_cast<char>(0x80|((cp>>6)&63));utf8[2]=static_cast<char>(0x80|(cp&63));ImGui::PushFont(icons,ImGui::GetStyle().FontSizeBase);ImGui::GetWindowDrawList()->AddText(position,ImGui::GetColorU32(color),utf8);ImGui::PopFont();}
}
void set_theme_dpi(float scale){static float applied{};if(applied==scale)return;applied=scale;auto& s=ImGui::GetStyle();s=base;s.ScaleAllSizes(scale);s.FontScaleDpi=scale;}
void initialize_theme(float scale){
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.FontSizeBase=16;s.WindowRounding=3;s.ChildRounding=2;s.FrameRounding=s.GrabRounding=2.3f;s.TabRounding=2;s.WindowBorderSize=s.ChildBorderSize=1;s.FrameBorderSize=0;s.PopupBorderSize=1;s.WindowPadding={12,10};s.FramePadding={8,4};s.ItemSpacing={8,6};s.ItemInnerSpacing={6,4};s.CellPadding={6,4};s.ScrollbarSize=12;s.WindowMinSize={180,100};
    auto* c=s.Colors;c[ImGuiCol_Text]={.93f,.94f,.95f,1};c[ImGuiCol_TextDisabled]={.58f,.61f,.64f,1};c[ImGuiCol_WindowBg]={.13f,.14f,.15f,1};c[ImGuiCol_ChildBg]={.12f,.13f,.14f,1};c[ImGuiCol_PopupBg]={.14f,.15f,.16f,.98f};c[ImGuiCol_Border]={.27f,.28f,.30f,.7f};
    c[ImGuiCol_TitleBg]=c[ImGuiCol_TitleBgActive]=c[ImGuiCol_TitleBgCollapsed]=c[ImGuiCol_MenuBarBg]=c[ImGuiCol_Tab]=c[ImGuiCol_TabDimmed]={.08f,.08f,.09f,1};
    c[ImGuiCol_FrameBg]=c[ImGuiCol_Button]={.25f,.25f,.25f,1};c[ImGuiCol_FrameBgHovered]=c[ImGuiCol_ButtonHovered]={.31f,.33f,.35f,1};c[ImGuiCol_FrameBgActive]=c[ImGuiCol_ButtonActive]={.15f,.40f,.55f,1};c[ImGuiCol_Header]={.16f,.30f,.39f,1};c[ImGuiCol_HeaderHovered]={.18f,.40f,.53f,1};c[ImGuiCol_HeaderActive]={.11f,.49f,.70f,1};c[ImGuiCol_TabHovered]={.15f,.40f,.55f,1};c[ImGuiCol_TabSelected]={.19f,.23f,.26f,1};c[ImGuiCol_TabDimmedSelected]={.15f,.18f,.20f,1};
    for(auto key:{ImGuiCol_CheckMark,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive,ImGuiCol_NavCursor,ImGuiCol_TabSelectedOverline,ImGuiCol_DockingPreview,ImGuiCol_ResizeGripHovered,ImGuiCol_ResizeGripActive})c[key]=primary;c[ImGuiCol_TextSelectedBg]={.11f,.64f,.92f,.30f};c[ImGuiCol_Separator]={.28f,.30f,.32f,1};c[ImGuiCol_SeparatorHovered]=c[ImGuiCol_SeparatorActive]=primary;
    auto& io=ImGui::GetIO();if(std::filesystem::exists(L"C:/Windows/Fonts/segoeui.ttf"))io.FontDefault=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf",16);else io.FontDefault=io.Fonts->AddFontDefault();
    if(std::filesystem::exists(L"C:/Windows/Fonts/SegoeIcons.ttf") && covered()){static ImWchar ranges[23]{};auto sorted=codes;std::sort(sorted.begin(),sorted.end());for(std::size_t i=0;i<sorted.size();++i)ranges[i*2]=ranges[i*2+1]=sorted[i];ImFontConfig config;config.MergeMode=false;icons=io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/SegoeIcons.ttf",16,&config,ranges);}
    base=s;set_theme_dpi(scale);
}
bool icon_button(Icon icon,const char* label,const char* tooltip){auto position=ImGui::GetCursorScreenPos();std::string text=icons?std::string("     ")+label:label;auto pressed=ImGui::Button(text.c_str());if(icons)draw_icon(icon,{position.x+ImGui::GetStyle().FramePadding.x,position.y+ImGui::GetStyle().FramePadding.y},ImGui::GetStyleColorVec4(ImGuiCol_Text));if(tooltip && ImGui::IsItemHovered())ImGui::SetTooltip("%s",tooltip);return pressed;}
void icon_text(Icon icon,const char* label){if(icons){auto position=ImGui::GetCursorScreenPos();draw_icon(icon,position,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::Dummy({ImGui::GetFontSize(),ImGui::GetFontSize()});ImGui::SameLine();}ImGui::TextUnformatted(label);}
bool icon_selectable(Icon icon,const char* label,bool selected){auto pos=ImGui::GetCursorScreenPos();std::string text=icons?std::string("     ")+label:label;auto result=ImGui::Selectable(text.c_str(),selected);if(icons)draw_icon(icon,pos,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));return result;}
}
