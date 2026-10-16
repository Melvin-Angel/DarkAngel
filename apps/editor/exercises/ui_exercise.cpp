#include "ui_exercise.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <darkangel/ui_assets.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
// R6 gate: the UI tab on disposable content. R6.1: create a menu document from its template, see its live preview, edit the
// markup (the preview follows), click a button (command logged only), Undo, Save. R6 U0: the document is descriptor plus
// owned .rml/.rcss member files saved together, it lists a shared style document, a packaged font and a packaged image
// (da:// URL) that its cooked closure carries, an inline (R6.1) document is migrated explicitly, and the preview keeps
// running while Play's HUD runs (one shared RmlUi runtime).
class UiExercise final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;AssetId document,shared,inline_document,font,image;
    std::string& markup(){auto& value=editor.authoring.open(*editor.assets,document).value;return value["$members"]["GateMenu.rml"].get_ref<std::string&>();}
  public:
    UiExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame) override{
        if(!enabled_)return;
        if(frame==2){const auto root=editor.assets->source_root();
            // A packaged image beside the document's folder, and the project's packaged font.
            std::filesystem::create_directories(root/"ui/images");std::filesystem::copy_file(root/"royal_district/static/mud_huts/mudhut-trim.png",root/"ui/images/gate.png",std::filesystem::copy_options::overwrite_existing);
            image=editor.assets->adopt("ui/images/gate.png");editor.assets->scan();editor.assets->cook("ui/images/gate.png");editor.refresh_assets();
            for(const auto& asset:editor.asset_inventory)if(asset.path=="ui/fonts/Roboto-Medium.ttf")font=asset.id;require(font!=AssetId{},"UI exercise: the project's packaged font is missing");
            shared=editor.authoring.create_ui_document(*editor.assets,"GateShared","style");document=editor.authoring.create_ui_document(*editor.assets,"GateMenu","menu");
            auto& value=editor.authoring.open(*editor.assets,document).value;
            value["styles"]={{{"asset",shared.text()},{"source","ui/shared/GateShared.daui"}}};value["fonts"]={{{"asset",font.text()},{"source","ui/fonts/Roboto-Medium.ttf"},{"family","Gate Font"}}};value["images"]={{{"asset",image.text()},{"source","ui/images/gate.png"}}};
            auto& text=value["$members"]["GateMenu.rml"].get_ref<std::string&>();const auto at=text.find("</div>");text.insert(at,"<img id=\"menu-emblem\" src=\"da://"+image.text()+"\"/>");
            editor.authoring.record_changes(*editor.assets,"UI dependencies");editor.save_gameplay();editor.refresh_assets();
            shell.ui_selection=document;shell.select_workspace(Workspace::Ui);}
        if(frame==6){if(!(shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Start"))std::cout<<"UI exercise diagnostic: alive="<<shell.ui_preview_alive()<<" text=["<<shell.ui_preview_text("menu-start")<<"] problems=["<<shell.ui_preview_problems()<<"] author=["<<shell.author_diagnostic<<"]"<<std::endl;require(shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Start","UI exercise: template preview missing");
            auto& text=markup();auto at=text.find(">Start<");require(at!=std::string::npos,"UI exercise: template text");text.replace(at,7,">Begin<");editor.authoring.record_changes(*editor.assets,"Edit UI source");}
        if(frame==9){require(shell.ui_preview_text("menu-start")=="Begin","UI exercise: preview did not follow the edit");require(shell.ui_preview_click("menu-start"),"UI exercise: button not laid out");
            const auto commands=shell.ui_preview_commands();require(!commands.empty()&&commands.back()=="menu.start","UI exercise: click command not logged");
            editor.authoring.undo(*editor.assets);}
        if(frame==12){require(shell.ui_preview_text("menu-start")=="Start","UI exercise: Undo did not reach the preview");editor.authoring.redo(*editor.assets);editor.save_gameplay();editor.refresh_assets();
            require(!editor.authoring.dirty(),"UI exercise: Save left the document dirty");
            const auto root=editor.assets->source_root();const std::string saved=[&]{std::ifstream input(root/"ui/menus/GateMenu.rml",std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});}();
            require(saved.find(">Begin<")!=std::string::npos&&std::filesystem::exists(root/"ui/menus/GateMenu.rcss")&&std::filesystem::exists(root/"ui/shared/GateShared.rcss"),"UI exercise: member files were not saved with their descriptor");
            // The cooked closure carries the shared style, the packaged font and the image; nothing is read from source paths.
            auto cooked=load_cooked_ui_document(editor.package_model(document),editor.assets->cas_path(),document);
            require(cooked.fonts.size()==1&&cooked.fonts[0].first=="Gate Font"&&cooked.images.size()==1&&cooked.definition.style.find(".panel")!=std::string::npos,"UI exercise: cooked dependency closure");
            // An inline (R6.1) document is migrated explicitly and saved as descriptor plus members.
            inline_document=AssetId::random();auto t=ui_template("puzzle");nlohmann::json old={{"schema",1},{"kind","ui"},{"asset",inline_document.text()},{"name","GateInline"},{"purpose","puzzle"},{"surfaces",t.surfaces},{"size",{1280,720}},{"markup",t.markup},{"style",t.style}};
            editor.assets->create_native("ui/puzzles/GateInline.daui",old.dump(2));editor.refresh_assets();editor.authoring.migrate_ui_document(*editor.assets,inline_document);editor.save_gameplay();editor.refresh_assets();
            require(std::filesystem::exists(root/"ui/puzzles/GateInline.rml")&&load_cooked_ui_document(editor.package_model(inline_document),editor.assets->cas_path(),inline_document).definition.schema==2,"UI exercise: migration");
            shell.select_workspace(Workspace::Game);editor.play();}
        if(frame==18){require(editor.preview.active()!=nullptr,"UI exercise: Play did not start");shell.ui_selection=document;shell.select_workspace(Workspace::Ui);}
        if(frame==22){require(editor.preview.active()!=nullptr&&shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Begin","UI exercise: the preview did not run alongside Play");editor.stop();}
        if(frame==28){require(shell.ui_preview_alive()&&shell.ui_preview_text("menu-start")=="Begin","UI exercise: preview after Play");done_=true;
            std::cout<<"Editor ui create=1 preview=1 edit=1 command=1 undo=1 save=1 members=1 shared_style=1 package=1 migrate=1 play_coexist=1\n";}
    }
    void finish() override{require(!enabled_||done_,"UI exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_ui(bool enabled,Controller& editor,Shell& shell){return std::make_unique<UiExercise>(enabled,editor,shell);}
}
