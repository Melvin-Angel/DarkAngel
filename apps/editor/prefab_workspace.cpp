#include "editor_ui.hpp"
#include "editor_world_objects.hpp"
#include "prefab_commands.hpp"
#include "reference_picker.hpp"
#include <darkangel/prefab_assets.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
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
std::string prefab_label(const Controller& editor,AssetId prefab){for(const auto& asset:editor.asset_inventory)if(asset.id==prefab)return std::filesystem::path(asset.path).stem().string();return prefab.text();}
std::string published(const Controller& editor,AssetId prefab){auto text=editor.prefab_links()(prefab);if(!text)throw std::runtime_error("Save the prefab before using it in a scene");return *text;}
// The published prefabs a definition mounts or derives from, transitively (nested prefabs and variant bases resolve against
// saved definitions, as scenes do).
AssemblySources dependencies_of(const Controller& editor,const std::string& text){
    AssemblySources result;for(auto inner:prefab_references(text))for(auto& [id,source]:EditorDocument::link_closure(inner,published(editor,inner),editor.prefab_links()))result.emplace(id,std::move(source));return result;}
// A published prefab's own dependencies, without itself.
AssemblySources dependencies_of(const Controller& editor,AssetId prefab,const std::string& text){auto closure=EditorDocument::link_closure(prefab,text,editor.prefab_links());closure.erase(prefab);return closure;}
void prepare_models(Controller& editor,const Json& entities){for(const auto& [local,entity]:entities.items())if(entity.contains("model")&&entity["model"].is_string()&&editor.prepare_model)editor.prepare_model(AssetId::parse(entity["model"].get<std::string>()));}
// The placement a preview resolves the definition at: an identity no scene uses.
const StableId preview_placement=StableId::parse("7f0000000000000000000000000000b4");
Transform compose(const PrefabPlacement& at,Transform pose){const double c=std::cos(at.yaw),s=std::sin(at.yaw),x=pose.x,z=pose.z;pose.x=at.x+c*x+s*z;pose.y+=at.y;pose.z=at.z-s*x+c*z;pose.yaw+=at.yaw;return pose;}
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
    const auto text=published(editor,prefab);const auto dependencies=dependencies_of(editor,prefab,text);
    // Models are prepared first, so an instance that cannot be shown never enters the scene.
    AssemblySources all=dependencies;all[prefab]=text;for(const auto& [id,source]:all)prepare_models(editor,Json::parse(source).value("entities",Json::object()));
    const auto mount=place_prefab(*editor.document,prefab,text,{at.x,at.y,at.z,at.yaw},dependencies);
    const auto prefix=editor.document->placement().text()+"/"+mount+"/";
    for(const auto& origin:editor.document->plan().origins)if(origin.local_path.starts_with(prefix)){editor.select_mount(mount,origin.object);break;}
    editor.log("Placed a linked prefab instance. Undo removes it.");return editor.selected;
}
void Shell::draw_prefabs(Controller& editor,unsigned width,unsigned height,float top,ImTextureID scene_texture){
    prefab_preview.visible=false;
    ImGui::SetNextWindowPos({0,top});ImGui::SetNextWindowSize({float(width),float(height)-top});ImGui::Begin("Prefabs workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(!editor.assets){ImGui::TextWrapped("Open a project source mount to author prefabs.");ImGui::End();return;}
    const bool playing=editor.preview.active()!=nullptr;ImGui::BeginDisabled(playing);
    ImGui::BeginChild("Prefab list",{std::min(280.f*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.25f),0},ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Prefabs");ImGui::InputTextWithHint("##prefab-search","Search prefabs...",prefab_filter,sizeof(prefab_filter));
    ImGui::SetNextItemWidth(160);ImGui::InputText("##prefab-name",prefab_name,sizeof(prefab_name));ImGui::SameLine();
    if(ImGui::Button("Create prefab")){try{prefab_selection=editor.authoring.create_prefab(*editor.assets,prefab_name);editor.refresh_assets();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();}}
    const auto prefabs=prefab_assets(editor);
    if(prefab_selection!=AssetId{}&&!editor.authoring.drafts.contains(prefab_selection)&&std::none_of(prefabs.begin(),prefabs.end(),[&](const auto& p){return p.first==prefab_selection;}))prefab_selection={};
    if(prefab_selection==AssetId{}&&!prefabs.empty())prefab_selection=prefabs.front().first;
    for(const auto& [id,name]:prefabs){if(!reference_search(name,prefab_filter))continue;ImGui::PushID(id.text().c_str());bool dirty=false;if(auto draft=editor.authoring.drafts.find(id);draft!=editor.authoring.drafts.end())dirty=draft->second.dirty();if(ImGui::Selectable((name+(dirty?" *":"")).c_str(),prefab_selection==id))prefab_selection=id;ImGui::PopID();}
    for(const auto& [id,draft]:editor.authoring.drafts)if(draft.pending&&std::filesystem::path(draft.asset.path).extension()==".daprefab"){ImGui::PushID(id.text().c_str());if(ImGui::Selectable((std::filesystem::path(draft.asset.path).stem().string()+" (new)").c_str(),prefab_selection==id))prefab_selection=id;ImGui::PopID();}
    if(prefabs.empty())ImGui::TextWrapped("No prefabs yet. Name one and choose Create prefab, or use Create Prefab from Selection in the Scene Inspector.");
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Prefab definition",{ImGui::GetContentRegionAvail().x*.55f,0});
    try{if(prefab_selection!=AssetId{}){auto& draft=editor.authoring.open(*editor.assets,prefab_selection);auto& value=draft.value;const auto selection=prefab_selection;
        const bool variant=value.contains("base")&&!value["base"].is_null();
        ImGui::Text("Editing prefab definition: %s",value.at("name").get<std::string>().c_str());
        ImGui::TextDisabled("%s  |  %s",draft.asset.path.c_str(),draft.pending?"New draft: Save publishes it":draft.dirty()?"Unsaved prefab changes":"Saved");
        ImGui::TextWrapped("Changes here are the shared defaults of every instance. Instances keep their placement and their own overrides; saved changes reach open scenes at once and Play on its next fresh start.");
        {Json name=value.at("name");if(edit_value("Prefab name",name)&&!name.get<std::string>().empty())value["name"]=name;}
        AssemblySources dependencies;std::string dependency_error;try{dependencies=dependencies_of(editor,value.dump());}catch(const std::exception& error){dependency_error=error.what();}
        if(variant){
            // A variant: its base's members changed by its own typed patches. A base edit needs an explicit rebase.
            const auto base=AssetId::parse(value["base"].get<std::string>());ImGui::SeparatorText(("Variant of "+prefab_label(editor,base)).c_str());
            std::string base_text;try{base_text=published(editor,base);}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}
            if(!base_text.empty()){
                if(value.value("base_fingerprint","")!=prefab_fingerprint(base_text)){
                    ImGui::TextColored({1.f,.75f,.3f,1.f},"The base prefab changed since this variant was made or rebased. Play refuses it until you rebase.");
                    const auto conflicts=variant_conflicts(value,base_text);for(const auto& conflict:conflicts)ImGui::BulletText("Needs repair: %s",conflict.c_str());
                    ImGui::BeginDisabled(!conflicts.empty());if(ImGui::Button("Rebase onto the current base")){value=rebase_variant(value,base_text);author_diagnostic="Rebased. Save to publish the variant.";}ImGui::EndDisabled();}
                ImGui::SameLine();if(ImGui::SmallButton("Open base"))prefab_selection=base;
                Json entities;try{entities=prefab_entities(selection,[&]{auto all=dependencies;all[selection]=value.dump();all[base]=base_text;return all;}());}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}
                const auto base_entities=Json::parse(base_text).value("entities",Json::object());
                for(auto& [local,entity]:entities.items()){ImGui::PushID(local.c_str());ImGui::SeparatorText(entity.value("name",local).c_str());
                    draw_material_assignments(editor,entity,[&](AssetId slot,AssetId material){set_variant_field(value,base_text,local,"materials",slot.text(),material==AssetId{}?Json():Json(material.text()));});
            for(const char* component:world_components){if(!entity.contains(component)||entity[component].is_null())continue;ImGui::PushID(component);
                        if(ImGui::CollapsingHeader(component_label(component),ImGuiTreeNodeFlags_DefaultOpen))for(auto& [field,field_value]:entity[component].items()){
                            const bool patched=!base_entities.contains(local)||!base_entities[local].contains(component)||!base_entities[local][component].contains(field)||base_entities[local][component][field]!=field_value;
                            auto edited=field_value;if(edit_value((patched?"* "+field:field).c_str(),edited)){try{set_variant_field(value,base_text,local,component,field,edited);}catch(const std::exception& error){author_diagnostic=error.what();}}}
                        ImGui::PopID();}
                    ImGui::PopID();}
                const auto patches=value.value("patches",Json::array());
                if(ImGui::TreeNodeEx("Variant patches",ImGuiTreeNodeFlags_DefaultOpen,"Variant patches (%zu)",patches.size())){std::optional<std::size_t> removed;
                    for(std::size_t index=0;index<patches.size();++index){ImGui::PushID(static_cast<int>(index));ImGui::BulletText("%s %s%s%s = %s",patches[index].value("op","").c_str(),patches[index].value("component","").c_str(),patches[index].contains("field")?".":"",patches[index].value("field","").c_str(),patches[index].contains("value")?patches[index]["value"].dump().c_str():"");ImGui::SameLine();if(ImGui::SmallButton("Revert"))removed=index;ImGui::PopID();}
                    if(removed)value["patches"].erase(*removed);ImGui::TreePop();}}
        }else{
        auto& entities=value["entities"];
        for(auto& [local,entity]:entities.items()){ImGui::PushID(local.c_str());ImGui::SeparatorText(entity.value("name",local).c_str());
            {Json name=entity.value("name",local);if(edit_value("Object name",name))entity["name"]=name;}
            // Model reference: any imported static model; replacing it here changes every instance.
            std::string current="No model";AssetId model;if(entity.contains("model")&&entity["model"].is_string()){model=AssetId::parse(entity["model"].get<std::string>());current=model.text();for(const auto& item:place_items)if(item.id==model)current=item.name;}
            ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Model",current.c_str())){if(ImGui::Selectable("No model",model==AssetId{}))entity.erase("model");for(const auto& item:place_items)if(!item.npc&&item.gameplay==0&&ImGui::Selectable(item.name.c_str(),item.id==model)){entity["model"]=item.id.text();if(editor.prepare_model)editor.prepare_model(item.id);}ImGui::EndCombo();}
            draw_material_assignments(editor,entity,[&](AssetId slot,AssetId material){if(material==AssetId{}){if(entity.contains("materials"))entity["materials"].erase(slot.text());}else entity["materials"][slot.text()]=material.text();});
            for(const char* component:world_components){if(!entity.contains(component)||entity[component].is_null())continue;ImGui::PushID(component);
                if(ImGui::CollapsingHeader(component_label(component),ImGuiTreeNodeFlags_DefaultOpen)){for(auto& [field,field_value]:entity[component].items())edit_value(field.c_str(),field_value);if(ImGui::SmallButton("Remove component"))entity.erase(component);}
                ImGui::PopID();}
            if(ImGui::Button("Add component"))ImGui::OpenPopup("add-component");
            if(ImGui::BeginPopup("add-component")){for(const char* component:world_components)if(!entity.contains(component)||entity[component].is_null()){if(ImGui::Selectable(component_label(component))){try{entity[component]=world_object_defaults(editor,component);}catch(const std::exception& error){author_diagnostic=error.what();}}}ImGui::EndPopup();}
            ImGui::PopID();}
        // Nested prefabs: other saved prefabs this one mounts, each with its own placement inside this prefab.
        ImGui::SeparatorText("Nested prefabs");
        if(value.contains("mounts")&&value["mounts"].is_object()){std::optional<std::string> removed;
            for(auto& [key,mount]:value["mounts"].items()){ImGui::PushID(key.c_str());const auto inner=AssetId::parse(mount.at("asset").get<std::string>());ImGui::TextUnformatted(prefab_label(editor,inner).c_str());ImGui::SameLine();if(ImGui::SmallButton("Open"))prefab_selection=inner;ImGui::SameLine();if(ImGui::SmallButton("Remove"))removed=key;
                if(!mount.contains("placement")||mount["placement"].is_null())mount["placement"]={{"x",0.0},{"y",0.0},{"z",0.0},{"yaw",0.0}};for(const char* axis:{"x","y","z","yaw"}){Json number=mount["placement"].value(axis,0.0);if(edit_value(axis,number))mount["placement"][axis]=number;}
                ImGui::PopID();}
            if(removed){value["mounts"].erase(*removed);if(value["mounts"].empty())value.erase("mounts");}}
        {std::string label="Choose a prefab";for(const auto& [id,name]:prefabs)if(id==prefab_nested_choice)label=name;ImGui::SetNextItemWidth(200);
         if(ImGui::BeginCombo("##nested",label.c_str())){for(const auto& [id,name]:prefabs)if(id!=selection&&ImGui::Selectable(name.c_str(),id==prefab_nested_choice))prefab_nested_choice=id;ImGui::EndCombo();}ImGui::SameLine();
         ImGui::BeginDisabled(prefab_nested_choice==AssetId{});if(ImGui::Button("Add nested prefab")){if(!value.contains("mounts")||!value["mounts"].is_object())value["mounts"]=Json::object();auto key=AssetId::random().text();key.erase(std::remove(key.begin(),key.end(),'-'),key.end());value["mounts"][key]={{"asset",prefab_nested_choice.text()},{"placement",{{"x",0.0},{"y",0.0},{"z",0.0},{"yaw",0.0}}}};}ImGui::EndDisabled();}
        }
        editor.authoring.record_changes(*editor.assets,"Edit prefab");
        if(!dependency_error.empty())ImGui::TextColored({1.f,.55f,.3f,1.f},"Needs attention: %s",dependency_error.c_str());
        else try{decode_prefab(value.dump(),dependencies);ImGui::TextDisabled("Definition is valid.");}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Needs attention: %s. Save will reject it.",error.what());}
        if(ImGui::Button("Save prefab")){editor.save_gameplay();editor.refresh_assets();prefab_consumers_stale=true;author_diagnostic="Saved. Scenes that link this prefab now show the new definition.";}ImGui::SameLine();
        ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo")){editor.authoring.undo(*editor.assets);}ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo")){editor.authoring.redo(*editor.assets);}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("Place in scene")){select_workspace(Workspace::Scene);place_category="Prefabs";}
        ImGui::SameLine();if(ImGui::Button("Reveal in Assets"))reveal_asset(selection);
        std::size_t used=0;if(editor.document){for(const auto& instance:prefab_instances(*editor.document))if(instance.prefab==selection)++used;ImGui::TextDisabled("Instances in the open scene: %zu",used);}
        // Variants and consumers: who uses this prefab, which decides whether it can be deleted.
        ImGui::SeparatorText("Variants, file and users");
        if(!variant&&!draft.pending){ImGui::SetNextItemWidth(160);ImGui::InputText("##variant-name",prefab_variant_name,sizeof(prefab_variant_name));ImGui::SameLine();
            if(ImGui::Button("Create variant")){try{auto id=AssetId::random();auto made=make_variant(id,prefab_variant_name,selection,published(editor,selection));auto created=editor.authoring.create_prefab_source(*editor.assets,prefab_variant_name,made);editor.authoring.save(*editor.assets,created);editor.refresh_assets();prefab_selection=created;prefab_consumers_stale=true;author_diagnostic="Created a variant. It follows its base until it patches a field.";}catch(const std::exception& error){author_diagnostic=error.what();}}}
        if(prefab_consumers_for!=selection||prefab_consumers_stale){prefab_consumer_list=prefab_consumers(editor.assets->source_root(),selection);prefab_consumers_for=selection;prefab_consumers_stale=false;}
        if(prefab_consumer_list.empty())ImGui::TextDisabled("No saved scene or prefab uses this prefab.");
        else{ImGui::TextUnformatted("Used by:");for(const auto& consumer:prefab_consumer_list)ImGui::BulletText("%s (%s)",consumer.source.c_str(),consumer.kind.c_str());}
        if(!draft.pending){ImGui::SetNextItemWidth(160);ImGui::InputTextWithHint("##rename","New file name",prefab_rename,sizeof(prefab_rename));ImGui::SameLine();
            ImGui::BeginDisabled(!prefab_rename[0]||draft.dirty());if(ImGui::Button("Rename file")){try{editor.authoring.rename_prefab(*editor.assets,selection,prefab_rename);editor.refresh_assets();prefab_rename[0]=0;prefab_consumers_stale=true;author_diagnostic="Renamed. Scenes link the prefab by identity, so they are unaffected.";}catch(const std::exception& error){author_diagnostic=error.what();}}ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Moves the source file. Its identity, and so every scene and prefab that uses it, is unchanged.");}
        const bool deletable=prefab_consumer_list.empty()&&used==0;ImGui::SameLine();ImGui::BeginDisabled(!deletable);if(ImGui::Button("Delete prefab"))ImGui::OpenPopup("Delete selected prefab?");ImGui::EndDisabled();
        if(!deletable&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Remove its instances (and nested uses) listed above before deleting it.");
        if(ImGui::BeginPopupModal("Delete selected prefab?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Delete this prefab and discard its draft? Published source bytes are kept in the project cache.");
            if(ImGui::Button("Delete")){try{editor.authoring.delete_prefab(*editor.assets,selection);prefab_selection={};editor.refresh_assets();prefab_consumers_stale=true;author_diagnostic="Deleted prefab.";ImGui::CloseCurrentPopup();}catch(const std::exception& error){author_diagnostic=error.what();}}
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        // Preview inputs: the draft as it stands, and the instance shown in context.
        if(prefab_selection==selection){
            std::optional<std::string> context;if(prefab_preview.in_context&&editor.document)for(const auto& instance:prefab_instances(*editor.document))if(instance.prefab==selection){context=instance.mount;break;}
            const auto key=selection.text()+"|"+std::to_string(editor.authoring.revision())+"|"+std::to_string(std::hash<std::string>{}(value.dump()))+"|"+(context?*context+"|"+std::to_string(editor.document->revision()):std::string{});
            if(key!=prefab_preview.key){prefab_preview.key=key;try{
                AssemblySources linked=dependencies;EditorDocument resolved(selection,preview_placement,{{selection,value.dump()}},1,linked);
                editor.prepare_render_materials(resolved.plan());std::vector<PrefabPreviewModel> models;PrefabPlacement at;if(context)at=instance_placement(*editor.document,*context);
                for(const auto& [object,model]:resolved.plan().models){if(editor.prepare_model)editor.prepare_model(model);auto pose=resolved.world().read(resolved.world().find(object)).transform;models.push_back({object,model,context?compose(at,pose):pose,false,resolved.plan().materials.contains(object)?resolved.plan().materials.at(object):std::map<AssetId,AssetId>{}});}
                std::array<double,3> low{1e9,1e9,1e9},high{-1e9,-1e9,-1e9};for(const auto& shown:models){const double p[3]{shown.pose.x,shown.pose.y,shown.pose.z};for(int axis=0;axis<3;++axis){low[axis]=std::min(low[axis],p[axis]);high[axis]=std::max(high[axis],p[axis]);}}
                if(models.empty()){low={at.x,at.y,at.z};high=low;}
                for(int axis=0;axis<3;++axis)prefab_preview.center[axis]=(low[axis]+high[axis])*.5;prefab_preview.center[1]+=1;
                prefab_preview.radius=std::max(1.5,std::hypot(high[0]-low[0],high[2]-low[2])*.5+1.5);
                // In context: the open scene's other models near the instance, drawn but not part of the definition.
                if(context){const auto prefix=editor.document->placement().text()+"/"+*context+"/";std::set<StableId> own;for(const auto& origin:editor.document->plan().origins)if(origin.local_path.starts_with(prefix))own.insert(origin.object);
                    for(const auto& [object,model]:editor.document->plan().models){if(own.contains(object))continue;auto handle=editor.document->world().find(object);if(!editor.document->world().valid(handle))continue;auto pose=editor.document->world().read(handle).transform;
                        if(std::hypot(pose.x-at.x,pose.z-at.z)<=30&&models.size()<512)models.push_back({object,model,pose,true,editor.document->plan().materials.contains(object)?editor.document->plan().materials.at(object):std::map<AssetId,AssetId>{}});}}
                prefab_preview.models=std::move(models);prefab_preview.error.clear();}
                catch(const std::exception& error){prefab_preview.error=error.what();}}}
    }else ImGui::TextDisabled("Select or create a prefab.");}catch(const std::exception& error){author_diagnostic=error.what();}
    if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());
    ImGui::EndChild();ImGui::SameLine();
    // The preview pane: the definition from an orbit camera (drag to turn), or its instance among the scene around it.
    ImGui::BeginChild("Prefab preview");
    if(prefab_selection!=AssetId{}){
        bool context=prefab_preview.in_context;if(ImGui::Checkbox("Show in context",&context))prefab_preview.in_context=context;
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Shows the draft at its first instance in the open scene, with the scene around it. The surroundings are not edited here.");
        ImGui::SameLine();ImGui::TextDisabled(prefab_preview.in_context?"In context: surrounding scene shown, locked":"Isolated definition");
        if(!prefab_preview.error.empty())ImGui::TextColored({1.f,.55f,.3f,1.f},"Stale preview: %s",prefab_preview.error.c_str());
        const auto pos=ImGui::GetCursorScreenPos();auto size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,64.f);size.y=std::max(size.y,64.f);
        area={pos.x,pos.y,size.x,size.y};ImGui::Image(scene_texture,size);prefab_preview.visible=true;probe("prefab-preview");
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){const auto delta=ImGui::GetIO().MouseDelta;prefab_preview.yaw-=delta.x*.01f;prefab_preview.pitch=std::clamp(prefab_preview.pitch+delta.y*.01f,-.2f,1.4f);}
        if(ImGui::IsItemHovered()&&ImGui::GetIO().MouseWheel!=0)prefab_preview.radius=std::clamp(prefab_preview.radius*(ImGui::GetIO().MouseWheel>0?.9:1.1),.5,500.);
    }
    ImGui::EndChild();ImGui::EndDisabled();if(playing)ImGui::TextDisabled("Stop Play to edit prefabs.");ImGui::End();
}
bool Shell::draw_prefab_instance(Controller& editor,StableId object){
    std::optional<PrefabInstanceRef> instance;try{instance=prefab_instance_of(*editor.document,object);}catch(const std::exception&){}
    if(!instance)return false;
    auto& document=*editor.document;const auto mount=instance->mount,member=instance->member;const auto prefab=instance->prefab;
    const std::string prefab_name=prefab_label(editor,prefab);
    // The whole instance (its outliner row) or a member of a nested prefab: placement and instance commands only.
    const auto* chosen=editor.selected_mount();const bool whole=(chosen&&*chosen==mount)||instance->nested;
    auto run=[&](auto&& f){try{f();}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}};
    auto select_first=[&](const std::string& mount_key){const auto prefix=document.placement().text()+"/"+mount_key+"/";for(const auto& origin:document.plan().origins)if(origin.local_path.starts_with(prefix)){editor.select_mount(mount_key,origin.object);return;}};
    if(!ImGui::CollapsingHeader(("Prefab instance: "+prefab_name).c_str(),ImGuiTreeNodeFlags_DefaultOpen))return true;
    ImGui::TextDisabled(whole?"Editing instance (whole). Placement and instance commands; select a member row to edit its fields.":"Editing instance. Overrides apply to this placement only.");
    if(instance->nested)ImGui::TextWrapped("This object belongs to a prefab nested inside this instance. Edit it in the Prefabs workspace.");
    if(ImGui::Button("Open Prefab")){prefab_selection=prefab;prefab_preview.in_context=false;select_workspace(Workspace::Prefabs);return true;}ImGui::SameLine();
    if(ImGui::Button("Edit in context")){prefab_selection=prefab;prefab_preview.in_context=true;select_workspace(Workspace::Prefabs);return true;}ImGui::SameLine();
    if(ImGui::Button("Duplicate instance")){run([&]{auto copy=duplicate_instance(document,mount,{2,0,0,0});select_first(copy);});return true;}ImGui::SameLine();
    if(ImGui::Button("Delete instance")){run([&]{delete_instance(document,mount);editor.select_object(document.plan().origins.empty()?StableId{}:document.plan().origins.front().object);});return true;}
    if(ImGui::Button("Unpack (keep identities)")){run([&]{auto keys=unpack_instance(document,mount);const auto prefix=document.placement().text()+"/"+keys.begin()->second;for(const auto& origin:document.plan().origins)if(origin.local_path==prefix){editor.select_object(origin.object);break;}editor.log("Unpacked the instance into scene objects; they keep their persistent identities. Undo restores the instance.");});return true;}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Replaces the instance with ordinary scene objects that keep its current values, poses and persistent identities.");
    {auto at=instance_placement(document,mount);double values[4]{at.x,at.y,at.z,at.yaw*57.29577951308232};ImGui::SetNextItemWidth(260);
        if(ImGui::InputScalarN("Placement x y z",ImGuiDataType_Double,values,3,nullptr,nullptr,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)|ImGui::InputDouble("Yaw (deg)",&values[3],0,0,"%.1f",ImGuiInputTextFlags_EnterReturnsTrue)){run([&]{set_instance_placement(document,mount,{values[0],values[1],values[2],values[3]/57.29577951308232});});return true;}}
    if(!whole){
    // Component fields: inherited values look normal; overridden ones are marked and can be reverted one by one.
    Json effective,source;try{effective=instance_member(document,mount,member);source=prefab_member(document,prefab,member);}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Instance needs repair: %s",error.what());return true;}
    for(const char* component:world_components){
        const bool has=effective.contains(component)&&!effective[component].is_null(),inherited=source.contains(component)&&!source[component].is_null();
        if(!has){if(inherited){ImGui::TextDisabled("%s removed on this instance",component_label(component));ImGui::SameLine();ImGui::PushID(component);if(ImGui::SmallButton("Restore")){run([&]{revert_overrides(document,mount,{{member,component,""}});});ImGui::PopID();return true;}ImGui::PopID();}continue;}
        ImGui::PushID(component);
        if(ImGui::CollapsingHeader((std::string(component_label(component))+(inherited?"":" (added on this instance)")).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){
            for(auto& [field,value]:effective[component].items()){const bool overridden=!inherited||!source[component].contains(field)||source[component][field]!=value;
                ImGui::PushID(field.c_str());if(overridden&&inherited){auto key=override_key(member,component,field);bool picked=prefab_override_selection.contains(key);if(ImGui::Checkbox("##select",&picked)){if(picked)prefab_override_selection.insert(key);else prefab_override_selection.erase(key);}ImGui::SameLine();}
                auto edited=value;if(edit_value((overridden&&inherited?"* "+field:field).c_str(),edited)){const auto chosen_value=edited;const std::string name=field;ImGui::PopID();ImGui::PopID();run([&]{set_instance_field(document,mount,member,component,name,chosen_value);});return true;}
                if(overridden&&inherited){ImGui::SameLine();if(ImGui::SmallButton("Revert")){const std::string name=field;ImGui::PopID();ImGui::PopID();run([&]{revert_overrides(document,mount,{{member,component,name}});});return true;}if(ImGui::IsItemHovered())ImGui::SetTooltip("Prefab value: %s",source[component][field].dump().c_str());}
                ImGui::PopID();}
            if(ImGui::SmallButton(inherited?"Remove on this instance":"Remove added component")){ImGui::PopID();run([&]{remove_instance_component(document,mount,member,component);});return true;}}
        ImGui::PopID();}
    if(ImGui::Button("Add component"))ImGui::OpenPopup("instance-add-component");
    if(ImGui::BeginPopup("instance-add-component")){for(const char* component:world_components)if(!effective.contains(component)||effective[component].is_null())if(ImGui::Selectable(component_label(component))){const std::string chosen_component=component;run([&]{add_instance_component(document,mount,member,chosen_component,world_object_defaults(editor,chosen_component));});}ImGui::EndPopup();}
    }
    // Overrides panel: Revert and Apply Selected act on the checked fields only.
    const auto overrides=instance_overrides(document,mount);
    if(ImGui::TreeNodeEx("Overrides",ImGuiTreeNodeFlags_DefaultOpen,"Overrides (%zu)",overrides.size())){
        for(const auto& entry:overrides)ImGui::BulletText("%s %s%s%s: prefab %s, instance %s",entry.kind==PrefabOverride::AddedComponent?"Added":entry.kind==PrefabOverride::RemovedComponent?"Removed":"Field",component_label(entry.component),entry.field.empty()?"":".",entry.field.c_str(),entry.prefab_value.dump().c_str(),entry.instance_value.dump().c_str());
        std::vector<OverrideKey> keys;for(const auto& text:prefab_override_selection){auto a=text.find('|'),b=text.find('|',a+1);if(whole||text.substr(0,a)==member)keys.push_back({text.substr(0,a),text.substr(a+1,b-a-1),text.substr(b+1)});}
        if(whole&&ImGui::SmallButton("Select all overrides"))for(const auto& entry:overrides)prefab_override_selection.insert(override_key(entry.member,entry.component,entry.field));
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
        if(prefab_swap_target!=AssetId{}){try{const auto text=published(editor,prefab_swap_target);const auto dependencies=dependencies_of(editor,prefab_swap_target,text);
            const auto swap_key=mount+"|"+prefab_swap_target.text();if(prefab_material_swap_key!=swap_key){prefab_material_swap={};prefab_material_swap_key=swap_key;}
            AssemblySources all_sources=dependencies;all_sources[prefab_swap_target]=text;const auto replacement_entities=prefab_entities(prefab_swap_target,all_sources);std::vector<std::pair<std::string,AssetId>> slots;prefab_material_swap.replacement_slots.clear();
            if(replacement_entities.size()==1&&replacement_entities.begin().value().contains("model")){const auto model=AssetId::parse(replacement_entities.begin().value().at("model").get<std::string>());
                for(const auto& asset:editor.asset_inventory)if(asset.id==model){std::ifstream input(editor.assets->source_root()/(asset.path+".daimport"));if(input){const auto metadata=Json::parse(input);for(const auto& [key,id]:metadata.at("subassets").items())if(key.starts_with("material/")){auto slot=AssetId::parse(id.get<std::string>());slots.push_back({key,slot});prefab_material_swap.replacement_slots.insert(slot);}}}}
            ImGui::BeginDisabled(prefab_swap_defaults);
            for(const auto& entry:instance_overrides(document,mount))if(entry.component=="materials"){const auto old_slot=AssetId::parse(entry.field);ImGui::PushID(entry.field.c_str());std::string label="Choose replacement slot";
                if(prefab_material_swap.remap.contains(old_slot))for(const auto& [name,id]:slots)if(id==prefab_material_swap.remap.at(old_slot))label=name;
                if(ImGui::BeginCombo(("Map material slot "+entry.field).c_str(),label.c_str())){if(ImGui::Selectable("Use same stable slot when compatible"))prefab_material_swap.remap.erase(old_slot);for(const auto& [name,id]:slots)if(ImGui::Selectable(name.c_str()))prefab_material_swap.remap[old_slot]=id;ImGui::EndCombo();}ImGui::PopID();}
            ImGui::EndDisabled();
            auto preview=prepare_swap(document,mount,prefab_swap_target,text,prefab_swap_defaults,dependencies,prefab_material_swap);
            ImGui::TextWrapped("Kept: placement, mount and the object's persistent identity (world state such as discovery or collection).");
            for(const auto& carried:preview.carried)ImGui::BulletText("Carried: %s",carried.c_str());for(const auto& dropped:preview.discarded)ImGui::BulletText("Discarded: %s",dropped.c_str());
            if(!preview.compatible)ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",preview.reason.c_str());
            ImGui::BeginDisabled(!preview.compatible);if(ImGui::Button("Swap")){const auto target=prefab_swap_target;const bool defaults=prefab_swap_defaults;ImGui::EndDisabled();ImGui::TreePop();
                run([&]{AssemblySources all=dependencies;all[target]=text;for(const auto& [id,source]:all)prepare_models(editor,Json::parse(source).value("entities",Json::object()));
                    // Validate native resources and destination slots before changing the owned scene source.
                    if(replacement_entities.size()==1){const auto& entity=replacement_entities.begin().value();if(entity.contains("model")){const auto model=AssetId::parse(entity.at("model").get<std::string>());auto assignments=entity.value("materials",Json::object());
                        if(!defaults)for(const auto& entry:instance_overrides(document,mount))if(entry.component=="materials"){auto slot=AssetId::parse(entry.field);if(prefab_material_swap.remap.contains(slot))slot=prefab_material_swap.remap.at(slot);if(entry.instance_value.is_null())assignments.erase(slot.text());else assignments[slot.text()]=entry.instance_value;}
                        if(editor.prepare_material)for(const auto& [slot,material]:assignments.items())editor.prepare_material(model,AssetId::parse(slot),AssetId::parse(material.get<std::string>()));}}
                    commit_swap(document,mount,target,text,defaults,dependencies,prefab_material_swap);select_first(mount);prefab_swap_target={};});return true;}ImGui::EndDisabled();
        }catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}}
        ImGui::TreePop();}
    return true;
}
void Shell::draw_create_prefab(Controller& editor,StableId object){
    if(!editor.assets||!ImGui::TreeNode("Create Prefab from Selection"))return;
    ImGui::TextWrapped("Makes a reusable prefab from this object (and any objects added to the capture) and replaces them with one linked instance at the same place. The objects keep their persistent identities, so saved world state stays with them. Scene objects that target them are rewritten to address the prefab's exports.");
    // Multi-object capture: this object is the pivot; captured objects keep their pose relative to it.
    std::erase_if(prefab_capture,[&](StableId id){return !editor.document->world().valid(editor.document->world().find(id));});
    const bool captured=std::find(prefab_capture.begin(),prefab_capture.end(),object)!=prefab_capture.end();
    if(ImGui::Button(captured?"Captured":"Add to capture")&&!captured)prefab_capture.push_back(object);ImGui::SameLine();ImGui::BeginDisabled(prefab_capture.empty());if(ImGui::Button("Clear capture"))prefab_capture.clear();ImGui::EndDisabled();
    if(!prefab_capture.empty()){ImGui::TextDisabled("Capture (%zu): select others in the outliner and add them; this object is the pivot.",prefab_capture.size());for(auto id:prefab_capture)for(const auto& origin:editor.document->plan().origins)if(origin.object==id)ImGui::BulletText("%s",origin.name.c_str());}
    ImGui::SetNextItemWidth(180);ImGui::InputText("Prefab name",prefab_name,sizeof(prefab_name));
    if(ImGui::Button("Create prefab and link")){try{
        std::vector<StableId> objects{object};for(auto id:prefab_capture)if(id!=object)objects.push_back(id);
        auto id=AssetId::random();auto text=prefab_from_objects(*editor.document,objects,id,prefab_name);
        // Publish the definition first. If the scene edit then fails, the original objects are untouched.
        auto created=editor.authoring.create_prefab_source(*editor.assets,prefab_name,Json::parse(text));editor.authoring.save(*editor.assets,created);editor.refresh_assets();
        auto published_text=editor.prefab_links()(created);if(!published_text)throw std::runtime_error("The new prefab was not published");
        const auto mount=replace_with_instance(*editor.document,objects,created,*published_text);prefab_capture.clear();prefab_consumers_stale=true;
        const auto prefix=editor.document->placement().text()+"/"+mount+"/";for(const auto& origin:editor.document->plan().origins)if(origin.local_path.starts_with(prefix)){editor.select_mount(mount,origin.object);break;}
        editor.log("Created prefab "+std::string(prefab_name)+" and replaced "+std::to_string(objects.size())+" object(s) with a linked instance (same persistent identities).");}
        catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}
    ImGui::TreePop();
}
}
