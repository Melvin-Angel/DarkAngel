#include "editor_ux_exercise.hpp"
#include "../editor_ui.hpp"
#include <imgui_internal.h>
#include <darkangel/check.hpp>
#include <iostream>
#include <cmath>
namespace darkangel::editor_app::exercises {
namespace {
class EditorUx final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;
    std::string source;StableId selection;ImGuiID dock{};float width{},height{};double yaw{};AssetId asset,mask;std::string draft_bytes;nlohmann::json mask_before,mask_after;
    void history_key(ImGuiKey key,bool down){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,down);io.AddKeyEvent(ImGuiKey_LeftCtrl,down);io.AddKeyEvent(key,down);}
    void click(bool down){ImGui::GetIO().AddMouseButtonEvent(0,down);}
    void point(const char* probe){const auto p=shell.probes.at(probe);ImGui::GetIO().AddMousePosEvent(p.x,p.y);}
    void unchanged(){require(editor.document->world().serialize()==source,"R2 layout changed authored scene");require(editor.selected==selection,"R2 layout lost scene selection");}
public:
    EditorUx(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame)override{
        if(!enabled_)return;
        if(frame==3){
            source=editor.document->world().serialize();selection=editor.selected;
            auto* view=ImGui::FindWindowByName("Viewport");require(view&&view->DockId,"R2 Scene viewport is not docked");
            dock=view->DockId;width=view->Size.x;height=view->Size.y;
            require(width>ImGui::GetIO().DisplaySize.x*.48f&&height>ImGui::GetIO().DisplaySize.y*.45f,"R2 Scene viewport is not dominant");
            point("viewport-maximize");
        }
        if(frame==4)click(true);if(frame==5)click(false);
        if(frame==8){require(shell.scene_maximized,"R2 maximize action failed");auto* view=ImGui::FindWindowByName("Viewport");require(view->Size.x>width&&view->Size.y>height,"R2 maximize did not enlarge viewport");unchanged();point("viewport-maximize");}
        if(frame==9)click(true);if(frame==10)click(false);
        if(frame==13){require(!shell.scene_maximized&&ImGui::FindWindowByName("Viewport")->DockId==dock,"R2 restore lost dock destination");unchanged();shell.save_preferences();shell.select_workspace(Workspace::Game);}
        if(frame==17){auto* view=ImGui::FindWindowByName("Game Viewport");require(view&&view->Size.x>ImGui::GetIO().DisplaySize.x*.8f,"R2 Game viewport is not dominant");shell.select_workspace(Workspace::Settings);shell.project_settings=false;}
        if(frame==21){require(shell.workspace==Workspace::Settings,"R2 workspace navigation overrode Settings");shell.reset_layouts();shell.select_workspace(Workspace::Scene);}
        if(frame==25){unchanged();require(ImGui::FindWindowByName("Viewport")->DockId,"R2 reset did not restore Scene dock");
            if(editor.assets){for(const auto& candidate:editor.asset_inventory)if(std::filesystem::path(candidate.path).extension()==".dacharacter"){asset=candidate.id;break;}}
            require(asset!=AssetId{},"R2 needs character authoring fixture");shell.open_native_asset(editor,asset);draft_bytes=editor.authoring.drafts.at(asset).value.dump();}
        if(frame==29){require(shell.workspace==Workspace::Character&&shell.character_selection==asset,"R2 Open lost character owner");shell.reveal_asset(asset);}
        if(frame==33){require(shell.workspace==Workspace::Assets,"R2 workspace switching failed");shell.open_native_asset(editor,asset);require(editor.authoring.drafts.at(asset).value.dump()==draft_bytes,"R2 navigation lost draft");shell.select_workspace(Workspace::Scene);shell.save_preferences();}
        if(frame==37){unchanged();for(const auto& candidate:editor.asset_inventory)if(std::filesystem::path(candidate.path).extension()==".dafacemask"){mask=candidate.id;break;}require(mask!=AssetId{},"R2 focus check needs a Mask");shell.mask_editor=mask;auto object=editor.document->world().read(editor.document->world().find(selection));yaw=object.transform.yaw;PropertyChange change{selection,1,1,yaw+.2};auto prepared=editor.document->prepare(editor.document->revision(),{&change,1});editor.document->commit(prepared);}
        if(frame==38){ImGui::SetWindowFocus("Viewport");history_key(ImGuiKey_Z,true);}if(frame==39)history_key(ImGuiKey_Z,false);
        if(frame==42)unchanged();
        if(frame==43)history_key(ImGuiKey_Y,true);if(frame==44)history_key(ImGuiKey_Y,false);
        if(frame==47)require(std::abs(editor.document->world().read(editor.document->world().find(selection)).transform.yaw-yaw-.2)<1e-6,"R2 Ctrl+Y did not redo Scene owner");
        if(frame==48)history_key(ImGuiKey_Z,true);if(frame==49)history_key(ImGuiKey_Z,false);
        if(frame==53){unchanged();mask_before=editor.authoring.open(*editor.assets,mask).value;mask_after=mask_before;mask_after["name"]=mask_before.at("name").get<std::string>()+" R2 check";editor.authoring.apply(*editor.assets,mask,editor.authoring.revision(),mask_after,"R2 native focus check");ImGui::SetWindowFocus("Mask Editor");}
        if(frame==54)history_key(ImGuiKey_Z,true);if(frame==55)history_key(ImGuiKey_Z,false);
        if(frame==58){require(editor.authoring.open(*editor.assets,mask).value==mask_before,"R2 focused Mask Undo did not use native history");unchanged();}
        if(frame==59)history_key(ImGuiKey_Y,true);if(frame==60)history_key(ImGuiKey_Y,false);
        if(frame==63)require(editor.authoring.open(*editor.assets,mask).value==mask_after,"R2 focused Mask Redo did not use native history");
        if(frame==64)history_key(ImGuiKey_Z,true);if(frame==65)history_key(ImGuiKey_Z,false);
        if(frame==69){unchanged();require(editor.authoring.open(*editor.assets,mask).value==mask_before,"R2 native Undo did not recover draft");shell.mask_editor={};done_=true;std::cout<<"Editor R2 viewport_dominant=1 maximize_restore=1 reset=1 settings=1 draft_retained=1 scene_unchanged=1 shortcuts=1 scale="<<shell.ui_scale<<'\n';}
    }
    void finish()override{if(enabled_)require(done_,"R2 exercise ended before acceptance");}
    bool done()const override{return !enabled_||done_;}
    std::string_view result()const override{return {};}
};
}
std::unique_ptr<Exercise> make_editor_ux(bool enabled,Controller& e,Shell& s){return std::make_unique<EditorUx>(enabled,e,s);}
}
