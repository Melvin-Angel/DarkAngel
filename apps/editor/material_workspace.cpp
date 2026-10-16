#include "editor_ui.hpp"
#include <darkangel/material_assets.hpp>
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
void refresh_consumers(Controller& editor,AssetId material){for(auto model:material_consumers(editor,material))if(editor.prepare_model)editor.prepare_model(model);}
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
        if(ImGui::Button("Save material")){try{editor.save_gameplay();editor.refresh_assets();refresh_consumers(editor,material);author_diagnostic="Saved the material; models that use it were refreshed.";}catch(const std::exception& error){author_diagnostic=error.what();}}
        ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo##material"))editor.authoring.undo(*editor.assets);ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo##material"))editor.authoring.redo(*editor.assets);ImGui::EndDisabled();
        if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());
    }catch(const std::exception& error){ImGui::TextColored({1.f,.55f,.3f,1.f},"%s",error.what());}
}
void Shell::draw_model_materials(Controller& editor,const AssetInfo& model){
    if(!editor.assets)return;auto metadata=read_json(editor.assets->source_root()/(model.path+".daimport"));
    if(!metadata.is_object()||metadata.value("importer","")!="static-gltf-v1"||!metadata.contains("subassets"))return;
    if(!ImGui::CollapsingHeader("Materials",ImGuiTreeNodeFlags_DefaultOpen))return;
    ImGui::TextWrapped("Each slot uses its imported material unless an engine-owned material is assigned. Assignments are kept by the slot's stable key, so they survive reimport of the model.");
    const bool playing=editor.preview.active()!=nullptr;ImGui::BeginDisabled(playing);
    const auto assignments=metadata.value("materials",Json::object());const auto model_path=model.path;const auto model_id=model.id;
    auto run=[&](auto&& f){try{f();author_diagnostic.clear();}catch(const std::exception& error){author_diagnostic=error.what();editor.log(error.what(),ConsoleSeverity::Error);}};
    for(const auto& [slot,unused]:metadata["subassets"].items()){if(!slot.starts_with("material/"))continue;ImGui::PushID(slot.c_str());
        AssetId assigned;std::string label="Imported default";if(assignments.contains(slot)){assigned=AssetId::parse(assignments[slot].at("asset").get<std::string>());label=assignments[slot].at("source").get<std::string>();}
        ImGui::SeparatorText(slot.substr(9).c_str());ImGui::TextDisabled("%s",label.c_str());
        ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Material",assigned==AssetId{}?"Imported default":label.c_str())){
            if(ImGui::Selectable("Imported default",assigned==AssetId{})&&assigned!=AssetId{}){const std::string key=slot;run([&]{editor.assets->assign_model_material(model_path,key,{});editor.refresh_assets();if(editor.prepare_model)editor.prepare_model(model_id);});}
            for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".damaterial"&&asset.generation&&ImGui::Selectable(asset.path.c_str(),asset.id==assigned)&&asset.id!=assigned){const std::string key=slot;const auto chosen=asset.id;const auto path=asset.path;run([&]{editor.assets->assign_model_material(model_path,key,chosen,path);editor.refresh_assets();if(editor.prepare_model)editor.prepare_model(model_id);});}
            ImGui::EndCombo();}
        ImGui::SetNextItemWidth(160);ImGui::InputText("##material-name",material_name,sizeof(material_name));ImGui::SameLine();
        if(ImGui::Button(assigned==AssetId{}?"Create editable material":"Duplicate and assign")){const std::string key=slot;const auto from=assigned;run([&]{
            // A new instance from the slot's current cooked values (or a copy of the assigned one), published, then assigned.
            auto values=from==AssetId{}?Json::parse(editor.assets->material_values(model_path,key)):editor.authoring.open(*editor.assets,from).value;
            auto created=editor.authoring.create_material(*editor.assets,material_name,values);
            if(from!=AssetId{}&&values.contains("texture")&&values["texture"].is_string()){auto& draft=editor.authoring.open(*editor.assets,created).value;draft["texture"]=values["texture"];draft["sources"]=values["sources"];editor.authoring.record_changes(*editor.assets,"Copy material texture");}
            editor.authoring.save(*editor.assets,created);editor.refresh_assets();const auto* asset=asset_by_id(editor,created);if(!asset)throw std::runtime_error("The new material was not published");
            editor.assets->assign_model_material(model_path,key,created,asset->path);editor.refresh_assets();if(editor.prepare_model)editor.prepare_model(model_id);});}
        if(assigned!=AssetId{}&&ImGui::TreeNode("Edit assigned material")){draw_material_form(editor,assigned);ImGui::TreePop();}
        ImGui::PopID();}
    ImGui::EndDisabled();if(playing)ImGui::TextDisabled("Stop Play to change material assignments.");
}
}
