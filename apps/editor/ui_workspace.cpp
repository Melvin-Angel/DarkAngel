#include "editor_ui.hpp"
#include <darkangel/ui_assets.hpp>
#ifdef DAE_HUD
#include "hud_overlay.hpp"
#include <darkangel/ui_document.hpp>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
namespace darkangel::editor_app {
// Preview state of the UI tab. The preview has its own context in the shared UiRuntime (R6 U0), so it keeps running while
// Play's HUD runs.
struct UiWorkspaceState {
#ifdef DAE_HUD
    std::unique_ptr<UiDocumentPreview> preview;UiPreviewSample sample;
#endif
    std::string loaded;            // markup + style the preview last tried, so typing reloads only on change
    AssetId document;std::vector<std::string> commands;int aspect{};
    std::set<AssetId> registered;  // packaged fonts and images already given to the preview
    char family[33]{"Body"};
    std::string failure;           // why the preview could not be prepared, for diagnostics
};
namespace {
using Json=nlohmann::json;
// Multi-line std::string editing through ImGui's resize callback.
bool text_area(const char* label,std::string& text,ImVec2 size){
    auto resize=[](ImGuiInputTextCallbackData* data)->int{if(data->EventFlag==ImGuiInputTextFlags_CallbackResize){auto* value=static_cast<std::string*>(data->UserData);value->resize(static_cast<std::size_t>(data->BufTextLen));data->Buf=value->data();}return 0;};
    return ImGui::InputTextMultiline(label,text.data(),text.capacity()+1,size,ImGuiInputTextFlags_CallbackResize|ImGuiInputTextFlags_AllowTabInput,resize,&text);
}
constexpr const char* purposes[]={"hud","marker","menu","puzzle","style"};
constexpr const char* purpose_labels[]={"HUD element","World marker","Menu / screen","Puzzle panel","Shared style"};
Json read_json(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);if(!input)return Json();return Json::parse(input,nullptr,false);}
// The member texts and the descriptor of a UI draft: schema 2 keeps members in "$members", schema 1 inline.
std::string member_text(const Json& value,const char* role){
    if(value.value("schema",1)==1)return value.value(role,std::string{});if(!value.contains("members")||!value["members"].contains(role))return {};
    const auto name=value["members"][role].get<std::string>();return value.contains("$members")&&value["$members"].contains(name)?value["$members"][name].get<std::string>():std::string{};}
void set_member_text(Json& value,const char* role,const std::string& text){if(value.value("schema",1)==1){value[role]=text;return;}value["$members"][value["members"][role].get<std::string>()]=text;}
UiDocumentDefinition decode_draft(const Json& value){auto descriptor=value;descriptor.erase("$members");return decode_ui_document(descriptor.dump(),[&](std::string_view name){return value.at("$members").at(std::string(name)).get<std::string>();});}
}
void Shell::draw_ui(Controller& editor,unsigned width,unsigned height,float top){
    ImGui::SetNextWindowPos({0,top});ImGui::SetNextWindowSize({float(width),float(height)-top});ImGui::Begin("UI workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(!editor.assets){ImGui::TextWrapped("Open a project source mount to author UI documents.");ImGui::End();return;}
    if(!ui_state)ui_state=std::make_shared<UiWorkspaceState>();auto& state=*ui_state;
    const bool playing=editor.preview.active()!=nullptr;
    // Document list: grouped by purpose, with New from a template.
    ImGui::BeginChild("UI documents",{std::min(260.f*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.25f),0},ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("UI documents");ImGui::BeginDisabled(playing);ImGui::SetNextItemWidth(-1);ImGui::Combo("##ui-template",&ui_template,purpose_labels,5);ImGui::SetNextItemWidth(-1);ImGui::InputText("##ui-name",ui_name,sizeof(ui_name));
    if(ImGui::Button("New from template")){try{ui_selection=editor.authoring.create_ui_document(*editor.assets,ui_name,purposes[ui_template]);editor.refresh_assets();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();}}
    if(ui_selection!=AssetId{}){ImGui::SameLine();if(ImGui::Button("Duplicate")){try{ui_selection=editor.authoring.duplicate(*editor.assets,ui_selection,ui_name);editor.refresh_assets();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();}}}
    ImGui::EndDisabled();
    std::vector<std::pair<AssetId,std::string>> documents;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daui")documents.push_back({asset.id,asset.path});
    for(const auto& [id,draft]:editor.authoring.drafts)if(draft.pending&&std::filesystem::path(draft.asset.path).extension()==".daui")documents.push_back({id,draft.asset.path+" (new)"});
    if(ui_selection==AssetId{}&&!documents.empty())ui_selection=documents.front().first;
    for(int purpose=0;purpose<5;++purpose){bool header=false;
        for(const auto& [id,path]:documents){std::string kind;try{kind=editor.authoring.open(*editor.assets,id).value.value("purpose","");}catch(const std::exception&){continue;}if(kind!=purposes[purpose])continue;
            if(!header){ImGui::SeparatorText(purpose_labels[purpose]);header=true;}ImGui::PushID(id.text().c_str());if(ImGui::Selectable(std::filesystem::path(path).filename().string().c_str(),ui_selection==id))ui_selection=id;ImGui::PopID();}}
    if(documents.empty())ImGui::TextWrapped("No UI documents yet. Choose a template, name it and press New from template.");
    ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("UI document",{0,0});
    try{if(ui_selection==AssetId{}){ImGui::TextDisabled("Select or create a UI document.");}else{
        auto& draft=editor.authoring.open(*editor.assets,ui_selection);auto& value=draft.value;const bool members=value.value("schema",1)==2;const bool style_only=value.value("purpose","")=="style";
        ImGui::Text("Document: %s",value.value("name","").c_str());ImGui::SameLine();ImGui::TextDisabled("%s  |  %s",draft.asset.path.c_str(),draft.pending?"New draft: Save publishes it":draft.dirty()?"Unsaved changes":"Saved");
        // Authoring is not paused by Play: drafts and Save are independent of the running session (fresh Play uses saved files).
        if(ImGui::Button("Save")){try{editor.save_gameplay();editor.refresh_assets();author_diagnostic="Saved the UI document and its member files together.";}catch(const std::exception& error){author_diagnostic=error.what();}}ImGui::SameLine();
        ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo"))editor.authoring.undo(*editor.assets);ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo"))editor.authoring.redo(*editor.assets);ImGui::EndDisabled();
        if(!members){ImGui::SameLine();if(ImGui::Button("Migrate to member files")){try{editor.authoring.migrate_ui_document(*editor.assets,ui_selection);author_diagnostic="Migrated: Save writes the descriptor and its .rml/.rcss member files together.";}catch(const std::exception& error){author_diagnostic=error.what();}}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("This document keeps its markup and style inside the .daui (R6.1). Migration moves them into <name>.rml and <name>.rcss beside it.");}
        // Descriptor: purpose and permitted surfaces. The attachment that uses the document picks its surface.
        if(!style_only){
            {int purpose=int(std::find(std::begin(purposes),std::end(purposes),value.value("purpose",""))-std::begin(purposes));ImGui::SameLine();ImGui::SetNextItemWidth(150);if(purpose<4&&ImGui::Combo("Purpose",&purpose,purpose_labels,4))value["purpose"]=purposes[purpose];}
            for(const char* surface:{"screen","world_anchor","world_surface"}){auto& surfaces=value["surfaces"];bool on=std::find(surfaces.begin(),surfaces.end(),surface)!=surfaces.end();ImGui::SameLine();if(ImGui::Checkbox(surface,&on)){if(on)surfaces.push_back(surface);else surfaces.erase(std::remove(surfaces.begin(),surfaces.end(),surface),surfaces.end());}}
            if(std::find(value["surfaces"].begin(),value["surfaces"].end(),"world_surface")!=value["surfaces"].end())ImGui::TextDisabled("World surface: 2D preview; 3D runtime pending.");}
        else ImGui::TextDisabled("Shared style: documents list it under Styles; it is applied before their own style.");
        // Dependencies (member-file documents): shared styles, packaged fonts and images by virtual URL.
        if(!style_only){const auto binding=value.value("binding",std::string("ui-v1"));if(ImGui::BeginCombo("Binding contract",binding.c_str())){for(const char* option:{"ui-v1","owner-v1","inventory-v1","altar-v1"})if(ImGui::Selectable(option,binding==option))value["binding"]=option;ImGui::EndCombo();}}
        if(members&&!style_only&&ImGui::TreeNode("Dependencies")){
            auto list=[&](const char* key){if(!value.contains(key)||!value[key].is_array())value[key]=Json::array();return &value[key];};
            ImGui::SeparatorText("Shared styles");std::optional<std::size_t> removed;auto* styles=list("styles");
            for(std::size_t i=0;i<styles->size();++i){ImGui::PushID(int(i));ImGui::BulletText("%s",(*styles)[i].value("source","").c_str());ImGui::SameLine();if(ImGui::SmallButton("Remove"))removed=i;ImGui::PopID();}if(removed)styles->erase(*removed);removed.reset();
            if(ImGui::BeginCombo("Add shared style","Choose...")){for(const auto& [id,path]:documents){if(id==ui_selection)continue;Json other;try{other=editor.authoring.open(*editor.assets,id).value;}catch(const std::exception&){continue;}if(other.value("purpose","")!="style"||editor.authoring.drafts.at(id).pending)continue;
                    if(ImGui::Selectable(path.c_str()))styles->push_back({{"asset",id.text()},{"source",path}});}ImGui::EndCombo();}
            ImGui::SeparatorText("Fonts");auto* fonts=list("fonts");
            for(std::size_t i=0;i<fonts->size();++i){ImGui::PushID(int(100+i));ImGui::BulletText("%s  (font-family: %s)",(*fonts)[i].value("source","").c_str(),(*fonts)[i].value("family","").c_str());ImGui::SameLine();if(ImGui::SmallButton("Remove"))removed=i;ImGui::PopID();}if(removed)fonts->erase(*removed);removed.reset();
            ImGui::SetNextItemWidth(120);ImGui::InputText("Family",state.family,sizeof(state.family));ImGui::SameLine();
            if(ImGui::BeginCombo("Add font","Choose...")){for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".ttf"&&extension!=".otf")continue;if(ImGui::Selectable(asset.path.c_str()))fonts->push_back({{"asset",asset.id.text()},{"source",asset.path},{"family",std::string(state.family)}});}ImGui::EndCombo();}
            ImGui::SeparatorText("Images");auto* images=list("images");
            for(std::size_t i=0;i<images->size();++i){ImGui::PushID(int(200+i));const auto url="da://"+(*images)[i].value("asset","");ImGui::BulletText("%s  %s",(*images)[i].value("source","").c_str(),url.c_str());ImGui::SameLine();if(ImGui::SmallButton("Copy URL"))ImGui::SetClipboardText(url.c_str());ImGui::SameLine();if(ImGui::SmallButton("Remove"))removed=i;ImGui::PopID();}if(removed)images->erase(*removed);
            if(ImGui::BeginCombo("Add image","Choose...")){for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".png"&&extension!=".jpg"&&extension!=".jpeg")continue;
                    auto metadata=read_json(editor.assets->source_root()/(asset.path+".daimport"));if(!metadata.is_object()||metadata.value("importer","")!="texture-color-v1")continue;if(ImGui::Selectable(asset.path.c_str()))images->push_back({{"asset",asset.id.text()},{"source",asset.path}});}ImGui::EndCombo();}
            ImGui::TextDisabled("Use an image with src=\"da://<asset>\" or url(da://<asset>); fonts by font-family.");
            for(const char* key:{"styles","fonts","images"})if(value.contains(key)&&value[key].empty())value.erase(key);
            ImGui::TreePop();}
        const float column=ImGui::GetContentRegionAvail().x*.5f,rows=std::max(160.f,ImGui::GetContentRegionAvail().y*.5f-20);
        ImGui::BeginChild("UI source",{column,0});
        auto markup=member_text(value,"markup"),style=member_text(value,"style");markup.reserve(markup.size()+256);style.reserve(style.size()+256);
        if(!style_only){ImGui::TextDisabled(members?("RML markup: "+value["members"].value("markup",std::string{})).c_str():"RML markup");if(text_area("##markup",markup,{-1,rows}))set_member_text(value,"markup",markup);}
        ImGui::TextDisabled(members?("RCSS style: "+value["members"].value("style",std::string{})).c_str():"RCSS style");if(text_area("##style",style,{-1,style_only?rows*2:rows}))set_member_text(value,"style",style);
        if(members&&!style_only){
            if(!value["members"].contains("script")){if(ImGui::Button("Add presentation script")){const auto name=std::filesystem::path(draft.asset.path).stem().string()+".luau";value["members"]["script"]=name;value["$members"][name]="return { on_command = function(ui, handle, command)\n    return false\nend }\n";}}
            else{auto script=member_text(value,"script");ImGui::TextUnformatted("Local Luau presentation script");if(text_area("##ui-script",script,{-1,rows}))set_member_text(value,"script",script);}
        }
        ImGui::EndChild();
        {auto active=ImGui::GetActiveID();editor.authoring.record_changes(*editor.assets,active?"Edit UI source "+std::to_string(active):"Edit UI document",active!=0);}
        std::string invalid;std::optional<UiDocumentDefinition> decoded;try{decoded=members?decode_draft(value):decode_ui_document(value.dump());}catch(const std::exception& error){invalid=error.what();}
        ImGui::SameLine();ImGui::BeginChild("UI preview",{0,0},ImGuiChildFlags_Borders);
        if(!invalid.empty())ImGui::TextColored({1.f,.55f,.3f,1.f},"Needs attention: %s. Save will reject it.",invalid.c_str());
