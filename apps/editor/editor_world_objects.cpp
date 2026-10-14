#include "editor_world_objects.hpp"
#include <darkangel/world_objects.hpp>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace darkangel::editor_app {
namespace {
using Json=nlohmann::json;
template<class F>void action(Controller& editor,F&& call){try{call();}catch(const std::exception& error){editor.log(error.what(),ConsoleSeverity::Error);}}
bool text_field(const char* label,std::string& value,float width=220){char buffer[96]{};strncpy_s(buffer,value.c_str(),_TRUNCATE);ImGui::SetNextItemWidth(width);const bool entered=ImGui::InputText(label,buffer,sizeof(buffer),ImGuiInputTextFlags_EnterReturnsTrue);if(entered||ImGui::IsItemDeactivatedAfterEdit()){if(value==buffer)return false;value=buffer;return true;}return false;}
bool number_field(const char* label,Json& value,const char* key,double fallback,double low,double high,float width=130){double v=value.value(key,fallback);ImGui::SetNextItemWidth(width);if(ImGui::InputDouble(label,&v,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)){value[key]=std::clamp(v,low,high);return true;}return false;}
bool flag_field(const char* label,Json& value,const char* key,bool fallback){bool v=value.value(key,fallback);if(ImGui::Checkbox(label,&v)){value[key]=v;return true;}return false;}
bool vector_field(const char* label,Json& value,const char* key,std::array<double,3> fallback,double low,double high){
    std::array<double,3> v=fallback;if(value.contains(key))for(unsigned axis=0;axis<3;++axis)v[axis]=value[key][axis].get<double>();
    float f[3]{float(v[0]),float(v[1]),float(v[2])};ImGui::SetNextItemWidth(260);if(ImGui::InputFloat3(label,f,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)){value[key]=Json::array({std::clamp(double(f[0]),low,high),std::clamp(double(f[1]),low,high),std::clamp(double(f[2]),low,high)});return true;}return false;}
std::string item_label(const Controller& editor,const std::string& id){for(const auto& asset:editor.asset_inventory)if(asset.id.text()==id)return std::filesystem::path(asset.path).stem().string();return id.empty()?std::string("None"):id;}
bool item_combo(const Controller& editor,const char* label,Json& value,const char* key,bool optional){
    const std::string current=value.contains(key)&&value[key].is_string()?value[key].get<std::string>():std::string();bool changed=false;ImGui::SetNextItemWidth(220);
    if(ImGui::BeginCombo(label,item_label(editor,current).c_str())){if(optional&&ImGui::Selectable("None",current.empty())&&!current.empty()){value.erase(key);changed=true;}
        for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem"&&ImGui::Selectable((std::filesystem::path(asset.path).stem().string()+"##"+asset.id.text()).c_str(),asset.id.text()==current)&&asset.id.text()!=current){value[key]=asset.id.text();changed=true;}
        ImGui::EndCombo();}
    return changed;}
// One logical property's persistence: where it lives and what takes it back to as authored. Absent: the kind's default.
bool policy_field(Json& component,const char* property,const char* label,LedgerPolicy fallback){
    bool changed=false;ImGui::PushID(property);auto& policies=component["policies"];if(!policies.is_object())policies=Json::object();
    const bool custom=policies.contains(property);const auto policy=custom?decode_policy(policies[property].dump()):fallback;
    ImGui::TextUnformatted(label);ImGui::SameLine();ImGui::TextDisabled(custom?"(custom)":"(default)");
    int scope=policy.scope==LedgerScope::Save?0:1;LedgerPolicy next=policy;ImGui::SetNextItemWidth(170);if(ImGui::Combo("Kept",&scope,"In the save file\0This Play only\0")){next.scope=scope?LedgerScope::Session:LedgerScope::Save;changed=true;}
    const std::pair<LedgerReset,const char*> triggers[]{{LedgerReset::SceneEntry,"Reset on scene entry"},{LedgerReset::Rest,"Reset on rest"},{LedgerReset::PartyWipe,"Reset on party wipe"}};
    for(const auto& [trigger,name]:triggers){bool on=next.on(trigger);if(ImGui::Checkbox(name,&on)){next.resets=std::uint8_t(on?next.resets|std::uint8_t(trigger):next.resets&~std::uint8_t(trigger));changed=true;}}
    if(custom){ImGui::SameLine();if(ImGui::SmallButton("Use default")){policies.erase(property);if(policies.empty())component.erase("policies");ImGui::PopID();return true;}}
    if(changed)policies[property]=Json::parse(encode_policy(next));if(policies.empty())component.erase("policies");ImGui::PopID();return changed;}
}

