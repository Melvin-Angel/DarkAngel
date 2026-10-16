#include "../apps/editor/prefab_commands.hpp"
#include <darkangel/prefab_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/editor_document.hpp>
#include <darkangel/world_objects.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
using namespace darkangel;using namespace darkangel::editor_app;using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
Json transform(double x,double y,double z,double yaw){return {{"1",{{"version",2},{"fields",{{"1",yaw},{"2",x},{"3",y},{"4",z},{"5",0.0},{"6",0.0},{"7",1.0}}}}},{"2",{{"version",1},{"fields",{{"1",100},{"2",100}}}}}};}
std::string prefab(AssetId id,const std::string& member,const std::string& altar_name,double local_x=0){
    Json value={{"schema",1},{"kind","prefab"},{"asset",id.text()},{"name","Altar prefab"},{"entities",{{member,{{"name","Altar"},{"target",nullptr},{"types",transform(local_x,0,0,0)},{"altar",{{"name",altar_name}}}}}}}};return value.dump();}
Transform pose(EditorDocument& document,StableId object){return document.world().read(document.world().find(object)).transform;}
StableId member_object(EditorDocument& document,const std::string& mount){const auto prefix=document.placement().text()+"/"+mount+"/";for(const auto& origin:document.plan().origins)if(origin.local_path.starts_with(prefix))return origin.object;throw std::runtime_error("Instance member missing");}
std::string altar_name(EditorDocument& document,StableId object){auto found=document.plan().altars.find(object);check(found!=document.plan().altars.end(),"Altar missing from plan");return decode_altar(found->second).name;}
}
int main(){try{
    const auto member="00000000000000000000000000000001",other_member="00000000000000000000000000000002",loose="00000000000000000000000000000003";
    const auto a=AssetId::random(),b=AssetId::random(),c=AssetId::random(),root=AssetId::random();const StableId placement{0,1};
    // Prefab sources validate standalone; scene roles and invalid components are refused.
    check(decode_prefab(prefab(a,member,"Altar")).members.size()==1,"Prefab decode");
    {auto bad=Json::parse(prefab(a,member,"Altar"));bad["entities"][member]["spawn"]={{"npc",AssetId::random().text()}};rejects([&]{decode_prefab(bad.dump());},"A prefab must not hold a spawn point");}
    {auto bad=Json::parse(prefab(a,member,"Altar"));bad["entities"][member]["altar"]["colour"]="red";rejects([&]{decode_prefab(bad.dump());},"A prefab component must decode");}
    Json scene={{"schema",1},{"asset",root.text()},{"entities",{{loose,{{"name","Loose altar"},{"target",nullptr},{"types",transform(3,0,4,.25)},{"altar",{{"name","Loose"}}}}}}},{"mounts",Json::object()}};
    EditorDocument document(root,placement,{{root,scene.dump()}});const auto before=document.plan().fingerprint;
    const auto prefab_a=prefab(a,member,"Altar",1.0);
    // Placement composes position and yaw (yaw about +Y, local +Z faces (sin yaw, cos yaw)).
    const auto first=place_prefab(document,a,prefab_a,{10,0,0,1.5707963267948966});const auto first_object=member_object(document,first);
    {auto at=pose(document,first_object);check(std::abs(at.x-10)<1e-9&&std::abs(at.z+1)<1e-9&&std::abs(at.yaw-1.5707963267948966)<1e-12,"Mount placement composes the member transform");}
    const auto second=place_prefab(document,a,prefab_a,{-5,0,2,0});const auto second_object=member_object(document,second);
    check(first_object!=second_object&&document.plan().altars.size()==3,"Two instances are two persistent objects");
    // Undo/Redo of placement; links are not history.
    document.undo(document.revision());check(document.plan().altars.size()==2&&document.links().contains(a),"Undo removes the instance, keeps the link");document.redo(document.revision());check(member_object(document,second)==second_object,"Redo restores the same identity");
    // Overrides: one instance changes its altar name; the other follows the prefab. Setting the prefab value removes the patch.
    set_instance_field(document,first,member,"altar","name",Json("Shrine"));
    check(altar_name(document,first_object)=="Shrine"&&altar_name(document,second_object)=="Altar"&&instance_overrides(document,first).size()==1,"Instance override");
    set_instance_field(document,first,member,"altar","name",Json("Altar"));check(instance_overrides(document,first).empty(),"Prefab value removes the override");
    set_instance_field(document,first,member,"altar","name",Json("Shrine"));
    // A template change reaches the follower and leaves the override in place.
    document.link({{a,prefab(a,member,"Temple",1.0)}});
    check(altar_name(document,first_object)=="Shrine"&&altar_name(document,second_object)=="Temple","Template change keeps local override");
    // Add/remove component on an instance, then Revert.
    add_instance_component(document,second,member,"interactable",{{"prompt","Pray"},{"event","object.used"},{"radius",2.0},{"once",true}});check(document.plan().interactables.contains(second_object),"Added component on instance");
    revert_overrides(document,second,{{member,"interactable",""}});check(!document.plan().interactables.contains(second_object),"Revert removes the added component");
    // Apply Selected: the definition takes the instance value, the override goes away, the result is unchanged for this instance.
    const auto applied=prefab_with_overrides(document,first,{{member,"altar","name"}},document.links().at(a));
    check(Json::parse(applied)["entities"][member]["altar"]["name"]=="Shrine","Apply writes the selected override into the definition");
    document.link({{a,applied}});check(altar_name(document,first_object)=="Shrine","Applied definition with override still present");
    revert_overrides(document,first,{{member,"altar","name"}});check(altar_name(document,first_object)=="Shrine"&&altar_name(document,second_object)=="Shrine"&&instance_overrides(document,first).empty(),"Apply Selected removes exactly the applied override");
    // Moving a member moves the instance: placement changes, no transform override is written.
    {std::vector<PropertyChange> move{{first_object,1,2,20.0},{first_object,1,1,0.0}};auto edit=document.prepare(document.revision(),move);document.commit(edit);
     auto at=pose(document,first_object);check(std::abs(at.x-20)<1e-9&&std::abs(at.yaw)<1e-12,"Moved member lands where requested");
     auto graph=Json::parse(document.source(root));check(graph["mounts"][first].value("patches",Json::array()).empty(),"Moving an instance writes no transform override");
     auto placed=instance_placement(document,first);check(std::abs(placed.x-19)<1e-9&&std::abs(placed.yaw)<1e-12,"Instance placement follows the member move");}
    // Swap keeps mount, placement and persistent identity across different member keys; incompatible prefabs are refused.
    const auto prefab_b=prefab(b,other_member,"Other altar");
    check(prepare_swap(document,second,b,prefab_b,false).compatible,"Compatible swap");
    commit_swap(document,second,b,prefab_b,false);check(member_object(document,second)==second_object&&altar_name(document,second_object)=="Other altar","Swap keeps identity and adopts the replacement");
    {Json pickup=Json::parse(prefab(c,member,"x"));pickup["entities"][member].erase("altar");pickup["entities"][member]["pickup"]={{"item",AssetId::random().text()},{"quantity",1}};
     auto preview=prepare_swap(document,first,c,pickup.dump(),false);check(!preview.compatible&&!preview.reason.empty(),"Incompatible swap refused");}
    document.undo(document.revision());check(member_object(document,second)==second_object&&altar_name(document,second_object)=="Shrine","Swap Undo restores the old link");document.redo(document.revision());
    // Create Prefab from Selection keeps the loose object's persistent identity; a duplicate gets its own.
    const auto loose_object=[&]{for(const auto& origin:document.plan().origins)if(origin.local_path==placement.text()+"/"+loose)return origin.object;throw std::runtime_error("Loose object missing");}();
    const auto made=prefab_from_object(document,loose_object,c,"LooseAltar");const auto converted=replace_with_instance(document,loose_object,c,made);
    check(member_object(document,converted)==loose_object,"Conversion retains the persistent identity");
    {auto at=pose(document,loose_object);check(std::abs(at.x-3)<1e-9&&std::abs(at.z-4)<1e-9&&std::abs(at.yaw-.25)<1e-12,"Conversion keeps the pose");}
    const auto copy=duplicate_instance(document,converted,{2,0,0,0});check(member_object(document,copy)!=loose_object,"A duplicate never shares retained identity");
    delete_instance(document,copy);check(prefab_instances(document).size()==3,"Delete instance");
    // Save names links without copying them; reopening resolves them through the project, and refuses a missing one.
    const auto folder=std::filesystem::temp_directory_path()/("prefab-tests-"+AssetId::random().text());std::filesystem::create_directories(folder);
    document.save(folder/"scene.dascene");auto saved=Json::parse([&]{std::ifstream in(folder/"scene.dascene");return std::string(std::istreambuf_iterator<char>(in),{});}());
    check(saved.contains("links")&&saved["links"].size()==3&&!saved["assets"].contains(a.text()),"Saved scene links prefabs by identity only");
    auto reopened=EditorDocument::open(folder/"scene.dascene",[&](AssetId id)->std::optional<std::string>{if(document.links().contains(id))return document.links().at(id);return std::nullopt;});
    check(reopened->plan().fingerprint==document.plan().fingerprint&&!reopened->dirty(),"Reopened scene resolves its linked prefabs identically");
    rejects([&]{EditorDocument::open(folder/"scene.dascene");},"A scene with a missing linked prefab must not open silently");
    rejects([&]{document.prepare_source(document.revision(),a,prefab(a,member,"x"));},"Linked prefabs are not edited through the scene");
    // A prefab change that breaks an override is refused, leaving the scene unchanged.
    set_instance_field(document,first,member,"altar","name",Json("Edited"));{Json broken=Json::parse(document.links().at(a));broken["entities"][member].erase("altar");broken["entities"][member]["interactable"]={{"prompt","Use"},{"event","e"},{"radius",1.0},{"once",true}};
     const auto fingerprint=document.plan().fingerprint;rejects([&]{document.link({{a,broken.dump()}});},"A definition that breaks an instance override is refused");check(document.plan().fingerprint==fingerprint,"Refused link leaves the scene unchanged");}
    // Legacy scenes without placements resolve exactly as before.
    {EditorDocument plain(root,placement,{{root,scene.dump()}});check(plain.plan().fingerprint==before,"Legacy scene unchanged");}
    // Prefab sources cook through the asset service like the other native sources.
    {const auto source=folder/"sources";std::filesystem::create_directories(source/"prefabs");AssetService assets(source,folder/"cache");assets.scan();
     auto cooked=assets.create_native("prefabs/LooseAltar.daprefab",made);check(cooked.changed,"Prefab source cooks");
     rejects([&]{auto bad=Json::parse(prefab(AssetId::random(),member,"Altar"));bad["entities"][member]["altar"]["colour"]="red";assets.create_native("prefabs/Bad.daprefab",bad.dump());},"Invalid prefab source refused at cook");}
    std::filesystem::remove_all(folder);
    std::cout<<"R4 prefabs: placement composition, independent instances, overrides/Revert, template propagation, Apply Selected, instance moves, Swap identity and refusal, conversion identity, duplicate isolation, links Save/open and cook passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