#ifdef DAE_HUD
        if(!ui_renderer)ImGui::TextDisabled("The UI preview needs the editor's HUD renderer.");
        else if(style_only)ImGui::TextDisabled("A shared style has no markup of its own; preview a document that lists it.");
        else{
            const char* aspects[]={"16:9","4:3","21:9"};ImGui::SetNextItemWidth(90);ImGui::Combo("Aspect",&state.aspect,aspects,3);
            auto& sample=state.sample;float health=float(sample.health),stamina=float(sample.stamina),cooldown=float(sample.cooldown);
            ImGui::SameLine();ImGui::SetNextItemWidth(110);if(ImGui::SliderFloat("Health",&health,0,float(sample.maximum_health)))sample.health=health;ImGui::SameLine();ImGui::SetNextItemWidth(110);if(ImGui::SliderFloat("Stamina",&stamina,0,float(sample.maximum_stamina)))sample.stamina=stamina;
            ImGui::SetNextItemWidth(110);if(ImGui::SliderFloat("Cooldown",&cooldown,0,1))sample.cooldown=cooldown;ImGui::SameLine();ImGui::Checkbox("Flag",&sample.flag);ImGui::SameLine();
            {char name[65]{};strncpy_s(name,sample.name.c_str(),_TRUNCATE);ImGui::SetNextItemWidth(140);if(ImGui::InputText("Name",name,sizeof(name)))sample.name=name;}
            if(playing)ImGui::TextDisabled("Play is running; the preview keeps its own context beside the game HUD.");
            const ImVec2 room=ImGui::GetContentRegionAvail();const float ratio=state.aspect==1?4.f/3:state.aspect==2?21.f/9:16.f/9;
            const float view_width=std::max(64.f,std::min(room.x,(room.y-110)*ratio)),view_height=std::max(64.f,view_width/ratio);
            try{
                if(!state.preview){
                    // The "HUD" face: the project's packaged UI font, or the one the game ships beside its HUD document.
                    std::string bytes;if(auto font=editor.packaged_ui_font())bytes=std::move(font->second);
                    if(bytes.empty())throw std::runtime_error("No packaged UI font: import a .ttf/.otf (with its licence) under ui/fonts");
                    state.preview=std::make_unique<UiDocumentPreview>(*static_cast<ImGuiHudRenderer*>(ui_renderer),UiFont{"HUD",std::move(bytes)},int(view_width),int(view_height));state.loaded.clear();state.registered.clear();}
                // Shared styles (their current drafts) come first, then the document's own style; packaged fonts and images are
                // cooked assets given to the shared runtime before the document loads.
                std::string composed_style;
                if(decoded){for(const auto& shared:decoded->styles){try{composed_style+=member_text(editor.authoring.open(*editor.assets,shared.asset).value,"style")+"\n";}catch(const std::exception&){}}
                    for(const auto& font:decoded->fonts)if(state.registered.insert(font.asset).second)state.preview->register_font({font.family,load_cooked_font(editor.package_model(font.asset),editor.assets->cas_path(),font.asset)});
                    for(const auto& image:decoded->images)if(state.registered.insert(image.asset).second){auto texture=load_cooked_texture(editor.package_model(image.asset),editor.assets->cas_path(),image.asset);const auto& top=texture.mips.front();state.preview->register_image("da://"+image.asset.text(),{int(top.width),int(top.height),top.rgba});}}
                composed_style+=style;
                const auto key=ui_selection.text()+(decoded?decoded->binding:"ui-v1")+markup+"\n/*style*/\n"+composed_style;
                if(state.loaded!=key||state.document!=ui_selection){state.loaded=key;state.document=ui_selection;state.preview->load(markup,composed_style,decoded?decoded->binding:"ui-v1");}
                state.preview->resize(int(view_width),int(view_height));state.preview->update(sample);
                const ImVec2 origin=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##ui-preview",{view_width,view_height});
                const bool hovered=ImGui::IsItemHovered();const auto mouse=ImGui::GetIO().MousePos;
                if(hovered)state.preview->pointer(int(mouse.x-origin.x),int(mouse.y-origin.y),ImGui::IsMouseClicked(ImGuiMouseButton_Left),ImGui::IsMouseReleased(ImGuiMouseButton_Left));
                auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,{origin.x+view_width,origin.y+view_height},IM_COL32(24,26,32,255));
                auto& renderer=*static_cast<ImGuiHudRenderer*>(ui_renderer);renderer.begin(draw,origin,{view_width,view_height});state.preview->render();renderer.end();
                for(auto& command:state.preview->drain_commands()){state.commands.push_back(std::move(command));if(state.commands.size()>20)state.commands.erase(state.commands.begin());}
                if(state.preview->stale())ImGui::TextColored({1.f,.55f,.3f,1.f},"Stale preview: showing the last document that loaded.");
                if(!state.preview->problems().empty())ImGui::TextWrapped("%s",state.preview->problems().c_str());
            }catch(const std::exception& error){state.preview.reset();state.failure=error.what();ImGui::TextWrapped("Preview unavailable: %s",error.what());}
            ImGui::SeparatorText("Commands (logged only; preview clicks never change the game)");
            if(state.commands.empty())ImGui::TextDisabled("Click a button with data-event-click=\"command('id')\" in the preview.");
            for(auto it=state.commands.rbegin();it!=state.commands.rend();++it)ImGui::BulletText("%s",it->c_str());
        }