bool encounter_policy_field(Json& encounter){const bool changed=policy_field(encounter,"completed","Completed",LedgerPolicy{LedgerScope::Save,LedgerReset::Rest|LedgerReset::PartyWipe});ImGui::TextDisabled("Clear every reset for a boss that stays defeated.");return changed;}
bool draw_world_object_components(Controller& editor,StableId object){
    const auto& plan=editor.document->plan();const bool playing=editor.preview.active()!=nullptr;
    if(auto found=plan.pickups.find(object);found!=plan.pickups.end()&&ImGui::CollapsingHeader("Item pickup",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=Json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("pickup");
        item_combo(editor,"Item",value,"item",false);int quantity=value.value("quantity",1);ImGui::SetNextItemWidth(120);if(ImGui::InputInt("Quantity",&quantity))value["quantity"]=std::clamp(quantity,1,1000);
        auto prompt=value.value("prompt",std::string("Pick up"));if(text_field("Prompt",prompt))value["prompt"]=prompt;number_field("Radius (m)",value,"radius",2,.5,10);
        policy_field(value,"collected","Collected",LedgerPolicy{LedgerScope::Save});
        ImGui::TextWrapped("A player using it gets the items; the host grants them and marks it collected in the same tick. A pickup that does not fit the inventory stays. By default it is collected once for good.");
        ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){bool done=false;action(editor,[&]{decode_pickup(value.dump());scene_set_component(editor,object,"pickup",value,"Edited the item pickup.");done=true;});return done;}
    }
    if(auto found=plan.altars.find(object);found!=plan.altars.end()&&ImGui::CollapsingHeader("Altar",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=Json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("altar");
        auto name=value.value("name",std::string("Altar"));if(text_field("Name",name))value["name"]=name;auto prompt=value.value("prompt",std::string("Altar"));if(text_field("Prompt",prompt))value["prompt"]=prompt;number_field("Radius (m)",value,"radius",2.5,.5,10);
        const std::string checkpoint=value.value("checkpoint",std::string("rest"));int chosen=checkpoint=="never"?0:checkpoint=="discover"?1:2;ImGui::SetNextItemWidth(220);if(ImGui::Combo("Sets the checkpoint",&chosen,"Never\0When discovered\0When resting\0")){const char* names[]{"never","discover","rest"};value["checkpoint"]=names[chosen];}
        flag_field("Can rest here",value,"rest",true);flag_field("Fast travel destination",value,"fast_travel",true);flag_field("Saves when resting",value,"save",true);
        double forward=value.contains("arrival")?value["arrival"][0].get<double>():1.5,right=value.contains("arrival")?value["arrival"][1].get<double>():0;ImGui::SetNextItemWidth(120);bool moved=ImGui::InputDouble("Arrival ahead (m)",&forward,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);ImGui::SetNextItemWidth(120);moved|=ImGui::InputDouble("Arrival right (m)",&right,0,0,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue);if(moved)value["arrival"]=Json::array({std::clamp(forward,-10.,10.),std::clamp(right,-10.,10.)});
        ImGui::TextWrapped("Using it discovers it for the whole party and opens the altar menu. Resting restores Health, resets what resets on rest and builds the scene again. The party arrives and respawns at the arrival point, facing the way the altar faces. Fast travel is chosen by the host.");
        ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){bool done=false;action(editor,[&]{decode_altar(value.dump());scene_set_component(editor,object,"altar",value,"Edited the altar.");done=true;});return done;}
    }
    if(auto found=plan.movers.find(object);found!=plan.movers.end()&&ImGui::CollapsingHeader(Json::parse(found->second).value("door",true)?"Door or gate":"Movable platform",ImGuiTreeNodeFlags_DefaultOpen)){
        auto value=Json::parse(found->second);const auto before=value;ImGui::BeginDisabled(playing);ImGui::PushID("mover");const bool door=value.value("door",true);
        vector_field("Half extents (m)",value,"half",door?std::array<double,3>{1,1.5,.25}:std::array<double,3>{1,1.5,.25},.05,50);vector_field("Moves by (m, own axes)",value,"offset",{0,3,0},-200,200);
        number_field("Speed (m/s)",value,"speed",2,.05,50);number_field("Wait (s)",value,"wait",2,0,600);
        const std::string activation=value.value("activation",std::string("interact"));int a=activation=="interact"?0:activation=="event"?1:2;ImGui::SetNextItemWidth(220);if(ImGui::Combo("Activation",&a,"Used on purpose\0On a world event\0By itself\0")){const char* names[]{"interact","event","auto"};value["activation"]=names[a];if(a!=0){value.erase("locked");value.erase("travel");}if(a==1&&!value.contains("event"))value["event"]="platform.start";if(a!=1)value.erase("event");}
        const std::string behaviour=value.value("behaviour",std::string("toggle"));int b=behaviour=="toggle"?0:behaviour=="return"?1:2;ImGui::SetNextItemWidth(220);if(ImGui::Combo("Behaviour",&b,"Toggle open and closed\0Go, wait, come back\0Back and forth\0")){const char* names[]{"toggle","return","loop"};value["behaviour"]=names[b];}
        if(a==1){auto event=value.value("event",std::string());if(text_field("World event",event))value["event"]=event;}
        if(a==0){auto prompt=value.value("prompt",std::string(door?"Open":"Activate"));if(text_field("Prompt",prompt))value["prompt"]=prompt;number_field("Reach (m)",value,"radius",2.5,.5,10);
            bool locked=value.value("locked",false);if(ImGui::Checkbox("Locked",&locked)){value["locked"]=locked;if(locked&&!value.contains("requires"))value["requires"]=Json::object();}
            if(locked){auto locked_prompt=value.value("locked_prompt",std::string("Locked"));if(text_field("Locked prompt",locked_prompt))value["locked_prompt"]=locked_prompt;}
            ImGui::SeparatorText(locked?"Unlocked by":"Requires");auto requirement=value.value("requires",Json::object());const auto required_before=requirement;
            item_combo(editor,"Item",requirement,"item",true);if(requirement.contains("item")){int count=requirement.value("quantity",1);ImGui::SetNextItemWidth(120);if(ImGui::InputInt("Item quantity",&count))requirement["quantity"]=std::clamp(count,1,1000);flag_field("Consume the item",requirement,"consume",false);}else{requirement.erase("quantity");requirement.erase("consume");}
            auto tag=requirement.value("tag",std::string());if(text_field("World tag",tag)){if(tag.empty())requirement.erase("tag");else requirement["tag"]=tag;}
            const std::string encounter=requirement.value("encounter",std::string());std::string label="None";for(const auto& [id,json]:plan.encounters)if(id.text()==encounter)for(const auto& origin:plan.origins)if(origin.object==id)label=origin.name;
            ImGui::SetNextItemWidth(220);if(ImGui::BeginCombo("Encounter completed",label.c_str())){if(ImGui::Selectable("None",encounter.empty()))requirement.erase("encounter");for(const auto& [id,json]:plan.encounters){std::string name="Encounter";for(const auto& origin:plan.origins)if(origin.object==id)name=origin.name;if(ImGui::Selectable((name+"##"+id.text()).c_str(),id.text()==encounter))requirement["encounter"]=id.text();}ImGui::EndCombo();}
            if(requirement!=required_before){if(requirement.empty())value.erase("requires");else value["requires"]=requirement;}
            // A door may lead into another scene once it is open.
            ImGui::SeparatorText("Leads to");static std::vector<Shell::TravelScene> scenes;static bool listed=false;if(!listed){scenes=scene_travel_list(editor);listed=true;}
            const std::string scene=value.contains("travel")?value["travel"].value("scene",std::string()):std::string();std::string scene_label="Nowhere";for(const auto& known:scenes)if(known.id.text()==scene)scene_label=known.path;
            ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Scene",scene_label.c_str())){if(ImGui::Selectable("Nowhere",scene.empty()))value.erase("travel");for(const auto& known:scenes)if(ImGui::Selectable(known.path.c_str(),known.id.text()==scene))value["travel"]={{"scene",known.id.text()}};ImGui::EndCombo();}ImGui::SameLine();if(ImGui::SmallButton("Refresh##mover-scenes"))listed=false;
            if(value.contains("travel")){const std::string entry=value["travel"].value("entry",std::string());std::string entry_label="The scene's own start";for(const auto& known:scenes)if(known.id.text()==scene)for(const auto& [id,name]:known.entries)if(id.text()==entry)entry_label=name;
                ImGui::SetNextItemWidth(260);if(ImGui::BeginCombo("Entry point",entry_label.c_str())){if(ImGui::Selectable("The scene's own start",entry.empty()))value["travel"].erase("entry");for(const auto& known:scenes)if(known.id.text()==scene)for(const auto& [id,name]:known.entries)if(ImGui::Selectable((name+"##"+id.text()).c_str(),id.text()==entry))value["travel"]["entry"]=id.text();ImGui::EndCombo();}}}
        flag_field("Starts open (at its end)",value,"open",false);
        ImGui::SeparatorText("Persistence");if(value.value("locked",false))policy_field(value,"unlocked","Unlocked",LedgerPolicy{LedgerScope::Save});
        policy_field(value,"open","Open",door?LedgerPolicy{LedgerScope::Save,LedgerReset::Rest}:LedgerPolicy{LedgerScope::Session,LedgerReset::SceneEntry});
        ImGui::TextWrapped("A kinematic box that moves between where the object stands and that point moved by the offset, along the object's own axes. Players ride it. The host moves it; only whether it is unlocked and open is kept, never where it is in between.");
        ImGui::PopID();ImGui::EndDisabled();
        if(value!=before){bool done=false;action(editor,[&]{decode_mover(value.dump());scene_set_component(editor,object,"mover",value,door?"Edited the door.":"Edited the movable platform.");done=true;});return done;}
    }
    return false;
}

