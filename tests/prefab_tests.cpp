#include "../apps/editor/prefab_commands.hpp"
#include <darkangel/prefab_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/editor_document.hpp>
#include <darkangel/world_objects.hpp>
#include <darkangel/hash.hpp>
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
    // R4 remainder. Create Prefab from Selection for a referenced object: the prefab exports it and the referencing object is
    // rewritten to address the export; identities and the reference's target stay the same.
    {const auto target="00000000000000000000000000000011",pointer="00000000000000000000000000000012",outside="00000000000000000000000000000013";const auto scene_id=AssetId::random();
     Json graph={{"schema",1},{"asset",scene_id.text()},{"entities",{{target,{{"name","Target"},{"target",nullptr},{"types",transform(2,0,2,0)},{"altar",{{"name","Target"}}}}},{pointer,{{"name","Pointer"},{"target",target},{"types",transform(5,0,5,0)}}},{outside,{{"name","Outside"},{"target",nullptr},{"types",transform(9,0,9,.5)}}}}},{"mounts",Json::object()},{"exports",{{"00000000000000000000000000000021",target}}}};
     EditorDocument scene_doc(scene_id,placement,{{scene_id,graph.dump()}});auto object_of=[&](const std::string& local){for(const auto& origin:scene_doc.plan().origins)if(origin.local_path==placement.text()+"/"+local)return origin.object;throw std::runtime_error("Object missing");};
     const auto target_object=object_of(target),pointer_object=object_of(pointer),outside_object=object_of(outside);const auto made_id=AssetId::random();
     // Multi-object capture: the pivot is the first object; the second keeps its pose relative to it.
     const auto made=prefab_from_objects(scene_doc,{target_object,outside_object},made_id,"Captured");check(Json::parse(made).contains("exports"),"A referenced captured object is exported");
     const auto mount=replace_with_instance(scene_doc,{target_object,outside_object},made_id,made);
     check(member_object(scene_doc,mount)==target_object||member_object(scene_doc,mount)==outside_object,"Captured members keep their identities");
     {auto at=pose(scene_doc,outside_object);check(std::abs(at.x-9)<1e-9&&std::abs(at.z-9)<1e-9&&std::abs(at.yaw-.5)<1e-12,"Captured member keeps its pose");}
     check(scene_doc.world().read(scene_doc.world().find(pointer_object)).target==target_object,"The rewritten reference addresses the same object through the export");
     check(Json::parse(scene_doc.source(scene_id))["entities"][pointer]["target"].is_object(),"The reference is rewritten to the prefab export");
     rejects([&]{prefab_from_objects(scene_doc,{pointer_object},AssetId::random(),"Pointer");},"An object that references outside its capture is refused");
     // Unpack keeps identities, values, poses and the reference.
     const auto fingerprint_before=scene_doc.plan().fingerprint;auto keys=unpack_instance(scene_doc,mount);check(keys.size()==2&&prefab_instances(scene_doc).empty(),"Unpack removes the instance");
     check(scene_doc.world().valid(scene_doc.world().find(target_object))&&scene_doc.world().valid(scene_doc.world().find(outside_object)),"Unpacked objects keep their persistent identities");
     {auto at=pose(scene_doc,outside_object);check(std::abs(at.x-9)<1e-9&&std::abs(at.z-9)<1e-9&&std::abs(at.yaw-.5)<1e-12,"Unpacked object keeps its pose");}
     check(scene_doc.world().read(scene_doc.world().find(pointer_object)).target==target_object,"Unpack rewrites the reference to the new object");
     check(altar_name(scene_doc,target_object)=="Target","Unpacked object keeps its values");
     // An unpacked object's record edits keep the retained identity (the scene's retained map applies to record edits too).
     {auto record=Json::parse(*scene_doc.entity(keys.at(outside)));record["name"]="Moved";scene_doc.commit(scene_doc.prepare_entities(scene_doc.revision(),{{keys.at(outside),record.dump()}}),"Rename");check(scene_doc.world().valid(scene_doc.world().find(outside_object)),"Record edits keep retained identity");}
     scene_doc.undo(scene_doc.revision());scene_doc.undo(scene_doc.revision());check(prefab_instances(scene_doc).size()==1&&scene_doc.plan().fingerprint==fingerprint_before,"Undo restores the instance exactly");}
    // Nested prefabs: an outer prefab mounts an inner one with a placement; scenes link both, opening resolves the closure.
    {const auto inner=AssetId::random(),outer=AssetId::random(),scene_id=AssetId::random();const auto inner_text=prefab(inner,member,"Inner",0);
     Json outer_value={{"schema",1},{"kind","prefab"},{"asset",outer.text()},{"name","Outer"},{"entities",{{other_member,{{"name","Outer altar"},{"target",nullptr},{"types",transform(0,0,0,0)},{"altar",{{"name","Outer"}}}}}}},{"mounts",{{"00000000000000000000000000000031",{{"asset",inner.text()},{"placement",{{"x",2.0},{"y",0.0},{"z",0.0},{"yaw",0.0}}}}}}}};
     rejects([&]{decode_prefab(outer_value.dump());},"A nested prefab needs its inner prefab's source");check(decode_prefab(outer_value.dump(),{{inner,inner_text}}).members.size()==1,"Nested prefab decodes with its closure");
     Json empty={{"schema",1},{"asset",scene_id.text()},{"entities",Json::object()},{"mounts",Json::object()}};EditorDocument scene_doc(scene_id,placement,{{scene_id,empty.dump()}});
     const AssemblySources project{{inner,inner_text},{outer,outer_value.dump()}};auto source=[&](AssetId id)->std::optional<std::string>{if(project.contains(id))return project.at(id);return std::nullopt;};
     auto closure=EditorDocument::link_closure(outer,outer_value.dump(),source);check(closure.size()==2,"Link closure follows nested mounts");closure.erase(outer);
     const auto mount=place_prefab(scene_doc,outer,outer_value.dump(),{10,0,0,0},closure);check(scene_doc.plan().altars.size()==2,"Nested prefab instance resolves both levels");
     bool nested_found=false;for(const auto& origin:scene_doc.plan().origins)if(auto found=prefab_instance_of(scene_doc,origin.object);found&&found->nested){nested_found=true;check(found->mount==mount,"Nested member belongs to the top-level instance");auto at=pose(scene_doc,origin.object);check(std::abs(at.x-12)<1e-9,"Nested placement composes");}
     check(nested_found,"Nested member is reported as nested");rejects([&]{unpack_instance(scene_doc,mount);},"Unpacking a prefab with nested prefabs is refused");
     const auto folder2=std::filesystem::temp_directory_path()/("prefab-nested-"+AssetId::random().text());std::filesystem::create_directories(folder2/"prefabs");scene_doc.save(folder2/"scene.dascene");
     check(Json::parse([&]{std::ifstream in(folder2/"scene.dascene");return std::string(std::istreambuf_iterator<char>(in),{});}())["links"].size()==1,"A scene saves only its direct links");
     {std::ofstream(folder2/"prefabs/inner.daprefab",std::ios::binary)<<inner_text;std::ofstream(folder2/"prefabs/outer.daprefab",std::ios::binary)<<outer_value.dump();}
     auto root_found=EditorDocument::find_source_root(folder2/"scene.dascene");check(root_found&&std::filesystem::equivalent(*root_found,folder2),"The source mount of a scene is found");
     auto reopened=EditorDocument::open(folder2/"scene.dascene",EditorDocument::project_links(*root_found));check(reopened->plan().fingerprint==scene_doc.plan().fingerprint,"Tools resolve nested links through the project link source");
     check(prefab_consumers(folder2,inner).size()==1&&prefab_consumers(folder2,inner)[0].kind=="prefab"&&prefab_consumers(folder2,outer).size()==1&&prefab_consumers(folder2,outer)[0].kind=="scene","Consumers list scenes and nesting prefabs");
     // Prefab sources that mount others cook with their closure, rename keeps identity, delete refuses used prefabs.
     {const auto source_root=folder2/"cook";std::filesystem::create_directories(source_root/"prefabs");AssetService assets(source_root,folder2/"cache");assets.scan();
      check(assets.create_native("prefabs/inner.daprefab",inner_text).changed,"Inner prefab cooks");check(assets.create_native("prefabs/outer.daprefab",outer_value.dump()).changed,"Nested prefab cooks with its closure");
      auto sha=[&](const char* path){std::ifstream in(source_root/path,std::ios::binary);return sha256(std::string(std::istreambuf_iterator<char>(in),{}));};
      assets.rename_native("prefabs/inner.daprefab",sha("prefabs/inner.daprefab"),"prefabs/inner_renamed.daprefab");bool kept=false;for(const auto& asset:assets.assets())if(asset.id==inner&&asset.path=="prefabs/inner_renamed.daprefab")kept=true;check(kept,"Rename keeps the prefab identity");
      rejects([&]{assets.delete_item("prefabs/inner_renamed.daprefab",sha("prefabs/inner_renamed.daprefab"));},"A prefab another prefab mounts cannot be deleted");
      assets.delete_item("prefabs/outer.daprefab",sha("prefabs/outer.daprefab"));assets.delete_item("prefabs/inner_renamed.daprefab",sha("prefabs/inner_renamed.daprefab"));check(assets.assets().empty(),"Unused prefabs delete");}
     std::filesystem::remove_all(folder2);}
    // Variants: a variant patches its base; a base change needs an explicit rebase, which refuses broken patches.
    {const auto base=AssetId::random(),variant=AssetId::random(),scene_id=AssetId::random();const auto base_text=prefab(base,member,"Base");
     auto value=make_variant(variant,"Variant",base,base_text);set_variant_field(value,base_text,member,"altar","name",Json("Variant"));check(value["patches"].size()==1,"Variant field patch");
     set_variant_field(value,base_text,member,"altar","name",Json("Base"));check(value["patches"].empty(),"Base value removes the variant patch");set_variant_field(value,base_text,member,"altar","name",Json("Variant"));
     check(decode_prefab(value.dump(),{{base,base_text}}).members.size()==1,"Variant decodes against its base");
     check(EditorDocument::link_closure(variant,value.dump(),[&](AssetId id)->std::optional<std::string>{if(id==base)return base_text;return std::nullopt;}).contains(base),"A variant's link closure includes its base");
     Json empty={{"schema",1},{"asset",scene_id.text()},{"entities",Json::object()},{"mounts",Json::object()}};EditorDocument scene_doc(scene_id,placement,{{scene_id,empty.dump()}});
     const auto mount=place_prefab(scene_doc,variant,value.dump(),{0,0,0,0},{{base,base_text}});check(altar_name(scene_doc,member_object(scene_doc,mount))=="Variant","Variant instance resolves its patch");
     const auto changed=prefab(base,member,"Base 2",1.0);rejects([&]{scene_doc.link({{base,changed}});},"A changed base refuses the variant until rebase");
     auto rebased=rebase_variant(value,changed);scene_doc.link({{base,changed},{variant,rebased.dump()}});check(altar_name(scene_doc,member_object(scene_doc,mount))=="Variant","Rebased variant keeps its patch");
     Json broken=Json::parse(changed);broken["entities"][member].erase("altar");broken["entities"][member]["interactable"]={{"prompt","Use"},{"event","e"},{"radius",1.0},{"once",true}};
     check(!variant_conflicts(rebased,broken.dump()).empty(),"Rebase reports patches the new base breaks");rejects([&]{rebase_variant(rebased,broken.dump());},"Rebase refuses broken patches");
     // Apply Selected onto a variant becomes a variant patch.
     set_instance_field(scene_doc,mount,member,"altar","name",Json("Instance"));auto applied=Json::parse(prefab_with_overrides(scene_doc,mount,{{member,"altar","name"}},scene_doc.links().at(variant)));
     check(applied["patches"].size()==1&&applied["patches"][0]["value"]=="Instance","Apply Selected on a variant writes a variant patch");}
    std::filesystem::remove_all(folder);
    std::cout<<"R4 prefabs: placement composition, independent instances, overrides/Revert, template propagation, Apply Selected, instance moves, Swap identity and refusal, conversion identity, duplicate isolation, links Save/open and cook, referenced capture with exports, multi-object capture, Unpack identity, nested prefabs and tool link resolution, consumers, rename/delete, variants and rebase passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
