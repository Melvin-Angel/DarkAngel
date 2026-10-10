#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include "reference_picker.hpp"
#include <algorithm>
#include <cctype>

namespace darkangel::editor_app {
namespace {
std::filesystem::path choose_import(){wchar_t file[32768]{};OPENFILENAMEW options{};options.lStructSize=sizeof(options);options.lpstrFile=file;options.nMaxFile=32768;options.lpstrTitle=L"Choose a file to import into the project";options.lpstrFilter=L"All files\0*.*\0glTF models / canonical clips\0*.gltf;*.glb\0Color textures\0*.png;*.jpg;*.jpeg\0";options.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(GetOpenFileNameW(&options))return file;return {};}
constexpr AssetImportType types[]={AssetImportType::Model,AssetImportType::SkinnedMesh,AssetImportType::Animation,AssetImportType::Texture,AssetImportType::Vfx,AssetImportType::Audio,AssetImportType::Material,AssetImportType::UI};
std::string lowercase(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
}
void Shell::open_import(const std::filesystem::path& file,std::optional<AssetImportType> type){import_file=file;import_type=-1;if(type)for(int i=0;i<static_cast<int>(std::size(types));++i)if(types[i]==*type)import_type=i;import_prepared={};import_error.clear();import_status.clear();import_open=true;}
void Shell::choose_asset_import(){auto file=choose_import();if(!file.empty())open_import(file);}
void Shell::validate_import(Controller& editor){if(import_type<0||!editor.assets||editor.preview.active())throw std::runtime_error("Choose an import type and stop Play before validating");import_prepared={};import_error.clear();import_status.clear();import_prepared=editor.assets->prepare_import({import_file,types[import_type],canonical,import_loop});}
void Shell::commit_import(Controller& editor){if(editor.preview.active())throw std::runtime_error("Stop Play before importing");auto result=editor.import_asset(import_prepared);selected_asset=result.root;import_prepared={};import_status="Imported. Find the new asset in the Assets workspace.";}
void Shell::draw_import(Controller& editor){
    if(import_open){ImGui::OpenPopup("Import asset");import_open=false;}
    ImGui::SetNextWindowSize({620,0},ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal("Import asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize))return;
    ImGui::TextWrapped("%s",import_file.filename().string().c_str());ImGui::TextDisabled("Choose what you are importing.");
    if(ImGui::BeginCombo("Item type",import_type<0?"Select a type...":asset_import_label(types[import_type]).data())){for(int i=0;i<static_cast<int>(std::size(types));++i)if(ImGui::Selectable(asset_import_label(types[i]).data(),import_type==i)){import_type=i;import_prepared={};import_error.clear();import_status.clear();}ImGui::EndCombo();}
    bool supported=false;if(import_type>=0){auto type=types[import_type];auto reason=asset_import_support(type);supported=reason.empty();
        ImGui::TextWrapped("Project folder: %s/",asset_import_folder(type).data());
        if(!supported)ImGui::TextWrapped("%s",reason.data());else if(type==AssetImportType::Model)ImGui::TextWrapped("glTF / GLB static models. Local buffers and color textures come with the model. FBX requires offline conversion.");
        else if(type==AssetImportType::Texture)ImGui::TextWrapped("PNG / JPEG color textures, imported as sRGB with mipmaps. Normal maps and other data textures need a later profile.");
        else{ImGui::TextWrapped(type==AssetImportType::SkinnedMesh?"Canonical human GLB, with an embedded atlas. Other rig profiles require later support.":"Normalized canonical GLB clips. Raw FBX animation requires offline conversion.");
            if(ImGui::InputText("Canonical skeleton",canonical,sizeof(canonical))){import_prepared={};import_error.clear();}if(type==AssetImportType::Animation&&ImGui::Checkbox("Loop",&import_loop))import_prepared={};
        }
    }
    ImGui::Separator();ImGui::TextWrapped("The editor copies the file and its supported dependencies into the project. Your original files stay where they are.");
    ImGui::BeginDisabled(!supported||!editor.assets||editor.preview.active()!=nullptr);
    if(ImGui::Button("Validate import")){try{validate_import(editor);}catch(const std::exception& error){import_error=error.what();}}
    ImGui::EndDisabled();
    if(editor.preview.active())ImGui::TextWrapped("Stop Play before importing.");
    if(import_prepared){ImGui::TextWrapped("Destination: %s",import_prepared.destination().c_str());auto files=import_prepared.files();ImGui::Text("Validated %u project files",static_cast<unsigned>(files.size()));if(ImGui::BeginChild("Import files",{580,100},true)){for(const auto& path:files)ImGui::TextWrapped("%s",path.c_str());}ImGui::EndChild();}
    if(!import_error.empty())ImGui::TextColored(attention,"Import needs attention");if(!import_error.empty())ImGui::TextWrapped("%s",import_error.c_str());
    if(!import_status.empty())ImGui::TextWrapped("%s",import_status.c_str());
    ImGui::BeginDisabled(!import_prepared||editor.preview.active()!=nullptr);if(ImGui::Button("Import into project")){try{commit_import(editor);}catch(const std::exception& error){import_error=error.what();import_prepared={};}}ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button(import_status.empty()?"Cancel":"Done")){import_prepared={};ImGui::CloseCurrentPopup();}ImGui::EndPopup();
}
void Shell::draw_assets(Controller& editor,bool expanded){
    if(!editor.assets){ImGui::TextWrapped("No project source mount. Launch with --sources content --asset-cache .cache/editor-assets to browse and import.");return;}
    ImGui::BeginDisabled(editor.preview.active()!=nullptr);if(ImGui::Button("Import...")){auto file=choose_import();if(!file.empty())open_import(file);}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Refresh")){try{editor.refresh_assets();}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}
    auto selected=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==selected_asset;});bool native=false;if(selected!=editor.asset_inventory.end()){auto ext=std::filesystem::path(selected->path).extension().string();native=ext==".dacharacter"||ext==".daplayer"||ext==".dakit"||ext==".dainput"||ext==".dagraph"||ext==".daaction"||ext==".daability"||ext==".daeffect"||ext==".daattributes";}ImGui::SameLine();ImGui::BeginDisabled(!native);if(ImGui::Button("Open authoring")){try{open_native_asset(editor,selected_asset);}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}ImGui::EndDisabled();
    ImGui::SetNextItemWidth(expanded?320.f:-1.f);ImGui::InputTextWithHint("##assetsearch","Search path or UUID...",asset_filter,sizeof(asset_filter));
    const char* kinds[]={"All assets","Models","Characters","Animations","Textures","Players","Abilities","Effects","Action timelines","Animation graphs","Combat kits","Attribute schemas","Input profiles"};ImGui::SetNextItemWidth(expanded?200.f:-1.f);ImGui::Combo("##assetkind",&asset_kind,kinds,static_cast<int>(std::size(kinds)));
    ImGui::TextDisabled("%u assets and drafts",static_cast<unsigned>(editor.asset_inventory.size()));
    if(ImGui::BeginTable("Asset inventory",expanded?3:1,ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersInnerH,{0,expanded?ImGui::GetContentRegionAvail().y:180.f})){
        ImGui::TableSetupColumn("Asset");if(expanded){ImGui::TableSetupColumn("Project path");ImGui::TableSetupColumn("Cook generation",ImGuiTableColumnFlags_WidthFixed,130);}ImGui::TableHeadersRow();
        for(const auto& asset:editor.asset_inventory){auto path=lowercase(asset.path);if(asset_filter[0]&&!reference_search(asset.path,asset_filter)&&!reference_search(asset.id.text(),asset_filter))continue;
            if(asset_kind==1&&!(path.starts_with("models/")||path.find("/static/")!=path.npos))continue;
            if(asset_kind==2&&!(path.starts_with("characters/")||path.find("/character/")!=path.npos))continue;
            if(asset_kind==3&&!(path.starts_with("animation/")||path.find("/clips/")!=path.npos))continue;
            if(asset_kind==4&&!(path.starts_with("textures/")))continue;
            if(asset_kind>=5){constexpr const char* extensions[]={".daplayer",".daability",".daeffect",".daaction",".dagraph",".dakit",".daattributes",".dainput"};if(asset_kind>=5+static_cast<int>(std::size(extensions))||std::filesystem::path(path).extension()!=extensions[asset_kind-5])continue;}
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::PushID(asset.id.text().c_str());auto name=std::filesystem::path(asset.path).filename().string();if(ImGui::Selectable(name.c_str(),selected_asset==asset.id,ImGuiSelectableFlags_SpanAllColumns))selected_asset=asset.id;
            if(ImGui::IsItemHovered()){ImGui::BeginTooltip();ImGui::TextWrapped("%s",asset.path.c_str());ImGui::TextUnformatted(asset.id.text().c_str());ImGui::EndTooltip();}if(expanded){ImGui::TableNextColumn();ImGui::TextUnformatted(asset.path.c_str());ImGui::TableNextColumn();if(asset.generation)ImGui::Text("%llu",static_cast<unsigned long long>(asset.generation));else{auto draft=editor.authoring.drafts.find(asset.id);ImGui::TextDisabled(draft!=editor.authoring.drafts.end()&&draft->second.pending?"Pending creation":"Not cooked here");}}ImGui::PopID();
        }ImGui::EndTable();
    }
}
}