#else
        ImGui::TextDisabled("This editor build has no RmlUi, so UI documents cannot be previewed.");
#endif
        ImGui::EndChild();
    }}catch(const std::exception& error){author_diagnostic=error.what();}
    if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());
    ImGui::EndChild();ImGui::End();
}
#ifdef DAE_HUD
bool Shell::ui_preview_alive()const{return ui_state&&ui_state->preview;}
std::string Shell::ui_preview_problems()const{return ui_preview_alive()?ui_state->preview->problems():ui_state?ui_state->failure:std::string{};}
std::string Shell::ui_preview_text(const std::string& id)const{return ui_preview_alive()?ui_state->preview->text(id):std::string{};}
bool Shell::ui_preview_click(const std::string& id){if(!ui_preview_alive())return false;auto box=ui_state->preview->box(id);if(!box)return false;const int x=int(box->x+box->width/2),y=int(box->y+box->height/2);
    ui_state->preview->pointer(x,y,true,false);ui_state->preview->pointer(x,y,false,true);for(auto& command:ui_state->preview->drain_commands())ui_state->commands.push_back(std::move(command));return true;}
#else
bool Shell::ui_preview_alive()const{return false;}std::string Shell::ui_preview_problems()const{return {};}std::string Shell::ui_preview_text(const std::string&)const{return {};}bool Shell::ui_preview_click(const std::string&){return false;}
#endif
std::vector<std::string> Shell::ui_preview_commands()const{return ui_state?ui_state->commands:std::vector<std::string>{};}
}
