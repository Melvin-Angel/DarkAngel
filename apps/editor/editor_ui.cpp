#define NOMINMAX
#include <Windows.h>
#include <darkangel/interaction.hpp>
#include <darkangel/spline.hpp>
#include <darkangel/scene_travel.hpp>
#include <commdlg.h>
#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include "reference_picker.hpp"
#include "editor_world_objects.hpp"
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <darkangel/encounter.hpp>
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
// One entity of the scene's own source, by its local identity. Edited as one record, so only that object is resolved again.
nlohmann::json own_entity(const Controller& editor,const std::string& local){auto record=editor.document->entity(local);if(!record)throw std::runtime_error("The scene's own source has no such object");return nlohmann::json::parse(*record);}
void commit_entity(Controller& editor,const std::string& local,const nlohmann::json& entity,const char* message){auto edit=editor.document->prepare_entities(editor.document->revision(),{{local,entity.is_null()?std::nullopt:std::optional<std::string>(entity.dump())}});editor.document->commit(edit);editor.log(message);}
// Adds one entity to the scene's own source under the first free local identity and returns the placed object.
StableId add_object(Controller& editor,nlohmann::json entity,const Transform& at,const char* message){
    std::string local;
    for(std::size_t index=1;index<=scene_object_limit;++index){local=StableId{0,index}.text();if(!editor.document->entity(local))break;if(index==scene_object_limit)throw std::runtime_error("Scene object limit");}
    entity["target"]=nullptr;entity["types"]={{"1",{{"version",2},{"fields",{{"1",at.yaw},{"2",at.x},{"3",at.y},{"4",at.z},{"5",0},{"6",0},{"7",1}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};
    commit_entity(editor,local,entity,message);for(const auto& origin:editor.document->plan().origins)if(origin.local_path.ends_with("/"+local))return origin.object;throw std::runtime_error("Placed object did not resolve");
}
// Beside the selected object, or at the origin of an empty selection.
Transform beside_selection(Controller& editor,double height){Transform at;at.y=height;auto selected=editor.document->world().find(editor.selected);if(editor.document->world().valid(selected)){at=editor.document->world().read(selected).transform;at.x+=1.5;at.pitch=at.roll=0;at.scale=1;}return at;}
void set_spawn_npc(Controller& editor,StableId object,AssetId npc){const auto local=object_local(editor,object);auto entity=own_entity(editor,local);entity["spawn"]={{"npc",npc.text()}};commit_entity(editor,local,entity,"Changed the spawn point's NPC.");}
std::string npc_label(const Controller& editor,AssetId npc){for(const auto& asset:editor.asset_inventory)if(asset.id==npc)return std::filesystem::path(asset.path).stem().string();return npc.text();}
// Components of scene objects in the scene's own source: encounters, their volumes, trigger volumes, a spawn point's encounter.
nlohmann::json entity_source(const Controller& editor,StableId object){return own_entity(editor,object_local(editor,object));}
void set_component(Controller& editor,StableId object,const char* key,nlohmann::json value,const char* message){const auto local=object_local(editor,object);auto entity=own_entity(editor,local);if(value.is_null())entity.erase(key);else entity[key]=std::move(value);commit_entity(editor,local,entity,message);}
// Every scene file of the project with its asset identity and its entry points. A scene that does not open is left out.
std::vector<Shell::TravelScene> list_travel_scenes(const Controller& editor){
    std::vector<Shell::TravelScene> found;if(!editor.assets)return found;const auto root=editor.assets->source_root();std::error_code failed;
    for(std::filesystem::recursive_directory_iterator file(root,failed),end;!failed&&file!=end;file.increment(failed)){if(!file->is_regular_file()||file->path().extension()!=".dascene")continue;
        try{const auto document=EditorDocument::open(file->path());Shell::TravelScene scene;scene.id=document->root();scene.path=std::filesystem::relative(file->path(),root).generic_string();for(const auto& [entry,json]:document->plan().entries){std::string label=decode_entry(json).name;for(const auto& origin:document->plan().origins)if(origin.object==entry)label=origin.name+" ("+label+")";scene.entries.emplace_back(entry,label);}found.push_back(std::move(scene));}catch(const std::exception&){}}
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.path<b.path;});return found;
}
std::string object_name(const Controller& editor,StableId object){for(const auto& origin:editor.document->plan().origins)if(origin.object==object)return origin.name;return "(missing)";}
// A text field that commits when Enter is pressed or the field is left after an edit.
bool committed_text(const char* label,std::string& value){char buffer[64]{};strncpy_s(buffer,value.c_str(),_TRUNCATE);const bool entered=ImGui::InputText(label,buffer,sizeof(buffer),ImGuiInputTextFlags_EnterReturnsTrue);if(entered||ImGui::IsItemDeactivatedAfterEdit()){if(value==buffer)return false;value=buffer;return true;}return false;}
// The Scene transition section keeps the list it read until Refresh.
std::vector<Shell::TravelScene> travel_scenes;bool travel_scenes_listed=false;
// Inspector sections for the gameplay components of the selected object. Every edit is a scene edit: one Undo step, saved with the scene.
void draw_components(Controller& editor,StableId object){
    const auto& plan=editor.document->plan();const bool playing=editor.preview.active()!=nullptr;
    auto npc_combo=[&](const char* label,std::string& id){bool changed=false;AssetId current;try{current=AssetId::parse(id);}catch(const std::exception&){}if(ImGui::BeginCombo(label,npc_label(editor,current).c_str())){for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"&&ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str(),asset.id==current)&&asset.id!=current){id=asset.id.text();changed=true;}ImGui::EndCombo();}return changed;};
    auto half_fields=[&](nlohmann::json& half){const char* labels[]={"Half width (m)","Half height (m)","Half depth (m)"};for(unsigned axis=0;axis<3;++axis){double value=half[axis].get<double>();ImGui::SetNextItemWidth(130);if(ImGui::InputDouble(labels[axis],&value,.5,1,"%.2f")&&value>0&&value<=200)half[axis]=value;}};
    if(auto found=plan.encounters.find(object);found!=plan.encounters.end()&&ImGui::CollapsingHeader("Encounter",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("encounter");
        encounter_policy_field(value);
        int trigger=value["trigger"].contains("enter")?0:1;const char* triggers[]={"Starts when the player enters its bounds","Starts on a world event"};ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##trigger",&trigger,triggers,2))value["trigger"]=trigger?nlohmann::json{{"event","encounter.start"}}:nlohmann::json{{"enter",true}};
        if(value["trigger"].contains("event")){auto name=value["trigger"]["event"].get<std::string>();ImGui::SetNextItemWidth(180);if(committed_text("Start event",name))value["trigger"]["event"]=name;}
        int mode=value["mode"]=="timed"?1:0;const char* modes[]={"Waves","Timed"};ImGui::SetNextItemWidth(120);
        if(ImGui::Combo("Mode",&mode,modes,2)){const std::string npc=value.contains("waves")?value["waves"][0]["npc"].get<std::string>():value["timed"]["npc"].get<std::string>();if(mode){value.erase("waves");value["mode"]="timed";value["timed"]={{"npc",npc},{"interval_seconds",4.0},{"max_alive",3u},{"total",6u}};}else{value.erase("timed");value["mode"]="waves";value["waves"]=nlohmann::json::array({nlohmann::json{{"npc",npc},{"count",2u}}});}}
        if(value["mode"]=="waves"){auto& waves=value["waves"];int removed=-1;for(unsigned index=0;index<waves.size();++index){ImGui::PushID(int(index));ImGui::Text("Wave %u",index+1);ImGui::SameLine();ImGui::SetNextItemWidth(150);auto npc=waves[index]["npc"].get<std::string>();if(npc_combo("##npc",npc))waves[index]["npc"]=npc;ImGui::SameLine();int count=waves[index]["count"].get<int>();ImGui::SetNextItemWidth(100);if(ImGui::InputInt("enemies",&count)&&count>=1&&count<=16)waves[index]["count"]=unsigned(count);if(waves.size()>1){ImGui::SameLine();if(ImGui::SmallButton("Remove"))removed=int(index);}ImGui::PopID();}
            if(removed>=0)waves.erase(std::size_t(removed));ImGui::BeginDisabled(waves.size()>=8);if(ImGui::Button("Add wave"))waves.push_back(nlohmann::json(waves.back()));ImGui::EndDisabled();}
        else{auto& timed=value["timed"];auto npc=timed["npc"].get<std::string>();ImGui::SetNextItemWidth(150);if(npc_combo("Enemy",npc))timed["npc"]=npc;double interval=timed["interval_seconds"].get<double>();ImGui::SetNextItemWidth(130);if(ImGui::InputDouble("Seconds between spawns",&interval,.5,1,"%.1f")&&interval>=.1&&interval<=600)timed["interval_seconds"]=interval;int alive=timed["max_alive"].get<int>(),total=timed["total"].get<int>();ImGui::SetNextItemWidth(130);if(ImGui::InputInt("Most alive at once",&alive)&&alive>=1&&alive<=16)timed["max_alive"]=unsigned(alive);ImGui::SetNextItemWidth(130);if(ImGui::InputInt("Enemies in total",&total)&&total>=1&&total<=64)timed["total"]=unsigned(total);}
        int placement=value["placement"]=="random"?1:0;const char* placements[]={"Enemies appear at its spawn points","Enemies appear at random points in its bounds"};ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##placement",&placement,placements,2))value["placement"]=placement?"random":"points";
        ImGui::SeparatorText("World events it emits (empty for none)");
        for(const auto& [key,label]:{std::pair<const char*,const char*>{"started","When it starts"},{"wave_cleared","When a wave is dead"},{"completed","When it is complete"}}){std::string name=value.contains("emits")&&value["emits"].contains(key)?value["emits"][key].get<std::string>():std::string{};ImGui::SetNextItemWidth(180);if(committed_text(label,name)){if(name.empty()){if(value.contains("emits")){value["emits"].erase(key);if(value["emits"].empty())value.erase("emits");}}else value["emits"][key]=name;}}
        {std::string tag=value.value("tag",std::string{});ImGui::SetNextItemWidth(180);if(committed_text("World tag while it runs",tag)){if(tag.empty())value.erase("tag");else value["tag"]=tag;}}
        {double corpse=value.value("corpse_seconds",6.0);ImGui::SetNextItemWidth(130);if(ImGui::InputDouble("Dead enemies disappear after (s, 0 never)",&corpse,1,5,"%.1f")&&corpse>=0&&corpse<=600)value["corpse_seconds"]=corpse;}
        unsigned volumes=0,points=0;for(const auto& [volume,info]:plan.volumes)volumes+=info.encounter==object;for(const auto& [point,owner]:plan.spawn_owners)points+=owner==object;
        ImGui::TextDisabled("%u volume(s), %u spawn point(s), %u enemies in total.",volumes,points,decode_encounter(found->second).enemies());
        if((value["trigger"].contains("enter")||value["placement"]=="random")&&!volumes)ImGui::TextColored({1,.6f,.3f,1},"Needs a volume: place an Encounter volume with this encounter selected.");
        if(value["placement"]=="points"&&!points)ImGui::TextColored({1,.6f,.3f,1},"Needs a spawn point: select a spawn point and choose this encounter.");
        {const auto current=value.value("audio_profile",std::string{});std::string label="(none)";for(const auto& asset:editor.asset_inventory)if(asset.id.text()==current)label=std::filesystem::path(asset.path).stem().string();ImGui::SetNextItemWidth(220);
            if(ImGui::BeginCombo("Audio profile",label.c_str())){if(ImGui::Selectable("(none)",current.empty()))value.erase("audio_profile");for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daaudioprofile"&&ImGui::Selectable(asset.path.c_str(),asset.id.text()==current))value["audio_profile"]=asset.id.text();ImGui::EndCombo();}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Music and ambience layered over the scene's while this encounter runs. Profiles are made in the Audio tab.");}
        ImGui::TextWrapped("Names are lowercase letters, digits, '_' and '.'. A scene's spawn points plus the most its encounters have alive at once can be at most thirty-two actors.");
        ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{decode_encounter(value.dump());set_component(editor,object,"encounter",value,"Edited the encounter.");});return;}
    }
    if(auto found=plan.volumes.find(object);found!=plan.volumes.end()&&ImGui::CollapsingHeader("Encounter volume",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=entity_source(editor,object).at("volume");const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("volume");ImGui::Text("Bounds of: %s",plan.encounters.contains(found->second.encounter)?object_name(editor,found->second.encounter).c_str():"(its encounter was deleted)");half_fields(value["half"]);ImGui::TextDisabled("A box centred on this object, turned by its yaw.");ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{set_component(editor,object,"volume",value,"Resized the encounter volume.");});return;}
    }
    if(auto found=plan.splines.find(object);found!=plan.splines.end()&&ImGui::CollapsingHeader("Spline",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("spline");unsigned points=0;for(const auto& [point,info]:plan.spline_points)if(info.spline==object)++points;ImGui::Text("%u point(s)",points);
        bool closed=value.value("closed",false);if(ImGui::Checkbox("Closed loop",&closed))value["closed"]=closed;bool scattered=value.contains("scatter")&&!value["scatter"].is_null();
        if(ImGui::Checkbox("Scatter a model along it",&scattered)){if(!scattered)value.erase("scatter");else{AssetId model;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daimport"){model=asset.id;break;}if(model==AssetId{})editor.log("Import a model before scattering one.",ConsoleSeverity::Error);else value["scatter"]={{"model",model.text()},{"spacing",2.0},{"jitter",0.0},{"seed",1u},{"align",true}};}}
        if(value.contains("scatter")&&!value["scatter"].is_null()){auto& scatter=value["scatter"];const auto current=AssetId::parse(scatter["model"].get<std::string>());ImGui::SetNextItemWidth(260);
            if(ImGui::BeginCombo("Model",npc_label(editor,current).c_str())){for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daimport"){ImGui::PushID(asset.id.text().c_str());if(ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str(),asset.id==current))scatter["model"]=asset.id.text();ImGui::PopID();}ImGui::EndCombo();}
            double spacing=scatter.value("spacing",2.0),jitter=scatter.value("jitter",0.0);int seed=int(scatter.value("seed",1u));bool align=scatter.value("align",true);
            ImGui::SetNextItemWidth(120);if(ImGui::InputDouble("Spacing (m)",&spacing,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue))scatter["spacing"]=std::clamp(spacing,.25,50.);ImGui::SetNextItemWidth(120);if(ImGui::InputDouble("Sideways jitter (m)",&jitter,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue))scatter["jitter"]=std::clamp(jitter,0.,5.);
            ImGui::SetNextItemWidth(120);if(ImGui::InputInt("Seed",&seed,0,0,ImGuiInputTextFlags_EnterReturnsTrue))scatter["seed"]=unsigned(std::max(0,seed));if(ImGui::Checkbox("Face along the curve",&align))scatter["align"]=align;}
        ImGui::TextWrapped("Move its points with the gizmo. Add a point with the Place panel's Spline point while the spline or one of its points is selected. Scattered models are drawn only: they have no collision.");ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{decode_spline(value.dump());set_component(editor,object,"spline",value,"Edited the spline.");});return;}
    }
    if(auto found=plan.spline_points.find(object);found!=plan.spline_points.end()&&ImGui::CollapsingHeader("Spline point",ImGuiTreeNodeFlags_DefaultOpen))ImGui::TextWrapped("Point %u of its spline. Move it with the gizmo.",found->second.index);
    if(auto found=plan.interactables.find(object);found!=plan.interactables.end()&&ImGui::CollapsingHeader("Interactable",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("interactable");auto prompt=value["prompt"].get<std::string>();ImGui::SetNextItemWidth(220);if(committed_text("Prompt",prompt))value["prompt"]=prompt;auto name=value["event"].get<std::string>();ImGui::SetNextItemWidth(180);if(committed_text("World event",name))value["event"]=name;
        double radius=value.value("radius",2.0);ImGui::SetNextItemWidth(120);if(ImGui::InputDouble("Radius (m)",&radius,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue))value["radius"]=std::clamp(radius,.5,10.);bool once=value.value("once",true);if(ImGui::Checkbox("Only once",&once))value["once"]=once;
        auto tag=value.value("requires_tag",std::string{});ImGui::SetNextItemWidth(180);if(committed_text("Requires world tag",tag)){if(tag.empty())value.erase("requires_tag");else value["requires_tag"]=tag;}ImGui::TextWrapped("The player uses it with the interact input while within the radius. A use emits the world event; with a required tag it is refused until that tag is held. The server decides.");ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{decode_interactable(value.dump());set_component(editor,object,"interactable",value,"Edited the interactable.");});return;}
    }
    if(auto found=plan.triggers.find(object);found!=plan.triggers.end()&&ImGui::CollapsingHeader("Trigger volume",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("trigger");auto name=value["event"].get<std::string>();ImGui::SetNextItemWidth(180);if(committed_text("World event",name))value["event"]=name;bool once=value.value("once",true);if(ImGui::Checkbox("Only the first time",&once))value["once"]=once;half_fields(value["half"]);ImGui::TextWrapped("Emits its world event when the player walks into this box. An encounter that starts on that event starts a tick later.");ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{decode_trigger(value.dump());set_component(editor,object,"trigger",value,"Edited the trigger volume.");});return;}
    }
    if(auto found=plan.entries.find(object);found!=plan.entries.end()&&ImGui::CollapsingHeader("Scene entry point",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("entry");auto name=value["name"].get<std::string>();ImGui::SetNextItemWidth(220);if(committed_text("Name",name))value["name"]=name;ImGui::PopID();ImGui::EndDisabled();
        ImGui::TextWrapped("A party arrives here, the first player on this point and the others around it, facing the way this object faces. A scene transition names it by its identity:");ImGui::TextDisabled("%s",object.text().c_str());ImGui::SameLine();if(ImGui::SmallButton("Copy##entry-id"))ImGui::SetClipboardText(object.text().c_str());
        if(value!=before){action(editor,[&]{decode_entry(value.dump());set_component(editor,object,"entry",value,"Edited the scene entry point.");});return;}
    }
    if(auto found=plan.transitions.find(object);found!=plan.transitions.end()&&ImGui::CollapsingHeader("Scene transition",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=nlohmann::json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("transition");
        // Destinations are the project's scenes and their entry points, read when the section is first shown and on Refresh.
        if(!travel_scenes_listed){travel_scenes=list_travel_scenes(editor);travel_scenes_listed=true;}
        const auto current_scene=value["scene"].get<std::string>();const std::string current_entry=value.value("entry",std::string());std::string scene_label=current_scene,entry_label=current_entry.empty()?"The scene's own start":current_entry;
        for(const auto& known:travel_scenes)if(known.id.text()==current_scene){scene_label=known.path;for(const auto& [entry,label]:known.entries)if(entry.text()==current_entry)entry_label=label;}
        if(current_scene==editor.document->root().text()){scene_label="This scene";for(const auto& [entry,json]:plan.entries)if(entry.text()==current_entry)entry_label=object_name(editor,entry);}
        ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Destination scene",scene_label.c_str())){
            for(const auto& known:travel_scenes)if(ImGui::Selectable(known.path.c_str(),known.id.text()==current_scene)&&known.id.text()!=current_scene){value["scene"]=known.id.text();value.erase("entry");}ImGui::EndCombo();}ImGui::SameLine();if(ImGui::SmallButton("Refresh##travel-scenes"))travel_scenes_listed=false;
        ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Entry point",entry_label.c_str())){if(ImGui::Selectable("The scene's own start",current_entry.empty()))value.erase("entry");
            if(value["scene"].get<std::string>()==editor.document->root().text()){for(const auto& [entry,json]:plan.entries)if(ImGui::Selectable((object_name(editor,entry)+"##"+entry.text()).c_str(),entry.text()==current_entry))value["entry"]=entry.text();}
            else for(const auto& known:travel_scenes)if(known.id.text()==value["scene"].get<std::string>())for(const auto& [entry,label]:known.entries)if(ImGui::Selectable((label+"##"+entry.text()).c_str(),entry.text()==current_entry))value["entry"]=entry.text();
            ImGui::EndCombo();}
        int activation=value["activation"].get<std::string>()=="interact"?1:0;ImGui::SetNextItemWidth(200);if(ImGui::Combo("Activation",&activation,"Walking in\0Used on purpose\0")){value["activation"]=activation?"interact":"enter";if(activation){if(!value.contains("prompt"))value["prompt"]="Travel";}else{value.erase("prompt");value.erase("radius");}}
        if(activation){auto prompt=value.value("prompt",std::string("Travel"));ImGui::SetNextItemWidth(220);if(committed_text("Prompt",prompt))value["prompt"]=prompt;double radius=value.value("radius",2.0);ImGui::SetNextItemWidth(130);if(ImGui::InputDouble("Reach (m)",&radius,.5,1,"%.2f")&&radius>=.5&&radius<=10)value["radius"]=radius;}
        half_fields(value["half"]);ImGui::TextWrapped(activation?"A party member standing within reach is offered the prompt; using it sends the whole party to the destination.":"When any living party member walks into this box, the whole party travels to the destination. A party that arrives inside the box is not sent straight back.");
        ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){action(editor,[&]{decode_transition(value.dump());set_component(editor,object,"transition",value,"Edited the scene transition.");});return;}
    }
    if(draw_world_object_components(editor,object))return;
    if(plan.spawns.contains(object)&&!plan.encounters.empty()){
        auto owner=plan.spawn_owners.find(object);const bool owned=owner!=plan.spawn_owners.end();const std::string label=!owned?"none, spawns at Play start":plan.encounters.contains(owner->second)?object_name(editor,owner->second):"(its encounter was deleted)";ImGui::BeginDisabled(playing);ImGui::SetNextItemWidth(-1);std::optional<nlohmann::json> chosen;
        if(ImGui::BeginCombo("##spawn-encounter",("Encounter: "+label).c_str())){
            if(ImGui::Selectable("None: spawns at Play start",!owned)&&owned)chosen=nlohmann::json(nullptr);
            for(const auto& [encounter,json]:plan.encounters){std::string key;try{key=object_local(editor,encounter);}catch(const std::exception&){continue;}const bool current=owned&&owner->second==encounter;if(ImGui::Selectable((object_name(editor,encounter)+"##"+key).c_str(),current)&&!current)chosen=nlohmann::json(key);}
            ImGui::EndCombo();}
        if(owned)ImGui::TextWrapped("This point belongs to an encounter: it does not spawn at Play start, and the encounter's waves decide who appears here.");
        ImGui::EndDisabled();
        if(chosen)action(editor,[&]{auto spawn=entity_source(editor,object).at("spawn");if(chosen->is_null())spawn.erase("encounter");else spawn["encounter"]=*chosen;set_component(editor,object,"spawn",spawn,"Changed the spawn point's encounter.");});
    }
}
double property(const ObjectData& data,TypeId type,const PropertyMetadata& field){const void* component=type==1?static_cast<const void*>(&data.transform):static_cast<const void*>(&data.health);double value;std::memcpy(&value,static_cast<const char*>(component)+field.offset,sizeof(value));return value;}
}
void Shell::refresh_place_items(Controller& editor){
    // Rebuilt only when the catalogue changes: an imported model's importer is read from its sidecar once.
    std::size_t signature=editor.asset_inventory.size();for(const auto& asset:editor.asset_inventory)signature=signature*1315423911u+std::hash<std::string>{}(asset.path)+asset.generation;
    if(signature==place_signature&&!place_items.empty())return;place_signature=signature;place_items.clear();if(!editor.assets)return;
    place_items.push_back({{},"Encounter","Gameplay",false,1});place_items.push_back({{},"Encounter volume","Gameplay",false,2});place_items.push_back({{},"Trigger volume","Gameplay",false,3});place_items.push_back({{},"Interactable","Gameplay",false,4});place_items.push_back({{},"Spline","Gameplay",false,5});place_items.push_back({{},"Spline point","Gameplay",false,6});place_items.push_back({{},"Scene entry point","Gameplay",false,7});place_items.push_back({{},"Scene transition","Gameplay",false,8});place_items.push_back({{},"Item pickup","Gameplay",false,place_pickup});place_items.push_back({{},"Altar","Gameplay",false,place_altar});place_items.push_back({{},"Door or gate","Gameplay",false,place_door});place_items.push_back({{},"Movable platform","Gameplay",false,place_platform});
    for(const auto& asset:editor.asset_inventory){const std::filesystem::path path(asset.path);const auto extension=path.extension().string();
        if(extension==".danpc"){place_items.push_back({asset.id,path.stem().string(),"NPCs",true});continue;}
        if(extension!=".gltf"&&extension!=".glb")continue;
        std::ifstream sidecar(editor.assets->source_root()/(asset.path+".daimport"));if(!sidecar)continue;
        try{if(nlohmann::json::parse(sidecar).value("importer",std::string())!="static-gltf-v1")continue;}catch(const std::exception&){continue;}
        auto folder=path.parent_path().filename().string();std::replace(folder.begin(),folder.end(),'_',' ');place_items.push_back({asset.id,path.stem().string(),folder.empty()?"Environment":"Environment: "+folder,false});}
    std::sort(place_items.begin(),place_items.end(),[](const PlaceItem& a,const PlaceItem& b){return a.npc!=b.npc?a.npc:a.category!=b.category?a.category<b.category:a.name<b.name;});
}
double Shell::surface_height(double x,double z)const{if(place_surface)if(auto hit=place_surface({x,500,z},{x,-500,z}))return (*hit)[1];return place_height;}
StableId Shell::place(Controller& editor,const PlaceItem& item,double x,double z,double yaw,std::optional<double> y){
    if(editor.preview.active())throw std::runtime_error("Stop Play before placing objects");
    Transform at;at.x=x;at.y=y?*y:surface_height(x,z);at.z=z;at.yaw=yaw;StableId placed;
    // Gameplay volumes are built in, not assets.
    if(item.gameplay==1){AssetId npc;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"){npc=asset.id;break;}if(npc==AssetId{})throw std::runtime_error("Create an NPC definition before placing an encounter");
        placed=add_object(editor,{{"name","Encounter"},{"encounter",{{"trigger",{{"enter",true}}},{"mode","waves"},{"waves",nlohmann::json::array({nlohmann::json{{"npc",npc.text()},{"count",2u}}})},{"placement","random"}}}},at,"Placed an encounter.");
        // It starts with one volume around it, so it has bounds to enter and to spawn in.
        const auto owner=object_local(editor,placed);add_object(editor,{{"name","Encounter volume"},{"volume",{{"encounter",owner},{"half",nlohmann::json::array({5.0,2.0,5.0})}}}},at,"Placed its first volume.");editor.selected=placed;return placed;}
    if(item.gameplay==2){StableId owner=editor.selected;{const auto& plan=editor.document->plan();if(auto volume=plan.volumes.find(owner);volume!=plan.volumes.end())owner=volume->second.encounter;else if(auto point=plan.spawn_owners.find(owner);point!=plan.spawn_owners.end())owner=point->second;if(!plan.encounters.contains(owner))throw std::runtime_error("Select the encounter this volume belongs to, then place the volume");}
        const auto key=object_local(editor,owner);placed=add_object(editor,{{"name","Encounter volume"},{"volume",{{"encounter",key},{"half",nlohmann::json::array({4.0,2.0,4.0})}}}},at,"Placed an encounter volume.");editor.selected=placed;return placed;}
    // A spline starts with two points; a point joins the spline of the selected spline or point, after its last one.
    if(item.gameplay==5){placed=add_object(editor,{{"name","Spline"},{"spline",{{"closed",false}}}},at,"Placed a spline.");const auto owner=object_local(editor,placed);auto second=at;second.x+=4;add_object(editor,{{"name","Spline point"},{"spline_point",{{"spline",owner},{"index",0u}}}},at,"Placed its first point.");add_object(editor,{{"name","Spline point"},{"spline_point",{{"spline",owner},{"index",1u}}}},second,"Placed its second point.");editor.selected=placed;return placed;}
    if(item.gameplay==6){StableId owner=editor.selected;unsigned next=0;{const auto& plan=editor.document->plan();if(auto point=plan.spline_points.find(owner);point!=plan.spline_points.end())owner=point->second.spline;if(!plan.splines.contains(owner))throw std::runtime_error("Select the spline this point belongs to (or one of its points), then place the point");unsigned count=0;for(const auto& [point,info]:plan.spline_points)if(info.spline==owner){++count;next=std::max(next,info.index+1);}if(count>=spline_point_limit)throw std::runtime_error("A spline has at most 32 points");}
        const auto key=object_local(editor,owner);placed=add_object(editor,{{"name","Spline point"},{"spline_point",{{"spline",key},{"index",next}}}},at,"Placed a spline point.");editor.selected=placed;return placed;}
    // An entry point is where a party arrives; a transition starts out leading to this scene itself until a destination is picked.
    if(item.gameplay==7){placed=add_object(editor,{{"name","Entry point"},{"entry",{{"name","Entry"}}}},at,"Placed a scene entry point.");editor.selected=placed;return placed;}
    if(item.gameplay==8){placed=add_object(editor,{{"name","Scene transition"},{"transition",{{"scene",editor.document->root().text()},{"activation","enter"},{"half",nlohmann::json::array({2.0,2.0,1.0})}}}},at,"Placed a scene transition trigger.");editor.selected=placed;return placed;}
    if(item.gameplay>=place_pickup&&item.gameplay<=place_platform)return place_world_object(editor,item.gameplay,at);
    if(item.gameplay==4){placed=add_object(editor,{{"name","Interactable"},{"interactable",{{"prompt","Use"},{"event","object.used"},{"radius",2.0},{"once",true}}}},at,"Placed an interactable.");editor.selected=placed;return placed;}
    if(item.gameplay==3){placed=add_object(editor,{{"name","Trigger volume"},{"trigger",{{"event","trigger.entered"},{"half",nlohmann::json::array({2.0,2.0,2.0})},{"once",true}}}},at,"Placed a trigger volume.");editor.selected=placed;return placed;}
    if(item.npc)placed=add_object(editor,{{"name",item.name+" spawn"},{"spawn",{{"npc",item.id.text()}}}},at,"Placed an NPC spawn point.");
    else{
        // The model is uploaded first (cooked and packaged on its own when it is new to the session), so a placement that cannot
        // be shown never enters the scene. Play packages it with the scene when Play starts.
        if(editor.prepare_model)editor.prepare_model(item.id);
        placed=add_object(editor,{{"name",item.name},{"model",item.id.text()}},at,"Placed a model.");
    }
    editor.selected=placed;return placed;
}
void Shell::remove_object(Controller& editor,StableId object){
    if(editor.preview.active())throw std::runtime_error("Stop Play before deleting objects");
    const auto local=object_local(editor,object);const auto name=own_entity(editor,local).value("name",local);
    commit_entity(editor,local,nullptr,("Deleted "+name+". Undo brings it back.").c_str());if(editor.selected==object)editor.selected=editor.document->plan().origins.empty()?StableId{}:editor.document->plan().origins.front().object;
}
ViewArea Shell::draw(Controller& editor,unsigned width,unsigned height,const char* backend,std::uint64_t generation,double seconds,ImTextureID scene_texture){
    sync_scene_navigation(editor);
    forward=lateral=turn=0;walk=jump=false;controls_acquired=false;
    if(client_only){
        workspace=Workspace::Game;second_view=false;
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({float(width),float(height)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::Begin("Party player",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
        const auto at=ImGui::GetCursorScreenPos();const auto available=ImGui::GetContentRegionAvail();ViewArea area{at.x,at.y,available.x,available.y};
        ImGui::Image(scene_texture,available);controls_focus=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput;
        if(game_overlay)game_overlay(ImGui::GetWindowDrawList(),area);
        if(!client_status.empty())ImGui::GetWindowDrawList()->AddText({at.x+24,at.y+24},IM_COL32(255,255,255,255),client_status.c_str());
        ImGui::End();ImGui::PopStyleVar();return area;
    }
    // Route only to an established editing owner. Text widgets retain their own
    // editing history; Game and browsing-only workspaces never consume authoring keys.
    const auto& shortcuts=ImGui::GetIO();
    const auto* focused_window=ImGui::GetCurrentContext()->NavWindow;
    const auto* mask_window=ImGui::FindWindowByName("Mask Editor");
    const bool mask_owner=mask_editor!=AssetId{}&&focused_window&&mask_window&&focused_window->RootWindow==mask_window->RootWindow;
    const bool native_owner=mask_owner||workspace==Workspace::Character||workspace==Workspace::Player||workspace==Workspace::Npc||workspace==Workspace::Ability||workspace==Workspace::Animation||workspace==Workspace::Items||(workspace==Workspace::Tools&&(tool==ToolView::AuthoringHistory||tool==ToolView::InputBindings));
    const bool scene_owner=!native_owner&&(workspace==Workspace::Scene||(workspace==Workspace::Tools&&tool==ToolView::ScriptSource));
    if((native_owner||scene_owner)&&shortcuts.KeyCtrl&&!shortcuts.KeyAlt&&!shortcuts.WantTextInput&&!ImGui::IsAnyItemActive()&&!dragging&&!composer_dragging&&!editor.preview.active()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)){
        const bool undo=ImGui::IsKeyPressed(ImGuiKey_Z,false)&&!shortcuts.KeyShift;
        const bool redo=ImGui::IsKeyPressed(ImGuiKey_Y,false)||(shortcuts.KeyShift&&ImGui::IsKeyPressed(ImGuiKey_Z,false));
        if(ImGui::IsKeyPressed(ImGuiKey_S,false))action(editor,[&]{if(native_owner){editor.save_gameplay();editor.refresh_assets();}else editor.save(editor.path);});
        if(undo||redo)action(editor,[&]{if(native_owner){if(!editor.assets)return;if(undo&&editor.authoring.can_undo())editor.authoring.undo(*editor.assets);else if(redo&&editor.authoring.can_redo())editor.authoring.redo(*editor.assets);editor.refresh_assets();}else if(undo)editor.document->undo(editor.document->revision());else editor.document->redo(editor.document->revision());});
    }
    const auto menu_height=ImGui::GetFrameHeight();const auto toolbar_height=ImGui::GetFrameHeight()+ImGui::GetStyle().WindowPadding.y*2;const auto workspace_top=menu_height+toolbar_height;
    if(ImGui::BeginMainMenuBar()){ImGui::TextColored({.25f,.75f,.95f,1},"DarkAngel");ImGui::Separator();ImGui::BeginDisabled(dragging);
        {const bool file_open=ImGui::BeginMenu("File");probe("menu-file");if(file_open){ImGui::BeginDisabled(!editor.assets||editor.preview.active()!=nullptr);if(ImGui::MenuItem("Import asset..."))choose_asset_import();ImGui::EndDisabled();if(ImGui::MenuItem("New Scene"))action(editor,[&]{editor.demo();});if(ImGui::MenuItem("Open..."))action(editor,[&]{auto path=dialog(false);if(!path.empty())editor.open(path);});if(ImGui::MenuItem("Save Scene"))action(editor,[&]{editor.save(editor.path);});ImGui::BeginDisabled(editor.preview.active()!=nullptr);if(ImGui::MenuItem("Save All"))action(editor,[&]{
            // Every gameplay draft in every workspace through the coordinated Save, then the scene.
            if(editor.assets&&editor.authoring.dirty()){author_diagnostic.clear();editor.save_gameplay();editor.refresh_assets();}if(!editor.path.empty())editor.save(editor.path);editor.log("Saved all gameplay sources and the scene.");});probe("menu-save-all");ImGui::EndDisabled();if(ImGui::MenuItem("Save As..."))action(editor,[&]{auto path=dialog(true);if(!path.empty())editor.save(path);});if(ImGui::BeginMenu("Reload")){if(ImGui::MenuItem("Scripts"))action(editor,[&]{editor.reload_scripts();});if(ImGui::MenuItem("Models"))reload_model=true;if(ImGui::MenuItem("Shaders"))reload_shader=true;ImGui::EndMenu();}ImGui::EndMenu();}}
        if(ImGui::BeginMenu("Tools")){if(ImGui::MenuItem("Console"))open_tool(ToolView::Console);if(ImGui::MenuItem("Authoring history"))open_tool(ToolView::AuthoringHistory);if(ImGui::MenuItem("Input bindings"))open_tool(ToolView::InputBindings);if(ImGui::MenuItem("Navigation"))open_tool(ToolView::Navigation);if(ImGui::MenuItem("Saves"))open_tool(ToolView::Saves);if(ImGui::MenuItem("Script source"))open_tool(ToolView::ScriptSource);if(ImGui::MenuItem("Runtime phases"))open_tool(ToolView::RuntimePhases);ImGui::EndMenu();}
        if(ImGui::BeginMenu("Settings")){if(ImGui::MenuItem("Editor settings")){if(workspace!=Workspace::Settings&&workspace!=Workspace::Tools)previous_workspace=workspace;project_settings=false;select_workspace(Workspace::Settings);}if(ImGui::MenuItem("Project settings")){if(workspace!=Workspace::Settings&&workspace!=Workspace::Tools)previous_workspace=workspace;project_settings=true;select_workspace(Workspace::Settings);}ImGui::EndMenu();}ImGui::EndDisabled();ImGui::TextDisabled("%s | %s",backend,editor.preview.active()?(editor.paused?"Paused":"Playing"):"Stopped");ImGui::EndMainMenuBar();}
    ImGui::SetNextWindowPos({0,menu_height});ImGui::SetNextWindowSize({float(width),toolbar_height});
    ImGui::Begin("Workflow navigation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginDisabled(dragging);draw_workspace_navigation();ImGui::EndDisabled();ImGui::End();
    if(ImGui::GetTime()-preferences_saved_at>5 && ImGui::GetIO().WantSaveIniSettings){save_preferences();ImGui::GetIO().WantSaveIniSettings=false;}
    draw_mask_editor(editor);
    if(workspace!=Workspace::Scene&&docked)ImGui::DockSpace(ImHashStr("DarkAngelSceneDock"),{0,0},ImGuiDockNodeFlags_KeepAliveOnly);
    if(workspace!=Workspace::Game&&game_docked)ImGui::DockSpace(ImHashStr("DarkAngelGameDock"),{0,0},ImGuiDockNodeFlags_KeepAliveOnly);
    if(workspace==Workspace::Assets){controls_focus=false;ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({float(width),float(height)-workspace_top});ImGui::Begin("Assets workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);draw_assets(editor,true);ImGui::End();draw_import(editor);return area;}
    // Audio: the host's panel fills the workspace.
    if(workspace==Workspace::Items){controls_focus=false;draw_items(editor,width,height,workspace_top);return area;}
    if(workspace==Workspace::Audio){controls_focus=false;ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({float(width),float(height)-workspace_top});ImGui::Begin("Audio workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
        if(draw_audio_panel)draw_audio_panel();else ImGui::TextDisabled("Audio needs the project's sources (start the editor with --sources) and a build with the audio library.");ImGui::End();return area;}
    // VFX: the host's effect panel on the left, the effect preview (the scene render target) filling the rest.
    if(workspace==Workspace::Vfx){controls_focus=false;ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({float(width),float(height)-workspace_top});ImGui::Begin("VFX workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("VFX effects",{std::min(360.f*ImGui::GetStyle().FontScaleDpi,float(width)*.38f),0},ImGuiChildFlags_Borders);if(draw_vfx_panel)draw_vfx_panel();else ImGui::TextDisabled("Visual effects need the project's sources (start the editor with --sources).");ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("VFX preview");const auto pos=ImGui::GetCursorScreenPos();auto size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,64.f);size.y=std::max(size.y,64.f);area={pos.x,pos.y,size.x,size.y};ImGui::Image(scene_texture,size);ImGui::EndChild();ImGui::End();return area;}
    composer_frame_seconds=std::isfinite(seconds)?std::clamp(seconds,0.,.1):0.;
    if(composer_dragging&&workspace!=Workspace::Ability&&workspace!=Workspace::Animation)cancel_composer_drag(editor);
    composer_visible=false;composer_texture=scene_texture;
    if(workspace==Workspace::Ability||workspace==Workspace::Animation||workspace==Workspace::Character||workspace==Workspace::Player||workspace==Workspace::Npc){controls_focus=false;draw_authoring(editor,width,height,workspace_top);return area;}
    if(workspace==Workspace::Game){draw_game(editor,width,height,workspace_top,seconds,scene_texture);draw_import(editor);return area;}
    if(workspace==Workspace::Tools||workspace==Workspace::Settings){controls_focus=false;draw_auxiliary(editor,width,height,workspace_top);draw_import(editor);return area;}
    controls_focus=false;

    ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({static_cast<float>(width),static_cast<float>(height)-workspace_top});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::Begin("Workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBackground);auto dock=ImHashStr("DarkAngelSceneDock");ImGui::DockSpace(dock,{0,0},scene_maximized?ImGuiDockNodeFlags_KeepAliveOnly:ImGuiDockNodeFlags_PassthruCentralNode);ImGui::End();ImGui::PopStyleVar();
    if(!docked){ImGui::DockBuilderRemoveNode(dock);ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);ImGui::DockBuilderSetNodeSize(dock,{static_cast<float>(width),static_cast<float>(height)-workspace_top});auto center=dock;auto left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.17f,nullptr,&center);auto right=ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.25f,nullptr,&center);auto bottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.24f,nullptr,&center);auto place_node=bottom;auto content=bottom;ImGui::DockBuilderDockWindow("Place",place_node);ImGui::DockBuilderDockWindow("World Outliner",left);ImGui::DockBuilderDockWindow("Inspector",right);ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Console",bottom);ImGui::DockBuilderDockWindow("Content Browser",content);ImGui::DockBuilderFinish(dock);if(auto* node=ImGui::DockBuilderGetNode(bottom))node->SelectedTabId=ImHashStr("#TAB",0,ImHashStr("Place"));docked=true;}
    // Side panels keep their pixel widths when the window shrinks or a wider saved layout is restored, which would leave the
    // viewport a minority of the workspace. Scale the Outliner and Inspector columns back so together they stay within 42%.
    if(auto* root=ImGui::DockBuilderGetNode(dock);root&&!scene_maximized){
        const auto column=[](const char* name)->ImGuiDockNode*{auto* window=ImGui::FindWindowByName(name);auto* node=window?window->DockNode:nullptr;return node&&!node->IsCentralNode()&&node->ParentNode&&node->ParentNode->SplitAxis==ImGuiAxis_X?node:nullptr;};
        auto* left=column("World Outliner");auto* right=column("Inspector");
        if(left&&right&&left!=right){const float limit=root->Size.x*.42f,sides=left->Size.x+right->Size.x;
            if(sides>limit){const float k=limit/sides;ImGui::DockBuilderSetNodeSize(left->ID,{left->Size.x*k,left->Size.y});ImGui::DockBuilderSetNodeSize(right->ID,{right->Size.x*k,right->Size.y});}}
    }
    StableId remove_request;
    const bool can_edit=editor.preview.active()==nullptr&&!dragging;
    if(!scene_maximized){
    ImGui::Begin("World Outliner");if(can_edit&&ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::GetIO().WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Delete,false))remove_request=editor.selected;ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##filter","Search objects...",filter,sizeof(filter));probe("outliner-filter");icon_text(Icon::Object,"Scene placements");
    // Only the rows in view are built: a scene of thousands of placements costs the rows on screen, not one row per object.
    std::vector<const ObjectOrigin*> listed;for(const auto& origin:editor.document->plan().origins)if(!filter[0]||reference_search(origin.name,filter)||reference_search(origin.object.text(),filter))listed.push_back(&origin);
    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(listed.size()));while(clipper.Step())for(int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row){const auto& origin=*listed[static_cast<std::size_t>(row)];ImGui::PushID(origin.object.text().c_str());if(icon_selectable(Icon::Object,(origin.name+"##object").c_str(),editor.selected==origin.object))editor.selected=origin.object;probe("outliner:"+origin.object.text());if(ImGui::BeginPopupContextItem("##object-menu")){editor.selected=origin.object;if(ImGui::MenuItem("Frame selection","F",false,editor.preview.active()==nullptr&&!dragging)){if(auto bounds=scene_selection_bounds(editor))scene_navigation.camera.focus(*bounds,area.width/std::max(1.f,area.height));}if(ImGui::MenuItem("Delete","Del",false,editor.preview.active()==nullptr))remove_request=origin.object;probe("menu-delete");ImGui::EndPopup();}if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",origin.name.c_str(),origin.object.text().c_str());ImGui::PopID();}
    ImGui::Separator();ImGui::BeginDisabled(editor.preview.active()!=nullptr||dragging);if(ImGui::Button("Add NPC spawn point"))ImGui::OpenPopup("Add NPC spawn point");ImGui::EndDisabled();
    if(ImGui::BeginPopup("Add NPC spawn point")){bool any=false;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"){any=true;if(ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str()))action(editor,[&]{const auto at=beside_selection(editor,place_height);place(editor,{asset.id,std::filesystem::path(asset.path).stem().string(),"NPCs",true},at.x,at.z,at.yaw);});}if(!any)ImGui::TextDisabled("No NPC definitions. Create one in the NPC tab.");ImGui::EndPopup();}
    ImGui::End();
    ImGui::Begin("Inspector");auto handle=editor.displayed().find(editor.selected);if(editor.displayed().valid(handle)){auto object=editor.displayed().read(handle);const auto& origins=editor.document->plan().origins;auto named=std::find_if(origins.begin(),origins.end(),[&](const auto& origin){return origin.object==object.id;});
        icon_text(Icon::Object,named==origins.end()?"Scene object":named->name.c_str());
        ImGui::TextDisabled(editor.document->dirty()?"Scene has unsaved changes":"Scene saved");
        if(editor.preview.active())ImGui::TextWrapped("Stop Play to edit scene properties.");
        int destination=scope==EditScope::Definition?1:0;const char* destinations[]={"Scene placement","Assembly definition"};ImGui::TextDisabled("Edit destination");ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##Edit destination",&destination,destinations,2))scope=destination?EditScope::Definition:EditScope::Placement;ImGui::BeginDisabled(editor.preview.active()!=nullptr || dragging);
        for(const auto& type:metadata())if(type.id<=2 && ImGui::CollapsingHeader(std::string(type.name).c_str(),ImGuiTreeNodeFlags_DefaultOpen)){if(ImGui::BeginTable("Properties",2,ImGuiTableFlags_SizingStretchProp)){ImGui::TableSetupColumn("Property",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("maximum").x+8);ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch);for(const auto& field:type.properties)if(field.flags&Editable){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(std::string(field.name).c_str());ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);auto value=property(object,type.id,field);ImGui::PushID(static_cast<int>(type.id*100+field.id));const bool steppers=ImGui::GetContentRegionAvail().x>=ImGui::GetFrameHeight()*2+ImGui::GetFontSize()*6;if(ImGui::InputDouble("##value",&value,steppers?.1:0,steppers?1:0,"%.3f"))action(editor,[&]{PropertyChange change{object.id,type.id,field.id,value};auto prepared=editor.document->prepare(editor.document->revision(),{&change,1},scope);editor.document->commit(prepared);editor.log(prepared.summary);});ImGui::PopID();}ImGui::EndTable();}}
        if(auto spawn=editor.document->plan().spawns.find(object.id);spawn!=editor.document->plan().spawns.end()&&ImGui::CollapsingHeader("NPC spawn point",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextWrapped("Play spawns this NPC here with its own kit, attributes and behaviour. The object's Health is replaced by the NPC's.");ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##spawn-npc",npc_label(editor,spawn->second).c_str())){for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".danpc"&&ImGui::Selectable(std::filesystem::path(asset.path).stem().string().c_str(),asset.id==spawn->second)&&asset.id!=spawn->second){const auto point=object.id;const auto npc=asset.id;action(editor,[&]{set_spawn_npc(editor,point,npc);});break;}ImGui::EndCombo();}
            ImGui::TextDisabled("Respawn: never");}
        draw_components(editor,object.id);
        if(ImGui::Button("Delete object"))remove_request=object.id;ImGui::SameLine();
        if(ImGui::Button("Undo"))action(editor,[&]{editor.document->undo(editor.document->revision());});ImGui::SameLine();if(ImGui::Button("Redo"))action(editor,[&]{editor.document->redo(editor.document->revision());});ImGui::EndDisabled();
        if(ImGui::CollapsingHeader("Advanced / Debug")){
        ImGui::TextWrapped("Persistent object ID: %s",object.id.text().c_str());ImGui::Text("World %llu | epoch %llu",handle.domain,handle.epoch);ImGui::Text("Handle %llu | valid",handle.runtime);for(const auto& type:metadata())if(type.id<=2)ImGui::Text("%s: TypeId %u | schema %u",std::string(type.name).c_str(),type.id,type.version);
        if(ImGui::CollapsingHeader("Script Bindings"))for(const auto& binding:object.scripts.records){ImGui::TextWrapped("Attachment %s",binding.id.text().c_str());ImGui::TextWrapped("Asset %s",asset_id(binding.asset).text().c_str());ImGui::TextUnformatted(binding.config_json.c_str());if(editor.preview.active())for(const auto& info:editor.preview.active()->scripts->bindings())if(info.id==binding.id){ImGui::Text("%s | generation %llu",info.enabled?"Enabled":"Disabled",info.generation);ImGui::Text("Calls %llu | last %llu us",info.metrics.calls,info.metrics.last_microseconds);}}
        if(ImGui::CollapsingHeader("Provenance"))for(const auto& origin:editor.document->plan().origins)if(origin.object==object.id){ImGui::TextWrapped("Definition %s",origin.definition.text().c_str());ImGui::TextWrapped("Stable local path %s",origin.local_path.c_str());ImGui::TextWrapped("%s",origin.layer.c_str());}
        }
    }else ImGui::TextDisabled("Select a scene object.");ImGui::End();
    ImGui::Begin("Content Browser");draw_assets(editor);ImGui::Separator();icon_text(Icon::Folder,"Scene models");std::set<AssetId> models;for(const auto& [object,asset]:editor.document->plan().models)models.insert(asset);for(auto asset:models)ImGui::TextWrapped("%s",asset.text().c_str());ImGui::Text("Resource generation %llu",generation);std::set<StableId> scripts;for(const auto& planned:editor.document->plan().bindings)scripts.insert(planned.binding.asset);for(auto id:scripts){ImGui::PushID(id.text().c_str());if(ImGui::Button("Edit script source"))action(editor,[&]{script_asset=asset_id(id);auto data=nlohmann::json::parse(editor.document->source(script_asset));strncpy_s(script,data.at("source").get<std::string>().c_str(),_TRUNCATE);show_script=true;open_tool(ToolView::ScriptSource);});ImGui::TextWrapped("%s",id.text().c_str());ImGui::PopID();}ImGui::TextDisabled("Source edits use native transactions.");ImGui::End();
    ImGui::Begin("Console");draw_console(editor);ImGui::End();
    }
    if(scene_maximized){ImGui::SetNextWindowDockID(0);ImGui::SetNextWindowPos({0,workspace_top});ImGui::SetNextWindowSize({float(width),float(height)-workspace_top});}
    ImGui::Begin("Viewport",nullptr,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|(scene_maximized?ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking:0));
    if(ImGui::Button(scene_maximized?"Restore viewport":"Maximize viewport"))toggle_viewport_maximize();probe("viewport-maximize");toolbar_next(110);const char* modes[]={"Move","Rotate","Scale"};ImGui::SetNextItemWidth(100);ImGui::Combo("##gizmo",&operation,modes,3);if(ImGui::GetWindowWidth()>560*ImGui::GetStyle().FontScaleDpi){toolbar_next(170);ImGui::TextDisabled("%s | metres | RH Y-up",backend);}else if(ImGui::IsItemHovered())ImGui::SetTooltip("Transform tool | metres | rotations in radians | RH Y-up | %s",backend);draw_scene_navigation_toolbar(editor);auto pos=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,1.f);size.y=std::max(size.y,1.f);area={pos.x,pos.y,size.x,size.y};viewport_draw=ImGui::GetWindowDrawList();ImGui::Image(scene_texture,size);probes["viewport-drop"]={area.x+area.width*.5f,area.y+area.height*.62f};probes["viewport-origin"]={area.x,area.y};
    update_scene_navigation(editor,seconds);
    if(debug_overlay)debug_overlay(viewport_draw,area);
    // A click in the viewport selects the object under the cursor; a click on the gizmo or on nothing keeps the selection.
    if(pick_object&&scene_camera&&!editor.preview.active()&&!scene_navigation.active()&&!ImGui::GetIO().KeyAlt&&!ImGui::GetIO().WantTextInput&&ImGui::IsItemHovered()&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)&&!ImGuizmo::IsOver()&&!ImGuizmo::IsUsing()){using namespace DirectX;const auto mouse=ImGui::GetMousePos();const float nx=(mouse.x-area.x)/area.width*2-1,ny=1-(mouse.y-area.y)/area.height*2;const auto inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&scene_view)*XMLoadFloat4x4(&scene_projection));
        XMFLOAT3 from,to;XMStoreFloat3(&from,XMVector3TransformCoord(XMVectorSet(nx,ny,0,1),inverse));XMStoreFloat3(&to,XMVector3TransformCoord(XMVectorSet(nx,ny,1,1),inverse));if(const auto hit=pick_object({from.x,from.y,from.z},{to.x,to.y,to.z}))editor.selected=hit;}
    if(ImGui::BeginDragDropTarget()){
        if(const auto* payload=ImGui::AcceptDragDropPayload("DA_PLACE");payload&&payload->DataSize==sizeof(int)){int index{};std::memcpy(&index,payload->Data,sizeof(index));
            if(index>=0&&index<static_cast<int>(place_items.size()))action(editor,[&]{
                // The drop position becomes a ray through the scene camera; the object lands where it first meets the scene collision,
                // or the ground plane where the ray meets none.
                using namespace DirectX;double x=0,z=0;std::optional<double> y;if(scene_camera){const auto mouse=ImGui::GetMousePos();const float nx=(mouse.x-area.x)/area.width*2-1,ny=1-(mouse.y-area.y)/area.height*2;const auto inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&scene_view)*XMLoadFloat4x4(&scene_projection));
                    XMFLOAT3 from,to;XMStoreFloat3(&from,XMVector3TransformCoord(XMVectorSet(nx,ny,0,1),inverse));XMStoreFloat3(&to,XMVector3TransformCoord(XMVectorSet(nx,ny,1,1),inverse));const double fall=double(to.y)-from.y;
                    if(auto hit=place_surface?place_surface({from.x,from.y,from.z},{to.x,to.y,to.z}):std::nullopt){x=(*hit)[0];y=(*hit)[1];z=(*hit)[2];}
                    else if(std::abs(fall)>1e-6){const double along=(place_height-from.y)/fall;if(along>0&&along<=1){x=from.x+(to.x-from.x)*along;z=from.z+(to.z-from.z)*along;}}x=std::clamp(x,-500.,500.);z=std::clamp(z,-500.,500.);}
                place(editor,place_items[static_cast<std::size_t>(index)],x,z,0,y);});}
        ImGui::EndDragDropTarget();}
    if(can_edit&&ImGui::IsWindowFocused()&&!ImGui::GetIO().WantTextInput&&ImGui::IsKeyPressed(ImGuiKey_Delete,false))remove_request=editor.selected;
    ImGui::End();
    if(!scene_maximized){
    ImGui::Begin("Place");refresh_place_items(editor);
    if(place_items.empty())ImGui::TextWrapped(editor.assets?"Nothing placeable yet. NPC definitions and imported static models appear here in categories.":"Open a project source mount to place NPCs and models.");
    else{
        // Categories come from the catalogue: one for NPC definitions, one per folder of static models.
        std::vector<std::pair<std::string,unsigned>> categories;for(const auto& item:place_items){if(categories.empty()||categories.back().first!=item.category)categories.push_back({item.category,0});++categories.back().second;}
        if(!place_category.empty()&&std::none_of(categories.begin(),categories.end(),[&](const auto& entry){return entry.first==place_category;}))place_category.clear();
        ImGui::SetNextItemWidth(std::min(200.f*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.36f));ImGui::InputTextWithHint("##place-filter","Search placeable assets...",place_filter,sizeof(place_filter));
        const bool compact_place=ImGui::GetWindowWidth()<400*ImGui::GetStyle().FontScaleDpi;
        if(!compact_place){toolbar_next(150);ImGui::SetNextItemWidth(std::min(200.f*ImGui::GetStyle().FontScaleDpi,ImGui::GetContentRegionAvail().x*.56f));
        if(ImGui::BeginCombo("##place-category",place_category.empty()?"All categories":place_category.c_str())){if(ImGui::Selectable("All categories",place_category.empty()))place_category.clear();for(const auto& [name,count]:categories){auto label=name+" ("+std::to_string(count)+")";if(ImGui::Selectable(label.c_str(),place_category==name))place_category=name;}ImGui::EndCombo();}}
        toolbar_next(70);if(ImGui::SmallButton("Options"))ImGui::OpenPopup("Placement options");
        if(ImGui::BeginPopup("Placement options")){if(compact_place&&ImGui::BeginCombo("Category",place_category.empty()?"All categories":place_category.c_str())){if(ImGui::Selectable("All categories",place_category.empty()))place_category.clear();for(const auto& [name,count]:categories)if(ImGui::Selectable(name.c_str(),place_category==name))place_category=name;ImGui::EndCombo();}ImGui::SetNextItemWidth(110*ImGui::GetStyle().FontScaleDpi);ImGui::InputDouble("Fallback ground height (m)",&place_height,0,0,"%.2f");ImGui::TextWrapped("Drag into the viewport or double-click to place beside selection. Collision surfaces take precedence over the fallback ground. Delete removes the selected object.");if(!can_edit)ImGui::TextDisabled("Stop Play to place or delete.");ImGui::EndPopup();}
        ImGui::Separator();ImGui::BeginChild("Place items",{0,0});const float tile=170;const int columns=std::max(1,int(ImGui::GetContentRegionAvail().x/(tile+8)));int column=0;
        for(std::size_t index=0;index<place_items.size();++index){const auto& item=place_items[index];if(!place_category.empty()&&item.category!=place_category)continue;
            if(place_filter[0]){auto lower=[](std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;};if(lower(item.name).find(lower(place_filter))==std::string::npos)continue;}
            if(column++%columns)ImGui::SameLine();ImGui::PushID(static_cast<int>(index));ImGui::BeginDisabled(!can_edit);
            ImGui::Button(item.name.c_str(),{tile,0});probe("place:"+item.name);
            if(ImGui::IsItemHovered()){ImGui::SetTooltip("%s\n%s\nDrag into the viewport, or double-click to place beside the selection.",item.name.c_str(),item.category.c_str());if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){const auto at=beside_selection(editor,place_height);const auto chosen=item;action(editor,[&]{place(editor,chosen,at.x,at.z,at.yaw);});}}
            if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)){const int payload=static_cast<int>(index);ImGui::SetDragDropPayload("DA_PLACE",&payload,sizeof(payload));ImGui::Text("Place %s",item.name.c_str());ImGui::EndDragDropSource();}
            ImGui::EndDisabled();ImGui::PopID();}
        ImGui::EndChild();
    }
    ImGui::End();
    }
    if(remove_request)action(editor,[&]{remove_object(editor,remove_request);});
    draw_import(editor);return area;
}
void Shell::gizmo(Controller& editor,const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection){if(workspace!=Workspace::Scene||editor.preview.active())return;if(!dragging&&(scene_navigation.active()||ImGui::GetIO().KeyAlt||ImGui::IsMouseDown(1)||ImGui::IsMouseDown(2)))return;auto handle=editor.document->world().find(editor.selected);if(!editor.document->world().valid(handle))return;auto pose=draft?*draft:editor.document->world().read(handle).transform;using namespace DirectX;XMFLOAT4X4 matrix;XMStoreFloat4x4(&matrix,XMMatrixScaling(pose.scale,pose.scale,pose.scale)*XMMatrixRotationRollPitchYaw(pose.pitch,pose.yaw,pose.roll)*XMMatrixTranslation(pose.x,pose.y,pose.z));ImGuizmo::SetDrawlist(viewport_draw);ImGuizmo::SetRect(area.x,area.y,area.width,area.height);auto op=operation==1?ImGuizmo::ROTATE:operation==2?ImGuizmo::SCALEU:ImGuizmo::TRANSLATE;ImGuizmo::Manipulate(&view._11,&projection._11,op,ImGuizmo::LOCAL,&matrix._11);
    bool using_gizmo=ImGuizmo::IsUsing();if(using_gizmo && !dragging){gesture_object=editor.selected;gesture_revision=editor.document->revision();dragging=true;cancelled=false;}
    if(using_gizmo){float position[3],rotation[3],scale[3];ImGuizmo::DecomposeMatrixToComponents(&matrix._11,position,rotation,scale);pose.x=position[0];pose.y=position[1];pose.z=position[2];pose.pitch=rotation[0]*XM_PI/180;pose.yaw=rotation[1]*XM_PI/180;pose.roll=rotation[2]*XM_PI/180;pose.scale=scale[0];if(!cancelled)draft=pose;else draft.reset();if(ImGui::IsKeyPressed(ImGuiKey_Escape)){cancelled=true;draft.reset();}}
    if(dragging && !using_gizmo){if(!cancelled && draft)action(editor,[&]{auto value=*draft;PropertyChange changes[]={{gesture_object,1,1,value.yaw},{gesture_object,1,2,value.x},{gesture_object,1,3,value.y},{gesture_object,1,4,value.z},{gesture_object,1,5,value.pitch},{gesture_object,1,6,value.roll},{gesture_object,1,7,value.scale}};auto transaction=editor.document->prepare(gesture_revision,changes,scope);editor.document->commit(transaction);editor.log("Committed one transform gesture.");});draft.reset();dragging=false;}
}
AssetId scene_audio_profile(const Controller& editor){const auto& held=editor.document->plan().audio_profiles;return held.empty()?AssetId{}:held.begin()->second;}
void set_scene_transition(Controller& editor,StableId object,AssetId scene,StableId entry,bool interact,std::string_view prompt,double radius){auto value=entity_source(editor,object).at("transition");value["scene"]=scene.text();if(entry)value["entry"]=entry.text();else value.erase("entry");value["activation"]=interact?"interact":"enter";if(interact){value["prompt"]=std::string(prompt);value["radius"]=radius;}else{value.erase("prompt");value.erase("radius");}decode_transition(value.dump());set_component(editor,object,"transition",value,"Edited the scene transition.");}
StableId scene_add_object(Controller& editor,nlohmann::json entity,const Transform& at,const char* message){return add_object(editor,std::move(entity),at,message);}
void scene_set_component(Controller& editor,StableId object,const char* key,nlohmann::json value,const char* message){set_component(editor,object,key,std::move(value),message);}
std::vector<Shell::TravelScene> scene_travel_list(const Controller& editor){return list_travel_scenes(editor);}
void set_scene_audio_profile(Controller& editor,AssetId profile){
    const auto& held=editor.document->plan().audio_profiles;
    if(held.empty()){if(profile!=AssetId{})add_object(editor,{{"name","Scene audio"},{"audio",{{"profile",profile.text()}}}},Transform{},"Set the scene's audio profile.");return;}
    const auto object=held.begin()->first;if(profile==AssetId{})set_component(editor,object,"audio",nullptr,"Removed the scene's audio profile.");else set_component(editor,object,"audio",{{"profile",profile.text()}},"Changed the scene's audio profile.");
}
}