StableId place_world_object(Controller& editor,int gameplay,const Transform& at){
    StableId placed;
    if(gameplay==place_pickup){AssetId item;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".daitem"){item=asset.id;break;}if(item==AssetId{})throw std::runtime_error("Create an item in the Items tab before placing a pickup");
        placed=scene_add_object(editor,{{"name","Item pickup"},{"pickup",{{"item",item.text()},{"quantity",1}}}},at,"Placed an item pickup.");}
    else if(gameplay==place_altar)placed=scene_add_object(editor,{{"name","Altar"},{"altar",{{"name","Altar"}}}},at,"Placed an altar.");
    else if(gameplay==place_door)placed=scene_add_object(editor,{{"name","Door"},{"mover",{{"door",true},{"half",Json::array({1.0,1.5,.25})},{"offset",Json::array({0.0,3.0,0.0})}}}},at,"Placed a door.");
    else if(gameplay==place_platform)placed=scene_add_object(editor,{{"name","Movable platform"},{"mover",{{"door",false},{"activation","auto"},{"behaviour","loop"},{"half",Json::array({1.5,.25,1.5})},{"offset",Json::array({0.0,0.0,6.0})},{"speed",2.0},{"wait",1.5}}}},at,"Placed a movable platform.");
    else throw std::runtime_error("Unknown world object placement");
    editor.selected=placed;return placed;
}
}
