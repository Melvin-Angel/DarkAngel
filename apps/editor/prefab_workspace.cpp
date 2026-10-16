#include "editor_ui.hpp"
#include "editor_world_objects.hpp"
#include "prefab_commands.hpp"
#include "reference_picker.hpp"
#include <darkangel/prefab_assets.hpp>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
namespace darkangel::editor_app {
namespace {
using Json=nlohmann::json;
constexpr const char* world_components[]={"altar","mover","pickup","interactable","trigger"};
const char* component_label(const std::string& key){return key=="altar"?"Altar":key=="mover"?"Door or platform":key=="pickup"?"Item pickup":key=="interactable"?"Interactable":key=="trigger"?"Trigger volume":"Component";}
// One field of a component record: numbers, flags, text and short number lists. Returns true when the value changed.
bool edit_value(const char* label,Json& value){
    ImGui::SetNextItemWidth(220);
    if(value.is_boolean()){bool b=value.get<bool>();if(ImGui::Checkbox(label,&b)){value=b;return true;}return false;}
    if(value.is_number_unsigned()){int n=static_cast<int>(value.get<std::uint64_t>());if(ImGui::InputInt(label,&n)&&n>=0){value=static_cast<unsigned>(n);return true;}return false;}
    if(value.is_number()){double n=value.get<double>();if(ImGui::InputDouble(label,&n,0,0,"%.3f")&&std::isfinite(n)){value=n;return true;}return false;}
    if(value.is_string()){char buffer[257]{};auto text=value.get<std::string>();strncpy_s(buffer,text.c_str(),_TRUNCATE);if(ImGui::InputText(label,buffer,sizeof(buffer))){value=std::string(buffer);return true;}return false;}
    if(value.is_array()&&value.size()<=3&&std::all_of(value.begin(),value.end(),[](const Json& v){return v.is_number();})){double n[3]{};for(std::size_t i=0;i<value.size();++i)n[i]=value[i].get<double>();
        bool changed=value.size()==3?ImGui::InputScalarN(label,ImGuiDataType_Double,n,3,nullptr,nullptr,"%.2f"):value.size()==2?ImGui::InputScalarN(label,ImGuiDataType_Double,n,2,nullptr,nullptr,"%.2f"):false;
        if(changed){for(std::size_t i=0;i<value.size();++i)value[i]=n[i];return true;}return false;}
    ImGui::TextDisabled("%s: %s",label,value.dump().c_str());return false;
}
std::string override_key(const std::string& member,const std::string& component,const std::string& field){return member+"|"+component+"|"+field;}
std::vector<std::pair<AssetId,std::string>> prefab_assets(const Controller& editor){std::vector<std::pair<AssetId,std::string>> result;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daprefab")result.push_back({asset.id,std::filesystem::path(asset.path).stem().string()});std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.second<b.second;});return result;}
std::string published(const Controller& editor,AssetId prefab){auto text=editor.prefab_links()(prefab);if(!text)throw std::runtime_error("Save the prefab before using it in a scene");return *text;}
}
Json world_object_defaults(Controller& editor,const std::string& component){
    if(component=="altar")return {{"name","Altar"}};
    if(component=="pickup"){for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem")return {{"item",asset.id.text()},{"quantity",1}};throw std::runtime_error("Create an item in the Items tab before adding a pickup");}
    if(component=="mover")return {{"door",true},{"half",Json::array({1.0,1.5,.25})},{"offset",Json::array({0.0,3.0,0.0})}};
    if(component=="interactable")return {{"prompt","Use"},{"event","object.used"},{"radius",2.0},{"once",true}};
    if(component=="trigger")return {{"event","trigger.entered"},{"half",Json::array({2.0,2.0,2.0})},{"once",true}};
    throw std::runtime_error("Unknown world-object component");
}
StableId place_prefab_instance(Controller& editor,AssetId prefab,const Transform& at){
    const auto text=published(editor,prefab);auto value=Json::parse(text);
    // Models are prepared first, so an instance that cannot be shown never enters the scene.
    for(const auto& [local,entity]:value.at("entities").items())if(entity.contains("model")&&entity["model"].is_string()&&editor.prepare_model)editor.prepare_model(AssetId::parse(entity["model"].get<std::string>()));
    const auto mount=place_prefab(*editor.document,prefab,text,{at.x,at.y,at.z,at.yaw});
    const auto prefix=editor.document->placement().text()+"/"+mount+"/";
    for(const auto& origin:editor.document->plan().origins)if(origin.local_path.starts_with(prefix)){editor.selected=origin.object;break;}
    editor.log("Placed a linked prefab instance. Undo removes it.");return editor.selected;
}
void Shell::draw_prefabs(Controller& editor,unsigned width,unsigned height,float top){
    ImGui::SetNextWindowPos({0,top});ImGui::SetNextWindowSize({float(width),float(height)-top});ImGui::Begin("Prefabs workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(!editor.assets){ImGui::TextWrapped("Open a project source mount to author prefabs.");ImGui::End();return;}
    const bool playing=editor.preview.active()!=nullptr;ImGui::BeginDisabled(playing);
    ImGui::BeginChild("Prefab list",{std::min(280.f*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.32f),0},ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Prefabs");ImGui::InputTextWithHint("##prefab-search","Search prefabs...",prefab_filter,sizeof(prefab_filter));
    ImGui::SetNextItemWidth(160);ImGui::InputText("##prefab-name",prefab_name,sizeof(prefab_name));ImGui::SameLine();
    if(ImGui::Button("Create prefab")){try{prefab_selection=editor.authoring.create_prefab(*editor.assets,prefab_name);editor.refresh_assets();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();}}
    const auto prefabs=prefab_assets(editor);
    if(prefab_selection!=AssetId{}&&!editor.authoring.drafts.contains(prefab_selection)&&std::none_of(prefabs.begin(),prefabs.end(),[&](const auto& p){return p.first==prefab_selection;}))prefab_selection={};
    if(prefab_selection==AssetId{}&&!prefabs.empty())prefab_selection=prefabs.front().first;
    for(const auto& [id,name]:prefabs){if(!reference_search(name,prefab_filter))continue;ImGui::PushID(id.text().c_str());bool dirty=false;if(auto draft=editor.authoring.drafts.find(id);draft!=editor.authoring.drafts.end())dirty=draft->second.dirty();if(ImGui::Selectable((name+(dirty?" *":"")).c_str(),prefab_selection==id))prefab_selection=id;ImGui::PopID();}
    for(const auto& [id,draft]:editor.authoring.drafts)if(draft.pending&&std::filesystem::path(draft.asset.path).extension()==".daprefab"){ImGui::PushID(id.text().c_str());if(ImGui::Selectable((std::filesystem::path(draft.asset.path).stem().string()+" (new)").c_str(),prefab_selection==id))prefab_selection=id;ImGui::PopID();}
    if(prefabs.empty())ImGui::TextWrapped("No prefabs yet. Name one and choose Create prefab, or use Create Prefab from Selection in the Scene Inspector.");
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Prefab definition");
    try{if(prefab_selection!=AssetId{}){auto& draft=editor.authoring.open(*editor.assets,prefab_selection);auto& value=draft.value;
        ImGui::Text("Editing prefab definition: %s",value.at("name").get<std::string>().c_str());
        ImGui::TextDisabled("%s  |  %s",draft.asset.path.c_str(),draft.pending?"New draft: Save publishes it":draft.dirty()?"Unsaved prefab changes":"Saved");
        ImGui::TextWrapped("Changes here are the shared defaults of every instance. Instances keep their placement and their own overrides; saved changes reach open scenes at once and Play on its next fresh start.");
        auto& entities=value["entities"];
        for(auto& [local,entity]:entities.items()){ImGui::PushID(local.c_str());ImGui::SeparatorText(entity.value("name",local).c_str());
            {Json name=entity.value("name",local);if(edit_value("Object name",name))entity["name"]=name;}
            // Model reference: any imported static model; replacing it here changes every instance.
            std::string current="No model";AssetId model;if(entity.contains("model")&&entity["model"].is_string()){model=AssetId::parse(entity["model"].get<std::string>());current=model.text();for(const auto& item:place_items)if(item.id==model)current=item.name;}
            ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Model",current.c_str())){if(ImGui::Selectable("No model",model==AssetId{}))entity.erase("model");for(const auto& item:place_items)if(!item.npc&&item.gameplay==0&&ImGui::Selectable(item.name.c_str(),item.id==model)){entity["model"]=item.id.text();if(editor.prepare_model)editor.prepare_model(item.id);}ImGui::EndCombo();}
            for(const char* component:world_components){if(!entity.contains(component)||entity[component].is_null())continue;ImGui::PushID(component);
                if(ImGui::CollapsingHeader(component_label(component),ImGuiTreeNodeFlags_DefaultOpen)){for(auto& [field,field_value]:entity[component].items())edit_value(field.c_str(),field_value);if(ImGui::SmallButton("Remove component"))entity.erase(component);}
                ImGui::PopID();}
            if(ImGui::Button("Add component"))ImGui::OpenPopup("add-component");
            if(ImGui::BeginPopup("add-component")){for(const char* component:world_components)if(!entity.contains(component)||entity[component].is_null()){if(ImGui::Selectable(component_label(component))){try{entity[component]=world_object_defaults(editor,component);}catch(const std::exception& error){author_diagnostic=error.what();}}}ImGui::EndPopup();}
            ImGui::PopID();}
        editor.authoring.record_changes(*editor.assets,"Edit prefab");
        try{decode_prefab(value.dump());ImGui::TextDisabled("Definition is valid.");}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Needs attention: %s. Save will reject it.",error.what());}
        if(ImGui::Button("Save prefab")){editor.save_gameplay();editor.refresh_assets();author_diagnostic="Saved. Scenes that link this prefab now show the new definition.";}ImGui::SameLine();
        ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo")){editor.authoring.undo(*editor.assets);}ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo")){editor.authoring.redo(*editor.assets);}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Place in scene")){const auto id=prefab_selection;select_workspace(Workspace::Scene);place_category="Prefabs";(void)id;}
        if(editor.document){std::size_t used=0;for(const auto& instance:prefab_instances(*editor.document))if(instance.prefab==prefab_selection)++used;ImGui::TextDisabled("Instances in the open scene: %zu",used);}
    }else ImGui::TextDisabled("Select or create a prefab.");}catch(const std::exception& error){author_diagnostic=error.what();}
    if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());
    ImGui::EndChild();ImGui::EndDisabled();if(playing)ImGui::TextDisabled("Stop Play to edit prefabs.");ImGui::End();
}
bool Shell::draw_prefab_instance(Controller& editor,StableId object){
    std::optional<PrefabInstanceRef> instance;try{instance=prefab_instance_of(*editor.document,object);}catch(const std::exception&){}
    if(!instance)return false;
    auto& document=*editor.document;const auto mount=instance->mount,member=instance->member;const auto prefab=instance->prefab;
    std::string prefab_name=prefab.text();for(const auto& asset:editor.asset_inventory)if(asset.id==prefab)prefab_name=std::filesystem::path(asset.path).stem().string();
    auto run=[&](auto&& f){try{f();}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}};
    if(!ImGui::CollapsingHeader(("Prefab instance: "+prefab_name).c_str(),ImGuiTreeNodeFlags_DefaultOpen))return true;
    ImGui::TextDisabled("Editing instance. Overrides apply to this placement only.");
    if(ImGui::Button("Open Prefab")){prefab_selection=prefab;select_workspace(Workspace::Prefabs);return true;}ImGui::SameLine();
    if(ImGui::Button("Duplicate instance")){run([&]{auto copy=duplicate_instance(document,mount,{2,0,0,0});const auto prefix=document.placement().text()+"/"+copy+"/";for(const auto& origin:document.plan().origins)if(origin.local_path.starts_with(prefix)){editor.selected=origin.object;break;}});return true;}ImGui::SameLine();
    if(ImGui::Button("Delete instance")){run([&]{delete_instance(document,mount);editor.selected=document.plan().origins.empty()?StableId{}:document.plan().origins.front().object;});return true;}
    {auto at=instance_placement(document,mount);double values[4]{at.x,at.y,at.z,at.yaw*57.29577951308232};ImGui::SetNextItemWidth(260);
        if(ImGui::InputScalarN("Placement x y z",ImGuiDataType_Double,values,3,nullptr,nullptr,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)|ImGui::InputDouble("Yaw (deg)",&values[3],0,0,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue)){run([&]{set_instance_placement(document,mount,{values[0],values[1],values[2],values[3]/57.29577951308232});});return true;}}
    // Component fields: inherited values look normal; overridden ones are marked and can be reverted one by one.
    Json effective,source;try{effective=instance_member(document,mount,member);source=prefab_member(document,prefab,member);}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Instance needs repair: %s",error.what());return true;}
    for(const char* component:world_components){
        const bool has=effective.contains(component)&&!effective[component].is_null(),inherited=source.contains(component)&&!source[component].is_null();
        if(!has){if(inherited){ImGui::TextDisabled("%s removed on this instance",component_label(component));ImGui::SameLine();ImGui::PushID(component);if(ImGui::SmallButton("Restore")){run([&]{revert_overrides(document,mount,{{member,component,""}});});ImGui::PopID();return true;}ImGui::PopID();}continue;}
        ImGui::PushID(component);
        if(ImGui::CollapsingHeader((std::string(component_label(component))+(inherited?"":" (added on this instance)")).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){
            for(auto& [field,value]:effective[component].items()){const bool overridden=!inherited||!source[component].contains(field)||source[component][field]!=value;
                ImGui::PushID(field.c_str());if(overridden&&inherited){auto key=override_key(member,component,field);bool picked=prefab_override_selection.contains(key);if(ImGui::Checkbox("##select",&picked)){if(picked)prefab_override_selection.insert(key);else prefab_override_selection.erase(key);}ImGui::SameLine();}
                auto edited=value;if(edit_value((overridden&&inherited?"* "+field:field).c_str(),edited)){const auto chosen=edited;const std::string name=field;ImGui::PopID();ImGui::PopID();run([&]{set_instance_field(document,mount,member,component,name,chosen);});return true;}
                if(overridden&&inherited){ImGui::SameLine();if(ImGui::SmallButton("Revert")){const std::string name=field;ImGui::PopID();ImGui::PopID();run([&]{revert_overrides(document,mount,{{member,component,name}});});return true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Prefab value: %s",source[component][field].dump().c_str());}
                ImGui::PopID();}
            if(ImGui::SmallButton(inherited?"Remove on this instance":"Remove added component")){ImGui::PopID();run([&]{remove_instance_component(document,mount,member,component);});return true;}}
        ImGui::PopID();}
    if(ImGui::Button("Add component"))ImGui::OpenPopup("instance-add-component");
    if(ImGui::BeginPopup("instance-add-component")){for(const char* component:world_components)if(!effective.contains(component)||effective[component].is_null())if(ImGui::Selectable(component_label(component))){const std::string chosen=component;run([&]{add_instance_component(document,mount,member,chosen,world_object_defaults(editor,chosen));});}ImGui::EndPopup();}
    // Overrides panel: Revert and Apply Selected act on the checked fields only.
    const auto overrides=instance_overrides(document,mount);
    if(ImGui::TreeNodeEx("Overrides",ImGuiTreeNodeFlags_DefaultOpen,"Overrides (%zu)",overrides.size())){
        for(const auto& entry:overrides)ImGui::BulletText("%s %s%s%s: prefab %s, instance %s",entry.kind==PrefabOverride::AddedComponent?"Added":entry.kind==PrefabOverride::RemovedComponent?"Removed":"Field",component_label(entry.component),entry.field.empty()?"":".",entry.field.c_str(),entry.prefab_value.dump().c_str(),entry.instance_value.dump().c_str());
        std::vector<OverrideKey> keys;for(const auto& text:prefab_override_selection){auto a=text.find('|'),b=text.find('|',a+1);if(text.substr(0,a)==member)keys.push_back({text.substr(0,a),text.substr(a+1,b-a-1),text.substr(b+1)});}
        ImGui::BeginDisabled(keys.empty());
        if(ImGui::Button("Revert selected")){run([&]{revert_overrides(document,mount,keys);prefab_override_selection.clear();});ImGui::EndDisabled();ImGui::TreePop();return true;}ImGui::SameLine();
        if(ImGui::Button("Apply selected to prefab")){run([&]{
            // Apply publishes the definition first; the scene then drops the same overrides. Between the two steps the instance
            // resolves to the same values, so a failure at either step leaves Play unchanged.
            auto& draft=editor.authoring.open(*editor.assets,prefab);if(draft.pending||draft.dirty())throw std::runtime_error("Save or undo the prefab's own unsaved changes before applying overrides to it");
            auto updated=prefab_with_overrides(document,mount,keys,draft.value.dump());editor.authoring.apply(*editor.assets,prefab,editor.authoring.revision(),Json::parse(updated),"Apply instance overrides to prefab");
            try{editor.authoring.save(*editor.assets,prefab);}catch(...){editor.authoring.undo(*editor.assets);throw;}
            editor.refresh_assets();editor.refresh_prefab_links();revert_overrides(document,mount,keys);prefab_override_selection.clear();editor.log("Applied the selected overrides to "+prefab_name+"; other instances follow the new definition.");});
            ImGui::EndDisabled();ImGui::TreePop();return true;}
        ImGui::EndDisabled();if(keys.empty())ImGui::TextDisabled("Check overridden fields to revert or apply them.");ImGui::TreePop();}
    // Swap: same placement and persistent identity, a different compatible prefab.
    if(ImGui::TreeNode("Swap prefab")){
        std::string label="Choose replacement";for(const auto& [id,name]:prefab_assets(editor))if(id==prefab_swap_target)label=name;
        ImGui::SetNextItemWidth(220);if(ImGui::BeginCombo("Replacement",label.c_str())){for(const auto& [id,name]:prefab_assets(editor))if(id!=prefab&&ImGui::Selectable(name.c_str(),id==prefab_swap_target))prefab_swap_target=id;ImGui::EndCombo();}
        ImGui::Checkbox("Use replacement defaults (discard this instance's component overrides)",&prefab_swap_defaults);
        if(prefab_swap_target!=AssetId{}){try{const auto text=published(editor,prefab_swap_target);auto preview=prepare_swap(document,mount,prefab_swap_target,text,prefab_swap_defaults);
            ImGui::TextWrapped("Kept: placement, mount and the object's persistent identity (world state such as discovery or collection).");
            for(const auto& carried:preview.carried)ImGui::BulletText("Carried: %s",carried.c_str());for(const auto& dropped:preview.discarded)ImGui::BulletText("Discarded: %s",dropped.c_str());
            if(!preview.compatible)ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",preview.reason.c_str());
            ImGui::BeginDisabled(!preview.compatible);if(ImGui::Button("Swap")){const auto target=prefab_swap_target;const bool defaults=prefab_swap_defaults;ImGui::EndDisabled();ImGui::TreePop();
                run([&]{const auto replacement=Json::parse(text);for(const auto& [local,entity]:replacement.at("entities").items())if(entity.contains("model")&&entity["model"].is_string()&&editor.prepare_model)editor.prepare_model(AssetId::parse(entity["model"].get<std::string>()));commit_swap(document,mount,target,text,defaults);
                    const auto prefix=document.placement().text()+"/"+mount+"/";for(const auto& origin:document.plan().origins)if(origin.local_path.starts_with(prefix)){editor.selected=origin.object;break;}prefab_swap_target={};});return true;}ImGui::EndDisabled();
        }catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}}
        ImGui::TreePop();}
    return true;
}
void Shell::draw_create_prefab(Controller& editor,StableId object){
    if(!editor.assets||!ImGui::TreeNode("Create Prefab from Selection"))return;
    ImGui::TextWrapped("Makes a reusable prefab from this object (its model and world-object components) and replaces the object with a linked instance at the same place. The object keeps its persistent identity, so saved world state stays with it.");
    ImGui::SetNextItemWidth(180);ImGui::InputText("Prefab name",prefab_name,sizeof(prefab_name));
    if(ImGui::Button("Create prefab and link")){try{
        auto id=AssetId::random();auto text=prefab_from_object(*editor.document,object,id,prefab_name);
        // Publish the definition first. If the scene edit then fails, the original object is untouched.
        auto created=editor.authoring.create_prefab_source(*editor.assets,prefab_name,Json::parse(text));editor.authoring.save(*editor.assets,created);editor.refresh_assets();
        auto published_text=editor.prefab_links()(created);if(!published_text)throw std::runtime_error("The new prefab was not published");
        replace_with_instance(*editor.document,object,created,*published_text);
        editor.log("Created prefab "+std::string(prefab_name)+" and replaced the object with a linked instance (same persistent identity).");}
        catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}
    ImGui::TreePop();
}
}
