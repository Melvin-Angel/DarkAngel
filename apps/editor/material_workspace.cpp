#include "editor_ui.hpp"
#include <darkangel/material_assets.hpp>
#include <darkangel/hash.hpp>
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
namespace darkangel::editor_app {
namespace {
using Json=nlohmann::json;
Json read_json(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);if(!input)return Json();return Json::parse(input,nullptr,false);}
const AssetInfo* asset_by_id(const Controller& editor,AssetId id){for(const auto& asset:editor.asset_inventory)if(asset.id==id)return &asset;return nullptr;}
// Models whose import metadata assigns this material: their viewport resources are refreshed after the material is saved.
std::vector<AssetId> material_consumers(const Controller& editor,AssetId material){
    std::vector<AssetId> result;if(!editor.assets)return result;
    for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".gltf"&&extension!=".glb")continue;
        auto metadata=read_json(editor.assets->source_root()/(asset.path+".daimport"));if(!metadata.is_object()||!metadata.contains("materials"))continue;
        for(const auto& [slot,entry]:metadata["materials"].items())if(entry.value("asset","")==material.text()){result.push_back(asset.id);break;}}
    return result;
}
void refresh_consumers(Controller& editor,AssetId material){for(auto model:material_consumers(editor,material)){if(editor.refresh_model)editor.refresh_model(model);else if(editor.prepare_model)editor.prepare_model(model);}}
}
void Shell::draw_material_form(Controller& editor,AssetId material){
    if(!editor.assets)return;
    try{auto& draft=editor.authoring.open(*editor.assets,material);auto& value=draft.value;if(value.value("kind","")!="material")return;
        ImGui::SeparatorText("Material");ImGui::Text("%s",value.value("name","").c_str());ImGui::TextDisabled("%s  |  %s",draft.asset.path.c_str(),draft.pending?"New draft: Save publishes it":draft.dirty()?"Unsaved material changes":"Saved");
        const auto consumers=material_consumers(editor,material);ImGui::TextWrapped(consumers.empty()?"No model slot uses this material yet.":"Shared material: editing it changes every model slot listed below. Duplicate it for a variation.");
        for(auto model:consumers)if(const auto* asset=asset_by_id(editor,model))ImGui::BulletText("%s",asset->path.c_str());
        float color[4];for(int i=0;i<4;++i)color[i]=value["color"][i].get<float>();if(ImGui::ColorEdit4("Base colour",color))for(int i=0;i<4;++i)value["color"][i]=double(color[i]);
        float roughness=value["roughness"].get<float>(),metallic=value["metallic"].get<float>();if(ImGui::SliderFloat("Roughness",&roughness,0,1))value["roughness"]=double(roughness);if(ImGui::SliderFloat("Metallic",&metallic,0,1))value["metallic"]=double(metallic);
        bool sides=value["double_sided"].get<bool>();if(ImGui::Checkbox("Double-sided",&sides))value["double_sided"]=sides;
        // Base-colour texture: any imported colour texture of the project (sRGB colour data only in this family).
        std::string current="No texture";if(value["texture"].is_string())if(const auto* asset=asset_by_id(editor,AssetId::parse(value["texture"].get<std::string>())))current=asset->path;
        ImGui::SetNextItemWidth(300);if(ImGui::BeginCombo("Base-colour texture",current.c_str())){
            if(ImGui::Selectable("No texture",value["texture"].is_null())){value["texture"]=nullptr;value.erase("sources");}
            for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension!=".png"&&extension!=".jpg"&&extension!=".jpeg")continue;
                auto metadata=read_json(editor.assets->source_root()/(asset.path+".daimport"));if(!metadata.is_object()||metadata.value("importer","")!="texture-color-v1")continue;
                if(ImGui::Selectable(asset.path.c_str(),current==asset.path)){value["texture"]=asset.id.text();value["sources"]={{"texture",asset.path}};}}
            ImGui::EndCombo();}
        editor.authoring.record_changes(*editor.assets,"Edit material");
        try{decode_material_instance(value.dump());}catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"Needs attention: %s. Save will reject it.",error.what());}
        if(ImGui::Button("Save material")){try{editor.save_gameplay();editor.refresh_assets();refresh_consumers(editor,material);prefab_preview.key.clear();author_diagnostic="Saved the material; models that use it were refreshed.";}catch(const std::exception& error){author_diagnostic=error.what();}}
        ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo##material"))editor.authoring.undo(*editor.assets);ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo##material"))editor.authoring.redo(*editor.assets);ImGui::EndDisabled();
        if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());
    }catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}
}
void Shell::draw_material_assignments(Controller& editor,const Json& entity,const std::function<void(AssetId,AssetId)>& assign,const std::function<void(AssetId)>& revert){
    if(!editor.assets||!entity.contains("model")||!entity.at("model").is_string())return;const auto* model=asset_by_id(editor,AssetId::parse(entity.at("model").get<std::string>()));if(!model)return;
    auto metadata=read_json(editor.assets->source_root()/(model->path+".daimport"));if(!metadata.is_object()||metadata.value("importer","")!="static-gltf-v1")return;
    ImGui::SeparatorText("Renderable materials");ImGui::TextWrapped("These references affect this definition or placement. Edit a shared material to change all its users.");
    ImGui::BeginDisabled(editor.preview.active()!=nullptr);
    const auto assignments=entity.value("materials",Json::object());
    for(const auto& [key,id]:metadata.at("subassets").items()){if(!key.starts_with("material/"))continue;const auto slot=AssetId::parse(id.get<std::string>());AssetId current;if(assignments.contains(slot.text()))current=AssetId::parse(assignments.at(slot.text()).get<std::string>());
        std::string label="Model default";if(current!=AssetId{})label=asset_by_id(editor,current)?asset_by_id(editor,current)->path:current.text();ImGui::PushID(slot.text().c_str());
        if(revert){const auto overrides=entity.value("material_overrides",Json::array());const bool local=std::find(overrides.begin(),overrides.end(),Json(slot.text()))!=overrides.end();ImGui::TextDisabled(local?"Local override":"Inherited from prefab");}
        auto run=[&](auto&& command){try{command();}catch(const std::exception& error){author_diagnostic=error.what();editor.log(error.what(),ConsoleSeverity::Error);}};
        if(ImGui::BeginCombo(key.c_str(),label.c_str())){if(ImGui::Selectable("Model default",current==AssetId{}))run([&]{assign(slot,{});});for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".damaterial"&&asset.generation&&ImGui::Selectable(asset.path.c_str(),current==asset.id))run([&]{assign(slot,asset.id);});ImGui::EndCombo();}
        if(revert){ImGui::SameLine();if(ImGui::SmallButton("Revert to prefab"))run([&]{revert(slot);});}if(current!=AssetId{}){ImGui::SameLine();if(ImGui::SmallButton("Edit shared"))reveal_asset(current);}ImGui::PopID();}
    for(const auto& [key,material]:assignments.items()){bool exists=false;for(const auto& [name,id]:metadata.at("subassets").items())if(name.starts_with("material/")&&id==key)exists=true;if(exists)continue;
        ImGui::PushID(key.c_str());ImGui::TextColored({1,.55f,.3f,1},"Missing slot %s (assignment preserved)",key.c_str());if(ImGui::SmallButton("Clear missing slot assignment"))try{assign(AssetId::parse(key),{});}catch(const std::exception& error){author_diagnostic=error.what();}ImGui::PopID();}
    ImGui::EndDisabled();
}
void Shell::draw_object_materials(Controller& editor,StableId object){
    if(editor.selected_mount())return;const auto model=editor.document->plan().models.find(object);if(model==editor.document->plan().models.end())return;const auto model_id=model->second;auto instance=prefab_instance_of(*editor.document,object);if(instance&&instance->nested)return;
    Json entity={{"model",model->second.text()},{"materials",Json::object()}};if(const auto found=editor.document->plan().materials.find(object);found!=editor.document->plan().materials.end())for(const auto& [slot,material]:found->second)entity["materials"][slot.text()]=material.text();
    if(instance){entity["material_overrides"]=Json::array();for(const auto& entry:instance_overrides(*editor.document,instance->mount))if(entry.member==instance->member&&entry.component=="materials")entity["material_overrides"].push_back(entry.field);}
    std::function<void(AssetId)> revert;if(instance)revert=[&](AssetId slot){const auto entries=instance_overrides(*editor.document,instance->mount);if(std::none_of(entries.begin(),entries.end(),[&](const auto& entry){return entry.member==instance->member&&entry.component=="materials"&&entry.field==slot.text();}))return;revert_overrides(*editor.document,instance->mount,{{instance->member,"materials",slot.text()}});editor.prepare_render_materials(editor.document->plan());};
    draw_material_assignments(editor,entity,[&](AssetId slot,AssetId material){if(material!=AssetId{}&&editor.prepare_material)editor.prepare_material(model_id,slot,material);if(instance)set_instance_material(*editor.document,instance->mount,instance->member,slot,material);else set_object_material(*editor.document,object,slot,material);},revert);
}
void Shell::draw_model_materials(Controller& editor,const AssetInfo& model){
    if(!editor.assets)return;auto metadata=read_json(editor.assets->source_root()/(model.path+".daimport"));
    if(!metadata.is_object()||metadata.value("importer","")!="static-gltf-v1"||!metadata.contains("subassets"))return;
    if(!ImGui::CollapsingHeader("Materials",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::TextWrapped("Each slot uses its imported material unless an engine-owned material is assigned. Assignments are kept by the slot's stable key, so they survive reimport of the model.");
    const bool playing=editor.preview.active()!=nullptr;ImGui::BeginDisabled(playing);
    const auto assignments=metadata.value("materials",Json::object());const auto model_path=model.path;const auto model_id=model.id;
    auto run=[&](auto&& f){try{f();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();editor.log(error.what(),ConsoleSeverity::Error);}};
    if(ImGui::Button("Review reimport changes"))run([&]{
        material_reimport_changes=editor.assets->model_reimport_changes(model_path);material_reimport_model=model_id;material_reimport_remap.clear();
        std::ifstream input(editor.assets->source_root()/(model_path+".daimport"),std::ios::binary);material_reimport_hash=sha256(std::string(std::istreambuf_iterator<char>(input),{}));});
    if(material_reimport_model==model_id){
        ImGui::TextWrapped("Matching stable keys keep their identities. Choose a destination or explicit retirement for each removed key. Apply adopts other new keys with fresh identities and publishes only if the model validates.");
        for(const auto& key:material_reimport_changes.removed){ImGui::PushID(key.c_str());const auto found=material_reimport_remap.find(key);const auto label=found==material_reimport_remap.end()?"Choose repair":found->second.empty()?"Retire (discard assignment)":found->second.c_str();
            if(ImGui::BeginCombo(key.c_str(),label)){if(ImGui::Selectable("Retire (discard assignment)"))material_reimport_remap[key]="";
                for(const auto& target:material_reimport_changes.added)if(key.substr(0,key.find('/'))==target.substr(0,target.find('/'))&&ImGui::Selectable(target.c_str()))material_reimport_remap[key]=target;ImGui::EndCombo();}ImGui::PopID();}
        for(const auto& key:material_reimport_changes.added)ImGui::BulletText("New: %s",key.c_str());
        if(material_reimport_changes.removed.empty()&&material_reimport_changes.added.empty())ImGui::TextDisabled("No stable-key changes.");
        else{ImGui::BeginDisabled(material_reimport_remap.size()!=material_reimport_changes.removed.size());if(ImGui::Button("Apply explicit reimport repair"))run([&]{auto repair=editor.assets->model_reimport_edit(model_path,material_reimport_hash,material_reimport_remap);editor.authoring.save_all(*editor.assets,{},std::span<const NativeSourceEdit>(&repair,1));material_reimport_model={};editor.refresh_assets();if(editor.refresh_model)editor.refresh_model(model_id);else if(editor.prepare_model)editor.prepare_model(model_id);});ImGui::EndDisabled();}}
    for(const auto& [slot,unused]:metadata["subassets"].items()){if(!slot.starts_with("material/"))continue;ImGui::PushID(slot.c_str());
        AssetId assigned;std::string label="Imported default";if(assignments.contains(slot)){assigned=AssetId::parse(assignments[slot].at("asset").get<std::string>());label=assignments[slot].at("source").get<std::string>();}
        ImGui::SeparatorText(slot.substr(9).c_str());ImGui::TextDisabled("%s",label.c_str());
        ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Material",assigned==AssetId{}?"Imported default":label.c_str())){
            if(ImGui::Selectable("Imported default",assigned==AssetId{})&&assigned!=AssetId{}){const std::string key=slot;run([&]{editor.assets->assign_model_material(model_path,key,{});editor.refresh_assets();if(editor.refresh_model)editor.refresh_model(model_id);else if(editor.prepare_model)editor.prepare_model(model_id);});}
            for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".damaterial"&&asset.generation&&ImGui::Selectable(asset.path.c_str(),asset.id==assigned)&&asset.id!=assigned){const std::string key=slot;const auto chosen=asset.id;const auto path=asset.path;run([&]{editor.assets->assign_model_material(model_path,key,chosen,path);editor.refresh_assets();if(editor.refresh_model)editor.refresh_model(model_id);else if(editor.prepare_model)editor.prepare_model(model_id);});}
            ImGui::EndCombo();}
        ImGui::SetNextItemWidth(160);ImGui::InputText("##material-name",material_name,sizeof(material_name));ImGui::SameLine();
        if(ImGui::Button(assigned==AssetId{}?"Create editable material":"Duplicate and assign")){const std::string key=slot;const auto from=assigned;run([&]{
            // A new instance from the slot's current cooked values (or a copy of the assigned one), published, then assigned.
            auto values=from==AssetId{}?Json::parse(editor.assets->material_values(model_path,key)):editor.authoring.open(*editor.assets,from).value;
            auto created=editor.authoring.create_material(*editor.assets,material_name,values);
            if(from!=AssetId{}&&values.contains("texture")&&values["texture"].is_string()){auto& draft=editor.authoring.open(*editor.assets,created).value;draft["texture"]=values["texture"];draft["sources"]=values["sources"];editor.authoring.record_changes(*editor.assets,"Copy material texture");}
            const auto& draft=editor.authoring.open(*editor.assets,created);auto assignment=editor.assets->model_material_edit(model_path,key,created,draft.asset.path);
            editor.authoring.save_all(*editor.assets,{},std::span<const NativeSourceEdit>(&assignment,1));editor.refresh_assets();if(editor.refresh_model)editor.refresh_model(model_id);else if(editor.prepare_model)editor.prepare_model(model_id);});}
        if(assigned!=AssetId{}&&ImGui::TreeNode("Edit assigned material")){draw_material_form(editor,assigned);ImGui::TreePop();}
        ImGui::PopID();}
    ImGui::EndDisabled();if(playing)ImGui::TextDisabled("Stop Play to change material assignments.");
}
}
