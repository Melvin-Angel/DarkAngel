#include "ui_exercise.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <algorithm>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
// R6.1 gate: the UI tab on disposable content. Create a menu document from its template, see its live preview, edit the
// markup (the preview follows), click a button (command logged only), Undo, Save, then start Play: the game HUD takes
// RmlUi from the preview, which comes back after Stop.
class UiExercise final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;AssetId document;
  public:
    UiExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame) override{
        if(!enabled_)return;
        if(frame==2){document=editor.authoring.create_ui_document(*editor.assets,"GateMenu","menu");editor.refresh_assets();shell.ui_selection=document;shell.select_workspace(Workspace::Ui);}
        if(frame==6){require(shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Start","UI exercise: template preview missing");
            auto& value=editor.authoring.open(*editor.assets,document).value;auto markup=value["markup"].get<std::string>();auto at=markup.find(">Start<");require(at!=std::string::npos,"UI exercise: template text");
            markup.replace(at,7,">Begin<");value["markup"]=markup;editor.authoring.record_changes(*editor.assets,"Edit UI source");}
        if(frame==9){require(shell.ui_preview_text("menu-start")=="Begin","UI exercise: preview did not follow the edit");require(shell.ui_preview_click("menu-start"),"UI exercise: button not laid out");
            const auto commands=shell.ui_preview_commands();require(!commands.empty()&&commands.back()=="menu.start","UI exercise: click command not logged");
            editor.authoring.undo(*editor.assets);}
        if(frame==12){require(shell.ui_preview_text("menu-start")=="Start","UI exercise: Undo did not reach the preview");editor.authoring.redo(*editor.assets);editor.save_gameplay();editor.refresh_assets();
            require(!editor.authoring.dirty(),"UI exercise: Save left the document dirty");shell.select_workspace(Workspace::Game);editor.play();}
        if(frame==24){require(!shell.ui_preview_alive(),"UI exercise: the preview kept RmlUi while the game HUD ran");editor.stop();shell.select_workspace(Workspace::Ui);}
        if(frame==30){require(shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Begin","UI exercise: preview did not return after Play");done_=true;
            std::cout<<"Editor ui create=1 preview=1 edit=1 command=1 undo=1 save=1 play_eviction=1 restore=1\n";}
    }
    void finish() override{require(!enabled_||done_,"UI exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_ui(bool enabled,Controller& editor,Shell& shell){return std::make_unique<UiExercise>(enabled,editor,shell);}
}
