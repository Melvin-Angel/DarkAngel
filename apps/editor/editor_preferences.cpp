#define NOMINMAX
#include <Windows.h>
#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include <imgui_internal.h>
#include <darkangel/hash.hpp>
#include <fstream>
#include <cstdlib>
#include <cmath>

namespace darkangel::editor_app {
namespace {
std::string environment_value(const char* key){char* value{};std::size_t size{};if(_dupenv_s(&value,&size,key))return {};std::string result=value?value:"";std::free(value);return result;}
}
void Shell::initialize_preferences(const std::filesystem::path& project,bool enabled){
    // Hidden exercises opt out unless an explicit isolated preference file is supplied.
    if(const auto override_path=environment_value("DAE_EDITOR_PREFERENCES");!override_path.empty())preferences_file=override_path;
    else if(enabled){if(const auto local=environment_value("LOCALAPPDATA");!local.empty())
        preferences_file=std::filesystem::path(local)/"DarkAngel"/"Editor"/(sha256(std::filesystem::absolute(project.empty()?".":project).lexically_normal().generic_string()).substr(0,16)+"-v1.json");}
    layout_dpi=ImGui::GetStyle().FontScaleDpi;
    if(preferences_file.empty())return;
    try{
        if(!std::filesystem::exists(preferences_file))return;
        if(std::filesystem::file_size(preferences_file)>2*1024*1024)throw std::runtime_error("Preference file exceeds 2 MiB");
        std::ifstream file(preferences_file);const auto value=nlohmann::json::parse(file);
        if(value.at("version")!=1)throw std::runtime_error("Unsupported preference version");
        // Validate the whole envelope before applying anything. Dock data is ImGui-owned.
        auto layout=value.at("layout").get<std::string>();
        const auto saved_dpi=value.value("layout_dpi",1.f);if(!std::isfinite(saved_dpi)||saved_dpi<.5f||saved_dpi>8)throw std::runtime_error("Invalid layout DPI");
        const auto scale=value.value("ui_scale",1.f);if(!std::isfinite(scale)||scale<.75f||scale>2.f)throw std::runtime_error("Invalid interface scale");
        auto kit_panels=value.value("kit_panels",std::array<bool,13>{});
        auto info=value.at("console_info").get<bool>(),warning=value.at("console_warning").get<bool>(),error=value.at("console_error").get<bool>();
        if(!layout.empty()&&(!layout.starts_with("[Window][")||layout.find("[Docking][Data]")==std::string::npos))throw std::runtime_error("Invalid workspace layout");
        kit_composition_visible=kit_panels;restored_layout_dpi=layout_dpi=saved_dpi;ui_scale=scale;restored_layout=std::move(layout);console_info=info;console_warning=warning;console_error=error;
        if(!restored_layout.empty())ImGui::LoadIniSettingsFromMemory(restored_layout.c_str(),restored_layout.size());
        docked=ImGui::DockBuilderGetNode(ImHashStr("DarkAngelSceneDock"))!=nullptr;
        game_docked=ImGui::DockBuilderGetNode(ImHashStr("DarkAngelGameDock"))!=nullptr;
        game_docked_views=1;preferences_status="Restored editor layout.";
    }catch(const std::exception& error){preferences_status=std::string("Preferences ignored; using defaults: ")+error.what();}
}
void Shell::scale_workspace_layouts(float dpi_scale){
    if(std::abs(dpi_scale-layout_dpi)<.001f)return;
    const float ratio=dpi_scale/layout_dpi;
    // ImGui keeps leaf split sizes in physical pixels. Scale those references as
    // well as style/text so restored sidebars do not shrink at a new DPI.
    for(auto& entry:ImGui::GetCurrentContext()->DockContext.Nodes.Data){
        auto* node=static_cast<ImGuiDockNode*>(entry.val_p);if(!node)continue;
        node->SizeRef={node->SizeRef.x*ratio,node->SizeRef.y*ratio};
        node->Size={node->Size.x*ratio,node->Size.y*ratio};
    }
    layout_dpi=dpi_scale;
}
void Shell::save_preferences(){
    if(preferences_file.empty())return;
    try{
        // Fullscreen is temporary. Preserve the last docked layout while maximized.
        if(!scene_maximized&&!game_maximized){std::size_t size{};const auto* ini=ImGui::SaveIniSettingsToMemory(&size);restored_layout.assign(ini,size);restored_layout_dpi=layout_dpi;}
        nlohmann::json value{{"version",1},{"kit_panels",kit_composition_visible},{"ui_scale",ui_scale},{"layout_dpi",restored_layout_dpi},{"layout",restored_layout},{"console_info",console_info},{"console_warning",console_warning},{"console_error",console_error}};
        std::filesystem::create_directories(preferences_file.parent_path());auto temporary=preferences_file;temporary+="."+std::to_string(GetCurrentProcessId())+".tmp";
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file<<value.dump(2)<<'\n';file.flush();if(!file)throw std::runtime_error("Cannot write preferences");}
        if(!MoveFileExW(temporary.c_str(),preferences_file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace preferences");
        preferences_saved_at=ImGui::GetTime();preferences_status="Editor preferences saved.";
    }catch(const std::exception& error){preferences_status=std::string("Preferences could not be saved: ")+error.what();}
}
void Shell::reset_layouts(){
    scene_maximized=game_maximized=false;scene_viewport_dock=game_viewport_dock=0;
    ImGui::ClearIniSettings();docked=game_docked=false;restored_layout.clear();
    preferences_status="Default layouts restored. Drafts and scene data are unchanged.";
}
void Shell::toggle_viewport_maximize(){
    auto& maximized=workspace==Workspace::Game?game_maximized:scene_maximized;
    auto& dock=workspace==Workspace::Game?game_viewport_dock:scene_viewport_dock;
    if(!maximized){save_preferences();dock=ImGui::GetWindowDockID();}
    maximized=!maximized;
    scene_navigation.cancel();controls_focus=false;
    if(!maximized&&dock)ImGui::SetWindowDock(ImGui::GetCurrentWindow(),dock,ImGuiCond_Always);
}
namespace {
struct Destination{const char* name;Workspace workspace;};
constexpr Destination destinations[]={{"Scene / Level Design",Workspace::Scene},{"Assets",Workspace::Assets},{"Character",Workspace::Character},{"Player",Workspace::Player},{"NPC",Workspace::Npc},{"Ability",Workspace::Ability},{"Animation",Workspace::Animation},{"VFX",Workspace::Vfx},{"Audio",Workspace::Audio},{"Items",Workspace::Items},{"Game",Workspace::Game},{"Prefabs",Workspace::Prefabs},{"UI",Workspace::Ui}};
}
void Shell::switch_tab(Workspace target){
    if(workspace==target)return;
    select_workspace(target);
    if(target==Workspace::Ability&&ability_memory!=AssetId{})authored_selection=ability_memory;
    else if(target==Workspace::Animation&&animation_memory!=AssetId{})authored_selection=animation_memory;
}
void Shell::workspace_shortcuts(Controller& editor){
    const auto& io=ImGui::GetIO();
    if(!io.KeyCtrl||io.KeyAlt||io.WantTextInput||controls_focus||dragging||composer_dragging||ImGui::IsAnyItemActive()||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;
    (void)editor;
    constexpr ImGuiKey digits[]={ImGuiKey_1,ImGuiKey_2,ImGuiKey_3,ImGuiKey_4,ImGuiKey_5,ImGuiKey_6,ImGuiKey_7,ImGuiKey_8,ImGuiKey_9,ImGuiKey_0};
    for(std::size_t i=0;i<std::size(digits);++i)if(ImGui::IsKeyPressed(digits[i],false)){switch_tab(destinations[i].workspace);return;}
    if(ImGui::IsKeyPressed(ImGuiKey_G,false)){switch_tab(Workspace::Game);return;}
    const bool next=ImGui::IsKeyPressed(ImGuiKey_PageDown,false),previous=ImGui::IsKeyPressed(ImGuiKey_PageUp,false);
    if(!next&&!previous)return;
    const auto current=workspace==Workspace::Tools||workspace==Workspace::Settings?navigation_workspace:workspace;
    const auto count=std::size(destinations);std::size_t at=0;
    for(std::size_t i=0;i<count;++i)if(destinations[i].workspace==current)at=i;
    switch_tab(destinations[(at+(next?1:count-1))%count].workspace);
}
void Shell::draw_workspace_navigation(){
    const auto requested=workspace;
    const bool external=requested!=navigation_workspace||requested==Workspace::Tools||requested==Workspace::Settings;
    if(ImGui::BeginTabBar("Workspaces",ImGuiTabBarFlags_FittingPolicyScroll|ImGuiTabBarFlags_TabListPopupButton|ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)){
        for(const auto& target:destinations){
            // Selection also follows Open/Edit routes from other workflows.
            const auto flags=external&&requested==target.workspace?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
            const bool open=ImGui::BeginTabItem(target.name,nullptr,flags);
            if(ImGui::IsItemHovered()){const auto index=static_cast<std::size_t>(&target-destinations);if(index<10)ImGui::SetTooltip("Ctrl+%zu",(index+1)%10);else if(target.workspace==Workspace::Game)ImGui::SetTooltip("Ctrl+G");else ImGui::SetTooltip("Ctrl+PageUp / Ctrl+PageDown");}
            if(open){if(!external||ImGui::IsItemActivated()||((requested==Workspace::Tools||requested==Workspace::Settings)&&navigation_workspace!=target.workspace))switch_tab(target.workspace);ImGui::EndTabItem();}
        }ImGui::EndTabBar();
        if(workspace!=Workspace::Tools&&workspace!=Workspace::Settings)navigation_workspace=workspace;
    }
}
void Shell::draw_editor_preferences(){
    workflow_header("Editor preferences","Layouts belong to this user and project. Drag panel tabs and dividers to customize Scene and Game. Other workspaces retain their task-specific editors.");
    if(ImGui::Button("Save preferences now"))save_preferences();toolbar_next(180);
    if(ImGui::Button("Reset workspace layouts"))reset_layouts();
    ImGui::TextWrapped("%s",preferences_file.empty()?"Persistence disabled for this hidden verification session.":preferences_file.string().c_str());
    if(!preferences_status.empty())ImGui::TextWrapped("%s",preferences_status.c_str());
    ImGui::SetNextItemWidth(220);ImGui::SliderFloat("Interface scale",&ui_scale,.75f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::Button("Reset interface scale"))ui_scale=1;
    ImGui::TextWrapped("Scale multiplies the monitor DPI. Layout dimensions and text scale together. Save preferences retains this value.");
    workflow_header("Navigation and focus","Scene: F frames selection; Shift+F switches Orbit/Fly. Hold right mouse to navigate; middle mouse pans. Esc releases viewport or Game input. Maximize / Restore is available in each viewport toolbar.");
    ImGui::TextWrapped("Ctrl+S saves the active Scene or native gameplay owner; Ctrl+Z undoes, Ctrl+Y / Ctrl+Shift+Z redoes. Text fields keep their text history. Game and browsing do not consume authoring shortcuts. File > Save All runs both existing owners.");
    workflow_header("Tabs and references","Ctrl+1 to Ctrl+9 and Ctrl+0 open Scene, Assets, Character, Player, NPC, Ability, Animation, VFX, Audio and Items; Ctrl+G opens Game; Ctrl+PageUp / Ctrl+PageDown step through the tabs. Each tab keeps its own selection and drafts. Over a reference field, F12 opens the referenced asset in the tab that owns it; Open / Edit and Reveal in Assets do the same with the mouse.");
}
} // namespace darkangel::editor_app
