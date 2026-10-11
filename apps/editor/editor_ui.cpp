#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include <cstring>
#include <cmath>
#include <set>
#include <fstream>
#include <algorithm>
namespace darkangel::editor_app {
namespace {
template<class F>void action(Controller& editor,F&& call){try{call();}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}
std::filesystem::path dialog(bool save){wchar_t file[32768]{};OPENFILENAMEW options{};options.lStructSize=sizeof(options);options.lpstrFile=file;options.nMaxFile=32768;options.lpstrFilter=L"DarkAngel scene\0*.dascene\0All files\0*.*\0";options.lpstrDefExt=L"dascene";options.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);if(save?GetSaveFileNameW(&options):GetOpenFileNameW(&options))return file;return {};}
// NPC spawn points are scene objects that reference an NPC definition. Adding, retargeting and removing one is a
// structural edit of the scene's own source through the document, so it shares its history, Undo and Save.
std::string object_local(const Controller& editor,StableId object){for(const auto& origin:editor.document->plan().origins)if(origin.object==object&&origin.definition==editor.document->root())return origin.local_path.substr(origin.local_path.rfind('/')+1);throw std::runtime_error("This object belongs to a mounted assembly; edit it there");}
void commit_scene(Controller& editor,const nlohmann::json& graph,const char* message){auto edit=editor.document->prepare_source(editor.document->revision(),editor.document->root(),graph.dump());editor.document->commit(edit);editor.log(message);}
// Adds one entity to the scene's own source under the first free local identity and returns the placed object.
StableId add_object(Controller& editor,nlohmann::json entity,const Transform& at,const char* message){
    auto graph=nlohmann::json::parse(editor.document->source(editor.document->root()));auto& entities=graph.at("entities");std::string local;
    for(unsigned index=1;index<=1024;++index){local=StableId{0,index}.text();if(!entities.contains(local))break;if(index==1024)throw std::runtime_error("Scene object limit");}
    entity["target"]=nullptr;entity["types"]={{"1",{{"version",2},{"fields",{{"1",at.yaw},{"2",at.x},{"3",at.y},{"4",at.z},{"5",0},{"6",0},{"7",1}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};
    entities[local]=std::move(entity);commit_scene(editor,graph,message);for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+local))return origin.object;throw std::runtime_error("Placed object did not resolve");
}
// Beside the selected object, or at the origin of an empty selection.
Transform beside_selection(Controller& editor,double height){Transform at;at.y=height;auto selected=editor.document->world().find(editor.selected);if(editor.document->world().valid(selected)){at=editor.document->world().read(selected).transform;at.x+=1.5;at.pitch=at.roll=0;at.scale=1;}return at;}
void set_spawn_npc(Controller& editor,StableId object,AssetId npc){auto graph=nlohmann::json::parse(editor.document->source(editor.document->root()));graph.at("entities").at(object_local(editor,object))["spawn"]={{"npc",npc.text()}};commit_scene(editor,graph,"Changed the spawn point's NPC.");}
std::string npc_label(const Controller& editor,AssetId npc){for(const auto& asset:editor.asset_inventory)if(asset.id==npc)return std::filesystem::path(asset.path).stem().string();return npc.text();}
double property(const ObjectData& data,TypeId type,const PropertyMetadata& field){const void* component=type==1?static_cast<const void*>(&data.transform):static_cast<const void*>(&data.health);double value;std::memcpy(&value,static_cast<const char*>(component)+field.offset,sizeof(value));return value;}
}
void Shell::refresh_place_items(Controller& editor){
    // Rebuilt only when the catalogue changes: an imported model's importer is read from its sidecar once.
    std::size_t signature=editor.asset_inventory.size();for(const auto& asset:editor.asset_inventory)signature=signature*1315423911u+std::hash<std::string>{}(asset.path)+asset.generation;
    if(signature==place_signature&&!place_items.empty())return;place_signature=signature;place_items.clear();if(!editor.assets)return;
    for(const auto& asset:editor.asset_inventory){const std::filesystem::path path(asset.path);const auto extension=path.extension().string();
        if(extension==".danpc"){place_items.push_back({asset.id,path.stem().string(),"NPCs",true});continue;}
        if(extension!=".gltf"&&extension!=".glb")continue;
        std::ifstream sidecar(editor.assets->source_root()/(asset.path+".daimport"));if(!sidecar)continue;
        try{if(nlohmann::json::parse(sidecar).value("importer",std::string())!="static-gltf-v1")continue;}catch(const std::exception&){continue;}
        auto folder=path.parent_path().filename().string();std::replace(folder.begin(),folder.end(),'_',' ');place_items.push_back({asset.id,path.stem().string(),folder.empty()?"Environment":"Environment: "+folder,false});}
    std::sort(place_items.begin(),place_items.end(),[](const PlaceItem& a,const PlaceItem& b){return a.npc!=b.npc?a.npc:a.category!=b.category?a.category<b.category:a.name<b.name;});
}
StableId Shell::place(Controller& editor,const PlaceItem& item,double x,double z,double yaw){
    if(editor.preview.active())throw std::runtime_error("Stop Play before placing objects");
    Transform at;at.x=x;at.y=place_height;at.z=z;at.yaw=yaw;StableId placed;
    if(item.npc)placed=add_object(editor,{{"name",item.name+" spawn"},{"spawn",{{"npc",item.id.text()}}}},at,"Placed an NPC spawn point.");
    else{
        // A model outside the loaded resources is cooked and packaged with the scene; a placement that cannot be shown is undone.
        bool loaded=true;try{if(editor.prepare_model)editor.prepare_model(item.id);}catch(const std::exception&){loaded=false;if(!editor.prepare_play_resources)throw std::runtime_error("This model is not in the loaded resources and this session cannot cook it");}
        placed=add_object(editor,{{"name",item.name},{"model",item.id.text()}},at,"Placed a model.");
        if(!loaded){try{editor.prepare_play_resources();}catch(...){editor.document->undo(editor.document->revision());throw;}}
    }
    editor.selected=placed;return placed;
}
void Shell::remove_object(Controller& editor,StableId object){
    if(editor.preview.active())throw std::runtime_error("Stop Play before deleting objects");
    auto graph=nlohmann::json::parse(editor.document->source(editor.document->root()));const auto local=object_local(editor,object);const auto name=graph.at("entities").at(local).value("name",local);graph.at("entities").erase(local);
    commit_scene(editor,graph,("Deleted "+name+". Undo brings it back.").c_str());if(editor.selected==object)editor.selected=editor.document->plan().origins.empty()?StableId{}:editor.document->plan().origins.front().object;
}
ViewArea Shell::draw(Controller& editor,unsigned width,unsigned height,const char* backend,std::uint64_t generation,double seconds,ImTextureID scene_texture){
    forward=lateral=turn=0;walk=jump=false;controls_acquired=false;
    const auto menu_height=ImGui::GetFrameHeight();const auto toolbar_height=ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y*2;const auto workspace_top=menu_height+toolbar_height;
    if(ImGui::BeginMainMenuBar()){ImGui::TextColored({.25f,.75f,.95f,1},"DarkAngel");ImGui::Separator();ImGui::BeginDisabled(dragging);
        if(ImGui::BeginMenu("File")){ImGui::BeginDisabled(!editor.assets||editor.preview.active()!=nullptr);if(ImGui::MenuItem("Import asset..."))choose_asset_import();ImGui::EndDisabled();if(ImGui::MenuItem("New Scene"))action(editor,[&]{editor.demo();});if(ImGui::MenuItem("Open..."))action(editor,[&]{auto path=dialog(false);if(!path.empty())editor.open(path);});if(ImGui::MenuItem("Save Scene"))action(editor,[&]{editor.save(editor.path);});if(ImGui::MenuItem("Save As..."))action(editor,[&]{auto path=dialog(true);if(!path.empty())editor.save(path);});if(ImGui::BeginMenu("Reload")){if(ImGui::MenuItem("Scripts"))action(editor,[&]{editor.reload_scripts();});if(ImGui::MenuItem("Models"))reload_model=true;if(ImGui::MenuItem("Shaders"))reload_shader=true;ImGui::EndMenu();}ImGui::EndMenu();}
        if(ImGui::BeginMenu("Tools")){if(ImGui::MenuItem("Console"))open_tool(ToolView::Console);if(ImGui::MenuItem("Authoring history"))open_tool(ToolView::AuthoringHistory);if(ImGui::MenuItem("Input bindings"))open_tool(ToolView::InputBindings);if(ImGui::MenuItem("Navigation"))open_tool(ToolView::Navigation);if(ImGui::MenuItem("Script source"))open_tool(ToolView::ScriptSource);if(ImGui::MenuItem("Runtime phases"))open_tool(ToolView::RuntimePhases);ImGui::EndMenu();}
        if(ImGui::BeginMenu("Settings")){if(ImGui::MenuItem("Editor settings")){if(workspace!=Workspace::Settings&&workspace!=Workspace::Tools)previous_workspace=workspace;project_settings=false;select_workspace(Workspace::Settings);}if(ImGui::MenuItem("Project settings")){if(workspace!=Workspace::Settings&&workspace!=Workspace::Tools)previous_workspace=workspace;project_settings=true;select_workspace(Workspace::Settings);}ImGui::EndMenu();}ImGui::EndDisabled();ImGui::TextDisabled("%s | %s",backend,editor.preview.active()?(editor.paused?"Paused":"Playing"):"Stopped");ImGui::EndMainMenuBar();}
    ImGui::SetNextWindowPos({0,menu_height});ImGui::SetNextWindowSize({float(width),toolbar_height});ImGui::Begin("Workflow navigation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);ImGui::BeginDisabled(dragging);if(ImGui::Selectable("Scene / Level Design",workspace==Workspace::Scene,0,{180,0}))select_workspace(Workspace::Scene);ImGui::SameLine();if(ImGui::Selectable("Assets",workspace==Workspace::Assets,0,{110,0}))select_workspace(Workspace::Assets);ImGui::SameLine();if(ImGui::Selectable("Character",workspace==Workspace::Character,0,{100,0}))select_workspace(Workspace::Character);ImGui::SameLine();if(ImGui::Selectable("Player",workspace==Workspace::Player,0,{90,0}))select_workspace(Workspace::Player);ImGui::SameLine();if(ImGui::Selectable("NPC",workspace==Workspace::Npc,0,{70,0}))select_workspace(Workspace::Npc);ImGui::SameLine();if(ImGui::Selectable("Ability",workspace==Workspace::Ability,0,{110,0}))select_workspace(Workspace::Ability);ImGui::SameLine();if(ImGui::Selectable("Animation",workspace==Workspace::Animation,0,{110,0}))select_workspace(Workspace::Animation);ImGui::SameLine();if(ImGui::Selectable("Game",workspace==Workspace::Game,0,{110,0}))select_workspace(Workspace::Game);ImGui::EndDisabled();ImGui::SameLine();if(editor.document->dirty())ImGui::TextColored(attention,"Unsaved");else ImGui::TextDisabled("Saved");ImGui::End();
    draw_mask_editor(editor);
    if(workspace!=Workspace::Scene&&docked)ImGui::DockSpace(ImHashStr("DarkAngelSceneDock"),{0,0},ImGuiDockNodeFlags_KeepAliveOnly);
    if(workspace!=Workspace::Game&&game_docked)ImGui::DockSpace(ImHashStr("DarkAngelGameDock"),{0,0},ImGuiDockNodeFlags_KeepAliveOnly);
    if(workspace==Workspace::Assets){controls_focus=false;ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({float(width),float(height)-workspace_top});ImGui::Begin("Assets workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);draw_assets(editor,true);ImGui::End();draw_import(editor);return area;}
    composer_frame_seconds=std::isfinite(seconds)?std::clamp(seconds,0.,.1):0.;
    if(composer_dragging&&workspace!=Workspace::Ability&&workspace!=Workspace::Animation)cancel_composer_drag(editor);
    composer_visible=false;composer_texture=scene_texture;
    if(workspace==Workspace::Ability||workspace==Workspace::Animation||workspace==Workspace::Character||workspace==Workspace::Player||workspace==Workspace::Npc){controls_focus=false;draw_authoring(editor,width,height,workspace_top);return area;}
    if(workspace==Workspace::Game){draw_game(editor,width,height,workspace_top,seconds,scene_texture);draw_import(editor);return area;}
    if(workspace==Workspace::Tools||workspace==Workspace::Settings){controls_focus=false;draw_auxiliary(editor,width,height,workspace_top);draw_import(editor);return area;}
    controls_focus=false;

    ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({static_cast<float>(width),static_cast<float>(height)-workspace_top});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::Begin("Workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBackground);auto dock=ImHashStr("DarkAngelSceneDock");ImGui::DockSpace(dock,{0,0},ImGuiDockNodeFlags_PassthruCentralNode);ImGui::End();ImGui::PopStyleVar();
    if(!docked){ImGui::DockBuilderRemoveNode(dock);ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);ImGui::DockBuilderSetNodeSize(dock,{static_cast<float>(width),static_cast<float>(height)-workspace_top});auto center=dock;auto left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.18f,nullptr,&center);auto right=ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.28f,nullptr,&center);auto bottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.36f,nullptr,&center);auto place_node=ImGui::DockBuilderSplitNode(bottom,ImGuiDir_Left,.64f,nullptr,&bottom);auto content=bottom;ImGui::DockBuilderDockWindow("Place",place_node);ImGui::DockBuilderDockWindow("World Outliner",left);ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Console",bottom);ImGui::DockBuilderDockWindow("Content Browser",content);ImGui::DockBuilderFinish(dock);docked=true;}
    StableId remove_request;const bool can_edit=editor.preview.active()==nullptr&&!dragging;
    ImGui::Begin("World Outliner");if(can_edit&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Delete,false))remove_request=editor.selected;ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##filter","Search objects...",filter,sizeof(filter));icon_text(Icon::Object,"Scene placements");for(const auto& origin:editor.document->plan().origins){if(filter[0] && origin.name.find(filter)==std::string::npos)continue;ImGui::PushID(origin.object.text().c_str());if(icon_selectable(Icon::Object,(origin.name+"##object").c_str(),editor.selected==origin.object))editor.selected=origin.object;if(ImGui::BeginPopupContextItem("##object-menu")){editor.selected=origin.object;if(ImGui::MenuItem("Delete","Del",false,editor.preview.active()==nullptr))remove_request=origin.object;ImGui::EndPopup();}ImGui::TextDisabled("%s%s",editor.document->plan().spawns.contains(origin.object)?"NPC spawn | ":"",origin.object.text().substr(24).c_str());ImGui::PopID();}
    ImGui::Separator();ImGui::BeginDisabled(editor.preview.active()!=nullptr||dragging);if(ImGui::Button("Add NPC spawn point"))ImGui::OpenPopup("Add NPC spawn point");ImGui::EndDisabled();
    if(ImGui::BeginPopup("Add NPC spawn point")){bool any=false;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"){any=true;if(ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str()))action(editor,[&]{const auto at=beside_selection(editor,place_height);place(editor,{asset.id,std::filesystem::path(asset.path).stem().string(),"NPCs",true},at.x,at.z,at.yaw);});}if(!any)ImGui::TextDisabled("No NPC definitions. Create one in the NPC tab.");ImGui::EndPopup();}
    ImGui::End();
    ImGui::Begin("Inspector");auto handle=editor.displayed().find(editor.selected);if(editor.displayed().valid(handle)){auto object=editor.displayed().read(handle);icon_text(Icon::Object,"Persistent object ID");ImGui::Text("%s",object.id.text().substr(0,16).c_str());ImGui::Text("%s",object.id.text().substr(16).c_str());ImGui::Text("World %llu | epoch %llu",handle.domain,handle.epoch);ImGui::Text("Handle %llu | valid",handle.runtime);int destination=scope==EditScope::Definition?1:0;const char* destinations[]={"Scene placement","Assembly definition"};ImGui::TextDisabled("Edit destination");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##Edit destination",&destination,destinations,2))scope=destination?EditScope::Definition:EditScope::Placement;ImGui::BeginDisabled(editor.preview.active()!=nullptr || dragging);
        for(const auto& type:metadata())if(type.id<=2 && ImGui::CollapsingHeader(std::string(type.name).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){ImGui::TextDisabled("TypeId %u | schema %u",type.id,type.version);if(ImGui::BeginTable("Properties",2,ImGuiTableFlags_SizingStretchProp)){ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("maximum").x+8);ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);for(const auto& field:type.properties)if(field.flags&Editable){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(std::string(field.name).c_str());ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);auto value=property(object,type.id,field);ImGui::PushID(static_cast<int>(type.id*100+field.id));const bool steppers=ImGui::GetContentRegionAvail().x>=ImGui::GetFrameHeight()*2+ImGui::GetFontSize()*6;if(ImGui::InputDouble("##value",&value,steppers?.1:0,steppers?1:0,"%.3f"))action(editor,[&]{PropertyChange change{object.id,type.id,field.id,value};auto prepared=editor.document->prepare(editor.document->revision(),{&change,1},scope);editor.document->commit(prepared);editor.log(prepared.summary);});ImGui::PopID();}ImGui::EndTable();}}
        if(auto spawn=editor.document->plan().spawns.find(object.id);spawn!=editor.document->plan().spawns.end()&&ImGui::CollapsingHeader("NPC spawn point",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextWrapped("Play spawns this NPC here with its own kit, attributes and behaviour. The object's Health is replaced by the NPC's.");ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##spawn-npc",npc_label(editor,spawn->second).c_str())){for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"&&ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str(),asset.id==spawn->second)&&asset.id!=spawn->second){const auto point=object.id;const auto npc=asset.id;action(editor,[&]{set_spawn_npc(editor,point,npc);});break;}ImGui::EndCombo();}
            ImGui::TextDisabled("Respawn: never");}
        if(ImGui::Button("Delete object"))remove_request=object.id;ImGui::SameLine();
        if(ImGui::Button("Undo"))action(editor,[&]{editor.document->undo(editor.document->revision());});ImGui::SameLine();if(ImGui::Button("Redo"))action(editor,[&]{editor.document->redo(editor.document->revision());});ImGui::EndDisabled();
        if(ImGui::CollapsingHeader("Script Bindings",ImGuiTreeNodeFlags_DefaultOpen))for(const auto& binding:object.scripts.records){ImGui::TextWrapped("Attachment %s",binding.id.text().c_str());ImGui::TextWrapped("Asset %s",asset_id(binding.asset).text().c_str());ImGui::TextUnformatted(binding.config_json.c_str());if(editor.preview.active())for(const auto& info:editor.preview.active()->scripts->bindings())if(info.id==binding.id){ImGui::Text("%s | generation %llu",info.enabled?"Enabled":"Disabled",info.generation);ImGui::Text("Calls %llu | last %llu us",info.metrics.calls,info.metrics.last_microseconds);}}
        if(ImGui::CollapsingHeader("Provenance"))for(const auto& origin:editor.document->plan().origins)if(origin.object==object.id){ImGui::TextWrapped("Definition %s",origin.definition.text().c_str());ImGui::TextWrapped("Stable local path %s",origin.local_path.c_str());ImGui::TextWrapped("%s",origin.layer.c_str());}
    }else ImGui::TextDisabled("Select a scene object.");ImGui::End();
    ImGui::Begin("Content Browser");draw_assets(editor);ImGui::Separator();icon_text(Icon::Folder,"Scene models");std::set<AssetId> models;for(const auto& [object,asset]:editor.document->plan().models)models.insert(asset);for(auto asset:models)ImGui::TextWrapped("%s",asset.text().c_str());ImGui::Text("Resource generation %llu",generation);std::set<StableId> scripts;for(const auto& planned:editor.document->plan().bindings)scripts.insert(planned.binding.asset);for(auto id:scripts){ImGui::PushID(id.text().c_str());if(ImGui::Button("Edit script source"))action(editor,[&]{script_asset=asset_id(id);auto data=nlohmann::json::parse(editor.document->source(script_asset));strncpy_s(script,data.at("source").get<std::string>().c_str(),_TRUNCATE);show_script=true;open_tool(ToolView::ScriptSource);});ImGui::TextWrapped("%s",id.text().c_str());ImGui::PopID();}ImGui::TextDisabled("Source edits use native transactions.");ImGui::End();
    ImGui::Begin("Console");for(const auto& line:editor.console){auto error=line.severity==ConsoleSeverity::Error,warning=line.severity==ConsoleSeverity::Warning;icon_text(error?Icon::Error:warning?Icon::Warning:Icon::Info,error?"Error":warning?"Warning":"Info");ImGui::SameLine();ImGui::TextWrapped("%s",line.message.c_str());}ImGui::End();
    ImGui::Begin("Viewport");const char* modes[]={"Move","Rotate","Scale"};ImGui::SetNextItemWidth(100);ImGui::Combo("##gizmo",&operation,modes,3);ImGui::SameLine();ImGui::TextDisabled("%s | metres | RH Y-up",backend);auto pos=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,1.f);size.y=std::max(size.y,1.f);area={pos.x,pos.y,size.x,size.y};viewport_draw=ImGui::GetWindowDrawList();ImGui::Image(scene_texture,size);
    if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("DA_PLACE");payload&&payload->DataSize==sizeof(int)){int index{};std::memcpy(&index,payload->Data,sizeof(index));
            if(index>=0&&index<static_cast<int>(place_items.size()))action(editor,[&]{
                // The drop position becomes a ray through the scene camera; the object lands where it meets the ground plane.
                using namespace DirectX;double x=0,z=0;if(scene_camera){const auto mouse=ImGui::GetMousePos();const float nx=(mouse.x-area.x)/area.width*2-1,ny=1-(mouse.y-area.y)/area.height*2;const auto inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&scene_view)*XMLoadFloat4x4(&scene_projection));
                    XMFLOAT3 from,to;XMStoreFloat3(&from,XMVector3TransformCoord(XMVectorSet(nx,ny,0,1),inverse));XMStoreFloat3(&to,XMVector3TransformCoord(XMVectorSet(nx,ny,1,1),inverse));const double fall=double(to.y)-from.y;
                    if(std::abs(fall)>1e-6){const double along=(place_height-from.y)/fall;if(along>0&&along<=1){x=from.x+(to.x-from.x)*along;z=from.z+(to.z-from.z)*along;}}x=std::clamp(x,-500.,500.);z=std::clamp(z,-500.,500.);}
                place(editor,place_items[static_cast<std::size_t>(index)],x,z);});}
        ImGui::EndDragDropTarget();}
    if(can_edit&&ImGui::IsWindowFocused()&&!ImGui::GetIO().WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Delete,false))remove_request=editor.selected;
    ImGui::End();
    ImGui::Begin("Place");refresh_place_items(editor);
    if(place_items.empty())ImGui::TextWrapped(editor.assets?"Nothing placeable yet. NPC definitions and imported static models appear here in categories.":"Open a project source mount to place NPCs and models.");
    else{
        // Categories come from the catalogue: one for NPC definitions, one per folder of static models.
        std::vector<std::pair<std::string,unsigned>> categories;for(const auto& item:place_items){if(categories.empty()||categories.back().first!=item.category)categories.push_back({item.category,0});++categories.back().second;}
        if(!place_category.empty()&&std::none_of(categories.begin(),categories.end(),[&](const auto& entry){return entry.first==place_category;}))place_category.clear();
        ImGui::SetNextItemWidth(180);ImGui::InputTextWithHint("##place-filter","Search...",place_filter,sizeof(place_filter));ImGui::SameLine();if(ImGui::Selectable("All",place_category.empty(),0,{40,0}))place_category.clear();
        const float row_end=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;for(const auto& [name,count]:categories){const auto label=name+" ("+std::to_string(count)+")";const float wide=ImGui::CalcTextSize(label.c_str()).x+8;if(ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+wide<row_end)ImGui::SameLine();if(ImGui::Selectable(label.c_str(),place_category==name,0,{wide,0}))place_category=name;}
        ImGui::Separator();ImGui::BeginChild("Place items",{0,-ImGui::GetFrameHeightWithSpacing()});const float tile=170;const int columns=std::max(1,int(ImGui::GetContentRegionAvail().x/(tile+8)));int column=0;std::string heading;
        for(std::size_t index=0;index<place_items.size();++index){const auto& item=place_items[index];if(!place_category.empty()&&item.category!=place_category)continue;
            if(place_filter[0]){auto lower=[](std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;};if(lower(item.name).find(lower(place_filter))==std::string::npos)continue;}
            if(place_category.empty()&&heading!=item.category){heading=item.category;column=0;ImGui::TextDisabled("%s",heading.c_str());}
            if(column++%columns)ImGui::SameLine();ImGui::PushID(static_cast<int>(index));ImGui::BeginDisabled(!can_edit);
            ImGui::Button(item.name.c_str(),{tile,0});
            if(ImGui::IsItemHovered()){ImGui::SetTooltip("%s\n%s\nDrag into the viewport, or double-click to place beside the selection.",item.name.c_str(),item.category.c_str());if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){const auto at=beside_selection(editor,place_height);const auto chosen=item;action(editor,[&]{place(editor,chosen,at.x,at.z,at.yaw);});}}
            if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){const int payload=static_cast<int>(index);ImGui::SetDragDropPayload("DA_PLACE",&payload,sizeof(payload));ImGui::Text("Place %s",item.name.c_str());ImGui::EndDragDropSource();}
            ImGui::EndDisabled();ImGui::PopID();}
        ImGui::EndChild();ImGui::SetNextItemWidth(110);ImGui::InputDouble("Ground height (m)",&place_height,0,0,"%.2f");ImGui::SameLine();ImGui::TextDisabled(can_edit?"Drag into the viewport or double-click to place. Delete removes the selected object.":"Stop Play to place or delete.");
    }
    ImGui::End();
    if(remove_request)action(editor,[&]{remove_object(editor,remove_request);});
    draw_import(editor);return area;
}
void Shell::gizmo(Controller& editor,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection){if(workspace!=Workspace::Scene||editor.preview.active())return;auto handle=editor.document->world().find(editor.selected);if(!editor.document->world().valid(handle))return;auto pose=draft?*draft:editor.document->world().read(handle).transform;using namespace DirectX;XMFLOAT4X4 matrix;XMStoreFloat4x4(&matrix,XMMatrixScaling(pose.scale,pose.scale,pose.scale)*XMMatrixRotationRollPitchYaw(pose.pitch,pose.yaw,pose.roll)*XMMatrixTranslation(pose.x,pose.y,pose.z));ImGuizmo::SetDrawlist(viewport_draw);ImGuizmo::SetRect(area.x,area.y,area.width,area.height);auto op=operation==1?ImGuizmo::ROTATE:operation==2?ImGuizmo::SCALEU:ImGuizmo::TRANSLATE;ImGuizmo::Manipulate(&view._11,&projection._11,op,ImGuizmo::LOCAL,&matrix._11);
    bool using_gizmo=ImGuizmo::IsUsing();if(using_gizmo && !dragging){gesture_object=editor.selected;gesture_revision=editor.document->revision();dragging=true;cancelled=false;}
    if(using_gizmo){float position[3],rotation[3],scale[3];ImGuizmo::DecomposeMatrixToComponents(&matrix._11,position,rotation,scale);pose.x=position[0];pose.y=position[1];pose.z=position[2];pose.pitch=rotation[0]*XM_PI/180;pose.yaw=rotation[1]*XM_PI/180;pose.roll=rotation[2]*XM_PI/180;pose.scale=scale[0];if(!cancelled)draft=pose;else draft.reset();if(ImGui::IsKeyPressed(ImGuiKey_Escape)){cancelled=true;draft.reset();}}
    if(dragging && !using_gizmo){if(!cancelled && draft)action(editor,[&]{auto value=*draft;PropertyChange changes[]={{gesture_object,1,1,value.yaw},{gesture_object,1,2,value.x},{gesture_object,1,3,value.y},{gesture_object,1,4,value.z},{gesture_object,1,5,value.pitch},{gesture_object,1,6,value.roll},{gesture_object,1,7,value.scale}};auto transaction=editor.document->prepare(gesture_revision,changes,scope);editor.document->commit(transaction);editor.log("Committed one transform gesture.");});draft.reset();dragging=false;}
}
}
