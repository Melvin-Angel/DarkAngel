#include "editor_ui.hpp"
#include "reference_picker.hpp"
#include <cstring>
namespace darkangel::editor_app {
namespace {
bool text(const char* label,nlohmann::json& value,bool multiline=false){
    std::array<char,4097> buffer{};const auto current=value.get<std::string>();std::memcpy(buffer.data(),current.data(),std::min(current.size(),buffer.size()-1));if(multiline)ImGui::TextUnformatted(label);const bool changed=multiline?ImGui::InputTextMultiline("##item-description",buffer.data(),buffer.size(),{-1,120}):ImGui::InputText(label,buffer.data(),129);if(changed)value=buffer.data();return changed;
}
bool pick(const char* label,nlohmann::json& value,nlohmann::json& locators,const char* key,Controller& editor,std::initializer_list<const char*> extensions){
    std::string shown="(none)";for(const auto& asset:editor.asset_inventory)if(value.is_string()&&asset.id.text()==value.get<std::string>())shown=asset.path;bool changed{};
    if(ImGui::BeginCombo(label,shown.c_str())){if(ImGui::Selectable("(none)",value.is_null())){value=nullptr;locators.erase(key);changed=true;}
        for(const auto& asset:editor.asset_inventory){const auto ext=std::filesystem::path(asset.path).extension().string();if(std::none_of(extensions.begin(),extensions.end(),[&](const char* e){return ext==e;}))continue;if(std::string_view(key)!="mask"){std::ifstream file(editor.assets->source_root()/(asset.path+".daimport"));if(!file)continue;auto metadata=nlohmann::json::parse(file);if(metadata.value("importer","")!=(std::string_view(key)=="icon"?"texture-color-v1":"static-gltf-v1"))continue;}
        if(ImGui::Selectable(asset.path.c_str(),value.is_string()&&asset.id.text()==value.get<std::string>())){value=asset.id.text();locators[key]=asset.path;changed=true;}}ImGui::EndCombo();}return changed;
}
}
void Shell::draw_items(Controller& editor,unsigned width,unsigned height,float top){
    ImGui::SetNextWindowPos({0,top});ImGui::SetNextWindowSize({float(width),float(height)-top});ImGui::Begin("Items workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    if(!editor.assets){ImGui::TextWrapped("Open a project source mount to author items.");ImGui::End();return;}
    const bool playing=editor.preview.active()!=nullptr;ImGui::BeginDisabled(playing);
    ImGui::BeginChild("Item list",{320,0},ImGuiChildFlags_Borders);ImGui::TextUnformatted("Items");ImGui::InputText("Search",item_filter,sizeof(item_filter));
    if(ImGui::Button("Create item"))ImGui::OpenPopup("New item");
    if(ImGui::BeginPopup("New item")){ImGui::InputText("Name",item_name,sizeof(item_name));const char* types[]{"Regular","Consumable","Mask"};ImGui::Combo("Type",&item_create_kind,types,3);
        if(item_create_kind==2){nlohmann::json ref=item_create_mask==AssetId{}?nlohmann::json(nullptr):nlohmann::json(item_create_mask.text()),paths=nlohmann::json::object();if(pick("Mask definition",ref,paths,"mask",editor,{".dafacemask"}))item_create_mask=ref.is_null()?AssetId{}:AssetId::parse(ref.get<std::string>());}
        ImGui::BeginDisabled(item_create_kind==2&&item_create_mask==AssetId{});if(ImGui::Button("Create")){try{const char* kinds[]{"regular","consumable","mask"};item_selection=editor.authoring.create_item(*editor.assets,item_name,kinds[item_create_kind],item_create_kind==2?item_create_mask:AssetId{});editor.refresh_assets();author_diagnostic.clear();ImGui::CloseCurrentPopup();}catch(const std::exception& error){author_diagnostic=error.what();}}ImGui::EndDisabled();ImGui::EndPopup();}
    for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem"){
        try{const auto& draft=editor.authoring.open(*editor.assets,asset.id);const auto name=draft.value.at("name").get<std::string>();if(!reference_search(name,item_filter)&&!reference_search(asset.path,item_filter))continue;ImGui::PushID(asset.id.text().c_str());if(ImGui::Selectable((name+(draft.pending||draft.dirty()?" *":"")).c_str(),item_selection==asset.id))item_selection=asset.id;ImGui::PopID();}catch(const std::exception& error){author_diagnostic=error.what();}}
    ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Item details");
    try{if(item_selection!=AssetId{}){auto& draft=editor.authoring.open(*editor.assets,item_selection);auto& value=draft.value;ImGui::Text("%s",value.at("name").get<std::string>().c_str());ImGui::TextDisabled("%s",item_selection.text().c_str());text("Name",value["name"]);text("Description",value["description"],true);ImGui::Text("Type: %s",value.at("classification").get<std::string>().c_str());
        auto& locators=value["sources"];pick("Inventory image",value["icon"],locators,"icon",editor,{".png",".jpg",".jpeg"});pick("World model",value["model"],locators,"model",editor,{".gltf",".glb"});
        if(value.at("classification")=="mask"){pick("Mask definition",value["properties"]["mask"],locators,"mask",editor,{".dafacemask"});if(value["properties"]["mask"].is_string()&&ImGui::Button("Open Mask Editor")){mask_editor=AssetId::parse(value["properties"]["mask"].get<std::string>());mask_editor_raised={};}}
        else {int limit=value.at("stack_limit").get<int>();if(ImGui::InputInt("Stack limit",&limit))value["stack_limit"]=std::clamp(limit,1,1000000);bool unique=value.at("unique").get<bool>();if(ImGui::Checkbox("Unique item",&unique)){value["unique"]=unique;if(unique)value["stack_limit"]=1;}}
        editor.authoring.record_changes(*editor.assets,"Edit item fields");
        if(ImGui::Button("Save items")){editor.authoring.save_all(*editor.assets);editor.refresh_assets();author_diagnostic="Saved items.";}ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_undo());if(ImGui::Button("Undo")){editor.authoring.undo(*editor.assets);editor.refresh_assets();}ImGui::EndDisabled();ImGui::SameLine();ImGui::BeginDisabled(!editor.authoring.can_redo());if(ImGui::Button("Redo")){editor.authoring.redo(*editor.assets);editor.refresh_assets();}ImGui::EndDisabled();
        if(ImGui::Button("Delete item"))ImGui::OpenPopup("Delete selected item?");if(ImGui::BeginPopupModal("Delete selected item?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("Delete this item and discard its draft? Referenced items cannot be deleted. Published source bytes are kept in the project cache.");if(ImGui::Button("Delete")){try{editor.authoring.delete_item(*editor.assets,item_selection);item_selection={};editor.refresh_assets();author_diagnostic="Deleted item.";ImGui::CloseCurrentPopup();}catch(const std::exception& error){author_diagnostic=error.what();}}ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    }else ImGui::TextDisabled("Create or select an item.");}catch(const std::exception& error){author_diagnostic=error.what();}
    if(!author_diagnostic.empty())ImGui::TextWrapped("%s",author_diagnostic.c_str());ImGui::EndChild();ImGui::EndDisabled();if(playing)ImGui::TextDisabled("Stop Play to edit items.");ImGui::End();draw_mask_editor(editor);draw_import(editor);
}
}
