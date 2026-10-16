#include "prefab_commands.hpp"
#include <darkangel/prefab_assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
namespace darkangel::editor_app {
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
void require(bool test,const std::string& error){if(!test)throw std::runtime_error(error);}
Json parse(const std::string& text){return Json::parse(text);}
Json root_graph(const EditorDocument& document){return parse(document.source(document.root()));}
void commit_root(EditorDocument& document,const Json& graph,const char* origin){auto edit=document.prepare_source(document.revision(),document.root(),graph.dump());document.commit(edit,origin);}
std::string fresh_key(){auto text=AssetId::random().text();text.erase(std::remove(text.begin(),text.end(),'-'),text.end());return text;}
bool world_object(const std::string& component){return component=="altar"||component=="mover"||component=="pickup"||component=="interactable"||component=="trigger";}
Json& mount_of(Json& graph,const std::string& mount){require(graph.contains("mounts")&&graph["mounts"].contains(mount),"The prefab instance is no longer in the scene");return graph["mounts"][mount];}
Json& patches_of(Json& mount){if(!mount.contains("patches")||mount["patches"].is_null())mount["patches"]=Json::array();return mount["patches"];}
Json placement_json(const PrefabPlacement& p){for(double v:{p.x,p.y,p.z,p.yaw})require(std::isfinite(v),"Prefab placement must be finite");return {{"x",p.x},{"y",p.y},{"z",p.z},{"yaw",p.yaw}};}
const std::string& linked_text(const EditorDocument& document,AssetId prefab){auto found=document.links().find(prefab);require(found!=document.links().end(),"The prefab is not linked by this scene");return found->second;}
bool component_patch(const Json& patch){auto op=patch.value("op","");return op=="SetComponentField"||op=="AddComponent"||op=="RemoveComponent";}
// The same component operations the assembly resolver applies, on one entity.
void apply_component_patch(Json& entity,const Json& patch){
    auto op=patch.at("op").get<std::string>();auto component=patch.at("component").get<std::string>();
    if(op=="SetComponentField"){require(entity.contains(component)&&entity[component].is_object(),"Override names a component the prefab no longer has");entity[component][patch.at("field").get<std::string>()]=patch.at("value");}
    else if(op=="AddComponent")entity[component]=patch.at("value");
    else if(op=="RemoveComponent")entity.erase(component);}
// Every typed patch operation of the assembly resolver, on an assembly graph (its entities by local key).
void apply_patch(Json& graph,const Json& operation){
    auto op=operation.at("op").get<std::string>();auto target=operation.at("target").get<std::string>();auto& entities=graph["entities"];
    if(op=="AddEntity"){require(!entities.contains(target),"AddEntity target exists");entities[target]=operation.at("value");return;}
    require(entities.contains(target),"Patch target removed or unresolved");auto& entity=entities[target];
    if(op=="RemoveEntity")entities.erase(target);
    else if(op=="SetProperty")entity["types"][std::to_string(operation.at("type").get<unsigned>())]["fields"][std::to_string(operation.at("property").get<unsigned>())]=operation.at("value");
    else if(op=="ReplaceSlot")entity["model"]=operation.at("asset");
    else if(op=="SetSpawn")entity["spawn"]=operation.at("value");
    else if(op=="BindScript")entity["scripts"][operation.at("binding").get<std::string>()]=operation.at("value");
    else if(op=="RemoveScript")entity["scripts"].erase(operation.at("binding").get<std::string>());
    else if(op=="SetName")entity["name"]=operation.at("value");
    else if(component_patch(operation))apply_component_patch(entity,operation);
    else throw std::runtime_error("Unsupported typed patch operation");}
bool selected(const Json& patch,const std::vector<OverrideKey>& keys){
    if(!component_patch(patch))return false;
    for(const auto& key:keys)if(patch.at("target")==key.member&&patch.at("component")==key.component&&(key.field.empty()||patch.value("field","")==key.field))return true;return false;}
std::set<std::string> contract(const Json& entity){std::set<std::string> result;for(const auto& [key,value]:entity.items())if(world_object(key)&&!value.is_null())result.insert(key);return result;}
std::vector<std::string> member_keys(const Json& entities){std::vector<std::string> result;for(const auto& [local,entity]:entities.items())result.push_back(local);return result;}
// The links of a document plus extra sources (a replacement and its dependencies).
AssemblySources with(const EditorDocument& document,const AssemblySources& extra){auto all=document.links();for(const auto& [id,text]:extra)all[id]=text;return all;}
// A transform's yaw, x, y and z (type 1 fields 1-4).
struct Pose {double yaw,x,y,z;};
Pose pose_of(const Json& entity){const auto& f=entity.at("types").at("1").at("fields");return {f.at("1").get<double>(),f.at("2").get<double>(),f.at("3").get<double>(),f.at("4").get<double>()};}
void set_pose(Json& entity,const Pose& p){auto& f=entity["types"]["1"]["fields"];f["1"]=p.yaw;f["2"]=p.x;f["3"]=p.y;f["4"]=p.z;}
// Placement composition as the resolver does it (position and yaw), and its inverse.
Pose placed(const PrefabPlacement& at,const Pose& local){const double c=std::cos(at.yaw),s=std::sin(at.yaw);return {local.yaw+at.yaw,at.x+c*local.x+s*local.z,at.y+local.y,at.z-s*local.x+c*local.z};}
Pose unplaced(const PrefabPlacement& at,const Pose& world){const double c=std::cos(at.yaw),s=std::sin(at.yaw),dx=world.x-at.x,dz=world.z-at.z;return {world.yaw-at.yaw,c*dx-s*dz,world.y-at.y,s*dx+c*dz};}
// An EntityRef as written in a source: a local key string, {"local": key}, or {"mount": m, "export": e}.
bool refers_local(const Json& ref,const std::string& local){return (ref.is_string()&&ref==local)||(ref.is_object()&&ref.contains("local")&&ref["local"]==local);}
// Applies f to every EntityRef an entity makes (its target and its scripts' entity_refs); f may replace the reference.
template<class F> void each_reference(Json& entity,F&& f){
    if(entity.contains("target")&&!entity["target"].is_null())f(entity["target"]);
    if(entity.contains("scripts")&&entity["scripts"].is_object())for(auto& [attachment,script]:entity["scripts"].items())if(script.contains("entity_refs")&&script["entity_refs"].is_object())for(auto& [field,ref]:script["entity_refs"].items())f(ref);}
bool names(const Json& value,const std::string& local){if(value.is_string())return value==local;if(value.is_object()){if(value.contains("local")&&value["local"]==local)return true;for(const auto& [key,item]:value.items())if(names(item,local))return true;}if(value.is_array())for(const auto& item:value)if(names(item,local))return true;return false;}
// Whether an entity mentions `local` anywhere other than through the EntityRefs each_reference visits.
bool names_elsewhere(Json entity,const std::string& local){entity.erase("types");entity.erase("name");entity.erase("model");entity.erase("target");if(entity.contains("scripts")&&entity["scripts"].is_object())for(auto& [attachment,script]:entity["scripts"].items())script.erase("entity_refs");return names(entity,local);}
}
Json prefab_entities(AssetId prefab,const AssemblySources& sources){
    auto found=sources.find(prefab);require(found!=sources.end(),"The prefab's source is not available");auto graph=parse(found->second);
    if(graph.contains("base")&&!graph["base"].is_null()){auto base=prefab_entities(AssetId::parse(graph["base"].get<std::string>()),sources);Json resolved={{"entities",base}};
        for(const auto& operation:graph.value("patches",Json::array()))apply_patch(resolved,operation);return resolved["entities"];}
    return graph.value("entities",Json::object());
}
std::optional<PrefabInstanceRef> prefab_instance_of(EditorDocument& document,StableId object){
    const auto prefix=document.placement().text()+"/";
    for(const auto& origin:document.plan().origins)if(origin.object==object){
        if(!origin.local_path.starts_with(prefix)||!document.links().contains(origin.definition))return std::nullopt;
        auto rest=origin.local_path.substr(prefix.size());auto slash=rest.find('/');if(slash==rest.npos)return std::nullopt;
        auto graph=root_graph(document);auto mount=rest.substr(0,slash);
        if(!graph.contains("mounts")||!graph["mounts"].contains(mount))return std::nullopt;const auto asset=AssetId::parse(graph["mounts"][mount].at("asset").get<std::string>());
        if(!document.links().contains(asset))return std::nullopt;
        const bool nested=rest.find('/',slash+1)!=rest.npos;
        return PrefabInstanceRef{mount,rest.substr(slash+1),asset,nested};}
    return std::nullopt;
}
std::vector<PrefabInstanceRef> prefab_instances(const EditorDocument& document){
    std::vector<PrefabInstanceRef> result;auto graph=root_graph(document);
    const auto mounts=graph.value("mounts",Json::object());for(const auto& [mount,value]:mounts.items()){auto asset=AssetId::parse(value.at("asset").get<std::string>());if(document.links().contains(asset))result.push_back({mount,{},asset});}
    return result;
}
PrefabPlacement instance_placement(const EditorDocument& document,const std::string& mount){
    auto graph=root_graph(document);const auto& value=mount_of(graph,mount);auto placement=value.value("placement",Json::object());
    return {placement.value("x",0.),placement.value("y",0.),placement.value("z",0.),placement.value("yaw",0.)};
}
namespace {
// Links a prefab and its dependencies when they differ from what the document has. Linking alone changes no owned source.
void link_with(EditorDocument& document,AssetId prefab,const std::string& text,const AssemblySources& dependencies){
    AssemblySources changed;for(const auto& [id,source]:dependencies)if(!document.links().contains(id)||document.links().at(id)!=source)changed[id]=source;
    if(!document.links().contains(prefab)||document.links().at(prefab)!=text)changed[prefab]=text;if(!changed.empty())document.link(changed);}
}
std::string place_prefab(EditorDocument& document,AssetId prefab,const std::string& prefab_text,const PrefabPlacement& at,const AssemblySources& dependencies){
    auto definition=decode_prefab(prefab_text,dependencies);require(definition.id==prefab,"Prefab source identity");
    auto graph=root_graph(document);if(!graph.contains("mounts")||graph["mounts"].is_null())graph["mounts"]=Json::object();
    require(graph["mounts"].size()<128,"Assembly mount limit");auto mount=fresh_key();
    graph["mounts"][mount]={{"asset",prefab.text()},{"placement",placement_json(at)}};
    // Link first: the new mount resolves against the published definition.
    link_with(document,prefab,prefab_text,dependencies);
    commit_root(document,graph,"Place prefab");return mount;
}
void set_instance_placement(EditorDocument& document,const std::string& mount,const PrefabPlacement& at){auto graph=root_graph(document);mount_of(graph,mount)["placement"]=placement_json(at);commit_root(document,graph,"Move prefab instance");}
std::string duplicate_instance(EditorDocument& document,const std::string& mount,const PrefabPlacement& offset){
    auto graph=root_graph(document);auto copy=mount_of(graph,mount);copy.erase("retain");auto at=instance_placement(document,mount);
    copy["placement"]=placement_json({at.x+offset.x,at.y+offset.y,at.z+offset.z,at.yaw+offset.yaw});
    require(graph["mounts"].size()<128,"Assembly mount limit");auto fresh=fresh_key();graph["mounts"][fresh]=std::move(copy);commit_root(document,graph,"Duplicate prefab instance");return fresh;
}
void delete_instance(EditorDocument& document,const std::string& mount){auto graph=root_graph(document);mount_of(graph,mount);graph["mounts"].erase(mount);commit_root(document,graph,"Delete prefab instance");}
Json prefab_member(const EditorDocument& document,AssetId prefab,const std::string& member){auto entities=prefab_entities(prefab,document.links());require(entities.contains(member),"The prefab has no such member");return entities[member];}
Json instance_member(const EditorDocument& document,const std::string& mount,const std::string& member){
    auto graph=root_graph(document);auto& value=mount_of(graph,mount);auto entity=prefab_member(document,AssetId::parse(value.at("asset").get<std::string>()),member);
    for(const auto& patch:value.value("patches",Json::array())){if(patch.at("target")!=member)continue;if(component_patch(patch))apply_component_patch(entity,patch);else if(patch.value("op","")=="SetName")entity["name"]=patch.at("value");}
    return entity;
}
std::vector<PrefabOverride> instance_overrides(const EditorDocument& document,const std::string& mount){
    auto graph=root_graph(document);auto& value=mount_of(graph,mount);auto prefab=AssetId::parse(value.at("asset").get<std::string>());std::vector<PrefabOverride> result;
    for(const auto& patch:value.value("patches",Json::array())){if(!component_patch(patch))continue;
        PrefabOverride entry;entry.member=patch.at("target");entry.component=patch.at("component");auto source=prefab_member(document,prefab,entry.member);auto op=patch.at("op").get<std::string>();
        if(op=="SetComponentField"){entry.field=patch.at("field");entry.instance_value=patch.at("value");if(source.contains(entry.component)&&source[entry.component].is_object()&&source[entry.component].contains(entry.field))entry.prefab_value=source[entry.component][entry.field];}
        else if(op=="AddComponent"){entry.kind=PrefabOverride::AddedComponent;entry.instance_value=patch.at("value");}
        else{entry.kind=PrefabOverride::RemovedComponent;if(source.contains(entry.component))entry.prefab_value=source[entry.component];}
        result.push_back(std::move(entry));}
    return result;
}
void set_instance_field(EditorDocument& document,const std::string& mount,const std::string& member,const std::string& component,const std::string& field,const Json& value){
    require(world_object(component),"Only world-object component fields can be overridden on an instance");
    auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto& patches=patches_of(mounted);auto source=prefab_member(document,AssetId::parse(mounted.at("asset").get<std::string>()),member);
    // A component this instance added keeps its whole value in that one patch.
    for(auto& patch:patches)if(patch.value("op","")=="AddComponent"&&patch.at("target")==member&&patch.at("component")==component){require(patch["value"].contains(field),"The component has no such field");patch["value"][field]=value;commit_root(document,graph,"Override prefab field");return;}
    require(source.contains(component)&&source[component].is_object()&&source[component].contains(field),"The prefab member has no such component field");
    patches.erase(std::remove_if(patches.begin(),patches.end(),[&](const Json& patch){return patch.value("op","")=="SetComponentField"&&patch.at("target")==member&&patch.at("component")==component&&patch.at("field")==field;}),patches.end());
    if(source[component][field]!=value)patches.push_back({{"op","SetComponentField"},{"target",member},{"component",component},{"field",field},{"value",value}});
    commit_root(document,graph,"Override prefab field");
}
void add_instance_component(EditorDocument& document,const std::string& mount,const std::string& member,const std::string& component,const Json& defaults){
    require(world_object(component)&&defaults.is_object(),"Only world-object components can be added to an instance");
    auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto& patches=patches_of(mounted);auto source=prefab_member(document,AssetId::parse(mounted.at("asset").get<std::string>()),member);
    auto removed=std::find_if(patches.begin(),patches.end(),[&](const Json& patch){return patch.value("op","")=="RemoveComponent"&&patch.at("target")==member&&patch.at("component")==component;});
    if(removed!=patches.end())patches.erase(removed);
    else{require(!source.contains(component)||source[component].is_null(),"The member already has this component");patches.push_back({{"op","AddComponent"},{"target",member},{"component",component},{"value",defaults}});}
    commit_root(document,graph,"Add component to prefab instance");
}
void remove_instance_component(EditorDocument& document,const std::string& mount,const std::string& member,const std::string& component){
    require(world_object(component),"Only world-object components can be removed from an instance");
    auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto& patches=patches_of(mounted);auto source=prefab_member(document,AssetId::parse(mounted.at("asset").get<std::string>()),member);
    const bool added=std::any_of(patches.begin(),patches.end(),[&](const Json& patch){return patch.value("op","")=="AddComponent"&&patch.at("target")==member&&patch.at("component")==component;});
    patches.erase(std::remove_if(patches.begin(),patches.end(),[&](const Json& patch){return component_patch(patch)&&patch.at("target")==member&&patch.at("component")==component;}),patches.end());
    if(!added){require(source.contains(component),"The member has no such component");patches.push_back({{"op","RemoveComponent"},{"target",member},{"component",component}});}
    commit_root(document,graph,"Remove component from prefab instance");
}
void revert_overrides(EditorDocument& document,const std::string& mount,const std::vector<OverrideKey>& keys){
    require(!keys.empty(),"Select overrides to revert");auto graph=root_graph(document);auto& patches=patches_of(mount_of(graph,mount));const auto before=patches.size();
    patches.erase(std::remove_if(patches.begin(),patches.end(),[&](const Json& patch){return selected(patch,keys);}),patches.end());
    require(patches.size()!=before,"The selected overrides are no longer present");commit_root(document,graph,"Revert prefab overrides");
}
std::string prefab_with_overrides(const EditorDocument& document,const std::string& mount,const std::vector<OverrideKey>& keys,const std::string& prefab_text){
    require(!keys.empty(),"Select overrides to apply");auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto prefab=parse(prefab_text);
    require(prefab.at("asset")==mounted.at("asset"),"Apply targets the prefab this instance links");std::size_t applied{};
    const bool variant=prefab.contains("base")&&!prefab["base"].is_null();
    for(const auto& patch:mounted.value("patches",Json::array()))if(selected(patch,keys)){auto member=patch.at("target").get<std::string>();
        // A variant has no entities of its own: the override becomes one of its patches (replacing the same field's patch).
        if(variant){auto& patches=prefab["patches"];if(patches.is_null())patches=Json::array();patches.erase(std::remove_if(patches.begin(),patches.end(),[&](const Json& own){return own.value("op","")==patch.value("op","")&&own.at("target")==patch.at("target")&&own.value("component","")==patch.value("component","")&&own.value("field","")==patch.value("field","");}),patches.end());patches.push_back(patch);}
        else{require(prefab["entities"].contains(member),"The prefab no longer has this member");apply_component_patch(prefab["entities"][member],patch);}
        ++applied;}
    require(applied>0,"The selected overrides are no longer present");decode_prefab(prefab.dump(),document.links());return prefab.dump();
}
SwapPreview prepare_swap(EditorDocument& document,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_defaults,const AssemblySources& dependencies){
    SwapPreview preview;auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto current=AssetId::parse(mounted.at("asset").get<std::string>());
    auto sources=with(document,dependencies);sources[replacement]=replacement_text;
    auto definition=decode_prefab(replacement_text,sources);require(definition.id==replacement,"Replacement prefab identity");
    if(replacement==current){preview.reason="The instance already links this prefab";return preview;}
    auto old_entities=prefab_entities(current,document.links()),new_entities=prefab_entities(replacement,sources);
    if(old_entities.size()!=1||new_entities.size()!=1||parse(linked_text(document,current)).value("mounts",Json::object()).size()!=0||parse(replacement_text).value("mounts",Json::object()).size()!=0){preview.reason="Swap supports single-object prefabs; multi-object member mapping is later work";return preview;}
    auto old_entity=old_entities.begin().value();auto new_entity=new_entities.begin().value();
    if(contract(old_entity)!=contract(new_entity)){preview.reason="The replacement has a different gameplay contract (its world-object components differ); persistent state cannot carry over";return preview;}
    for(const auto& patch:mounted.value("patches",Json::array())){
        std::string label=patch.value("op","")+" "+patch.value("component",std::string{})+(patch.contains("field")?"."+patch.at("field").get<std::string>():std::string{});
        if(!component_patch(patch)){preview.carried.push_back(label);continue;}
        if(use_defaults){preview.discarded.push_back(label);continue;}
        auto component=patch.at("component").get<std::string>();auto op=patch.at("op").get<std::string>();
        bool fits=op=="SetComponentField"?new_entity.contains(component)&&new_entity[component].is_object()&&new_entity[component].contains(patch.at("field").get<std::string>()):op=="AddComponent"?!new_entity.contains(component):new_entity.contains(component);
        (fits?preview.carried:preview.discarded).push_back(label);}
    if(!preview.discarded.empty()&&!use_defaults){preview.reason="Some overrides do not fit the replacement; choose Use replacement defaults to discard them";return preview;}
    preview.compatible=true;return preview;
}
void commit_swap(EditorDocument& document,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_defaults,const AssemblySources& dependencies){
    auto preview=prepare_swap(document,mount,replacement,replacement_text,use_defaults,dependencies);require(preview.compatible,preview.reason);
    auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto current=AssetId::parse(mounted.at("asset").get<std::string>());
    auto sources=with(document,dependencies);sources[replacement]=replacement_text;
    const auto old_member=member_keys(prefab_entities(current,document.links()))[0],new_member=member_keys(prefab_entities(replacement,sources))[0];
    // The gameplay member keeps the authored path its identity already derives from.
    auto retained=mounted.value("retain",Json::object());const std::string path=retained.contains(old_member)?retained[old_member].get<std::string>():document.placement().text()+"/"+mount+"/"+old_member;
    Json patches=Json::array();for(auto patch:mounted.value("patches",Json::array())){if(use_defaults&&component_patch(patch))continue;if(patch.at("target")==old_member)patch["target"]=new_member;patches.push_back(std::move(patch));}
    mounted["asset"]=replacement.text();mounted["patches"]=std::move(patches);mounted.erase("retain");
    if(path!=document.placement().text()+"/"+mount+"/"+new_member)mounted["retain"]={{new_member,path}};
    link_with(document,replacement,replacement_text,dependencies);
    commit_root(document,graph,"Swap prefab");
}
namespace {
// The scene's own entity behind an object, by its local key: refused for objects that belong to mounts.
std::string own_local(EditorDocument& document,StableId object){
    const auto prefix=document.placement().text()+"/";
    for(const auto& origin:document.plan().origins)if(origin.object==object){require(origin.definition==document.root()&&origin.local_path.starts_with(prefix)&&origin.local_path.find('/',prefix.size())==origin.local_path.npos,"Select the scene's own objects, not members of an instance");return origin.local_path.substr(prefix.size());}
    throw std::runtime_error("The selected object is no longer in the scene");
}
// The scene's own retained identity of a local key (R4 Unpack), or its derived path.
std::string own_identity_path(const EditorDocument& document,const Json& graph,const std::string& local){
    if(graph.contains("retain")&&graph["retain"].is_object()&&graph["retain"].contains(local))return graph["retain"][local].get<std::string>();return document.placement().text()+"/"+local;}
}
std::string prefab_from_objects(EditorDocument& document,const std::vector<StableId>& objects,AssetId prefab,const std::string& name){
    require(!objects.empty()&&objects.size()<=prefab_member_limit,"Capture one to 64 of the scene's own objects");
    std::vector<std::string> locals;for(auto object:objects){auto local=own_local(document,object);require(std::find(locals.begin(),locals.end(),local)==locals.end(),"An object is captured twice");locals.push_back(local);}
    const std::set<std::string> captured(locals.begin(),locals.end());auto graph=root_graph(document);
    const auto pivot_pose=pose_of(graph.at("entities").at(locals[0]));const PrefabPlacement pivot{pivot_pose.x,pivot_pose.y,pivot_pose.z,pivot_pose.yaw};
    Json value={{"schema",1},{"kind","prefab"},{"asset",prefab.text()},{"name",name},{"entities",Json::object()}};
    for(const auto& local:locals){auto entity=graph.at("entities").at(local);
        // References leaving the capture would address the scene around the prefab, which a prefab cannot do.
        each_reference(entity,[&](const Json& ref){require(ref.is_string()||ref.contains("local"),"A captured object references a member of an instance; prefabs of such objects are not supported");
            const auto target=ref.is_string()?ref.get<std::string>():ref["local"].get<std::string>();require(captured.contains(target),"A captured object references a scene object outside the capture; capture it too or clear the reference");});
        for(const auto& [other,unused]:graph.at("entities").items())if(!captured.contains(other))require(!names_elsewhere(entity,other),"A captured object refers to another scene object in a way prefabs cannot hold");
        set_pose(entity,unplaced(pivot,pose_of(entity)));value["entities"][local]=std::move(entity);}
    // Scene objects outside the capture that reference a captured one address it through an export.
    for(const auto& [other,entity]:graph.at("entities").items()){if(captured.contains(other))continue;
        for(const auto& local:locals){require(!names_elsewhere(entity,local),"Another scene object refers to a captured object in a way that cannot cross a prefab boundary (only targets and script references can)");
            Json probe=entity;bool referenced=false;each_reference(probe,[&](const Json& ref){if(refers_local(ref,local))referenced=true;});
            if(referenced){if(!value.contains("exports"))value["exports"]=Json::object();bool exported=false;for(const auto& [key,target]:value["exports"].items())if(target==local)exported=true;if(!exported)value["exports"][fresh_key()]=local;}}}
    const auto exports=graph.value("exports",Json::object());for(const auto& [key,target]:exports.items())if(target.is_string()&&captured.contains(target.get<std::string>())){if(!value.contains("exports"))value["exports"]=Json::object();bool exported=false;for(const auto& [own,local]:value["exports"].items())if(local==target)exported=true;if(!exported)value["exports"][fresh_key()]=target;}
    decode_prefab(value.dump());return value.dump();
}
std::string prefab_from_object(EditorDocument& document,StableId object,AssetId prefab,const std::string& name){return prefab_from_objects(document,{object},prefab,name);}
std::string replace_with_instance(EditorDocument& document,const std::vector<StableId>& objects,AssetId prefab,const std::string& prefab_text){
    std::vector<std::string> locals;for(auto object:objects)locals.push_back(own_local(document,object));
    auto definition=decode_prefab(prefab_text);auto sorted=locals;std::sort(sorted.begin(),sorted.end());require(definition.id==prefab&&definition.members==sorted,"The prefab was not made from these objects");
    auto graph=root_graph(document);const auto pivot_pose=pose_of(graph.at("entities").at(locals[0]));const PrefabPlacement at{pivot_pose.x,pivot_pose.y,pivot_pose.z,pivot_pose.yaw};
    if(!graph.contains("mounts")||graph["mounts"].is_null())graph["mounts"]=Json::object();require(graph["mounts"].size()<128,"Assembly mount limit");
    auto mount=fresh_key();Json retain=Json::object();for(const auto& local:locals){retain[local]=own_identity_path(document,graph,local);graph["entities"].erase(local);if(graph.contains("retain")&&graph["retain"].is_object())graph["retain"].erase(local);}
    graph["mounts"][mount]={{"asset",prefab.text()},{"placement",placement_json(at)},{"retain",retain}};
    // References into the capture now go through the prefab's exports.
    const auto exports=parse(prefab_text).value("exports",Json::object());auto export_of=[&](const std::string& local)->std::optional<std::string>{for(const auto& [key,target]:exports.items())if(target==local)return key;return std::nullopt;};
    for(auto& [other,entity]:graph["entities"].items())each_reference(entity,[&](Json& ref){for(const auto& local:locals)if(refers_local(ref,local)){auto key=export_of(local);require(key.has_value(),"A reference into the capture has no export");ref={{"mount",mount},{"export",*key}};return;}});
    if(graph.contains("exports")&&graph["exports"].is_object())for(auto& [key,target]:graph["exports"].items())for(const auto& local:locals)if(target==local)target=mount+"/"+local;
    link_with(document,prefab,prefab_text,{});
    commit_root(document,graph,"Replace objects with prefab instance");return mount;
}
std::string replace_with_instance(EditorDocument& document,StableId object,AssetId prefab,const std::string& prefab_text){return replace_with_instance(document,std::vector<StableId>{object},prefab,prefab_text);}
std::map<std::string,std::string> unpack_instance(EditorDocument& document,const std::string& mount){
    auto graph=root_graph(document);auto mounted=mount_of(graph,mount);const auto prefab=AssetId::parse(mounted.at("asset").get<std::string>());
    require(parse(linked_text(document,prefab)).value("mounts",Json::object()).empty(),"This prefab mounts other prefabs; unpacking nested prefabs is not supported");
    // The instance as Play resolves it: definition, then every instance patch, then the placement.
    Json resolved={{"entities",prefab_entities(prefab,document.links())}};for(const auto& patch:mounted.value("patches",Json::array()))apply_patch(resolved,patch);
    const auto at=instance_placement(document,mount);const auto retained=mounted.value("retain",Json::object());
    std::map<std::string,std::string> keys;for(const auto& [member,unused]:resolved["entities"].items())keys[member]=graph.at("entities").contains(member)?fresh_key():member;
    if(!graph.contains("retain")||graph["retain"].is_null())graph["retain"]=Json::object();
    for(auto [member,entity]:resolved["entities"].items()){set_pose(entity,placed(at,pose_of(entity)));
        // Members refer to each other by local key; the keys may have changed.
        each_reference(entity,[&](Json& ref){const auto target=ref.is_string()?ref.get<std::string>():ref.value("local",std::string{});if(keys.contains(target))ref=keys.at(target);});
        graph["entities"][keys.at(member)]=entity;
        graph["retain"][keys.at(member)]=retained.contains(member)?retained[member].get<std::string>():document.placement().text()+"/"+mount+"/"+member;}
    // References into the instance (through its exports) and scene exports of its members now address the new objects.
    const auto exports=parse(linked_text(document,prefab)).value("exports",Json::object());
    for(auto& [other,entity]:graph["entities"].items())each_reference(entity,[&](Json& ref){if(ref.is_object()&&ref.value("mount","")==mount){auto key=ref.at("export").get<std::string>();require(exports.contains(key),"A reference names an export the prefab does not have");ref=keys.at(exports[key].get<std::string>());}});
    if(graph.contains("exports")&&graph["exports"].is_object())for(auto& [key,target]:graph["exports"].items())if(target.is_string()&&target.get<std::string>().starts_with(mount+"/")){auto member=target.get<std::string>().substr(mount.size()+1);require(keys.contains(member),"A scene export names a removed member");target=keys.at(member);}
    graph["mounts"].erase(mount);commit_root(document,graph,"Unpack prefab instance");return keys;
}
std::string prefab_fingerprint(const std::string& base_text){return sha256(parse(base_text).dump());}
Json make_variant(AssetId variant,const std::string& name,AssetId base,const std::string& base_text){
    auto value=parse(base_text);require(value.value("kind","")=="prefab"&&(!value.contains("base")||value["base"].is_null()),"A variant derives from a prefab that is not itself a variant");
    return {{"schema",1},{"kind","prefab"},{"asset",variant.text()},{"name",name},{"base",base.text()},{"base_fingerprint",prefab_fingerprint(base_text)},{"patches",Json::array()},{"entities",Json::object()}};
}
std::vector<std::string> variant_conflicts(const Json& variant,const std::string& base_text){
    std::vector<std::string> conflicts;Json resolved={{"entities",parse(base_text).value("entities",Json::object())}};
    for(const auto& patch:variant.value("patches",Json::array())){try{apply_patch(resolved,patch);}catch(const std::exception& error){conflicts.push_back(patch.value("op","")+" "+patch.value("target","")+" "+patch.value("component","")+(patch.contains("field")?"."+patch["field"].get<std::string>():"")+": "+error.what());}}
    return conflicts;
}
Json rebase_variant(const Json& variant,const std::string& base_text){
    auto conflicts=variant_conflicts(variant,base_text);if(!conflicts.empty())throw std::runtime_error("Rebase refused; repair or remove these patches first: "+conflicts.front()+(conflicts.size()>1?" (and "+std::to_string(conflicts.size()-1)+" more)":""));
    auto result=variant;result["base_fingerprint"]=prefab_fingerprint(base_text);return result;
}
void set_variant_field(Json& variant,const std::string& base_text,const std::string& member,const std::string& component,const std::string& field,const Json& value){
    require(world_object(component),"Only world-object component fields can be patched by a variant");
    auto base=parse(base_text).value("entities",Json::object());require(base.contains(member),"The base prefab has no such member");auto& patches=variant["patches"];if(patches.is_null())patches=Json::array();
    for(auto& patch:patches)if(patch.value("op","")=="AddComponent"&&patch.at("target")==member&&patch.at("component")==component){patch["value"][field]=value;return;}
    require(base[member].contains(component)&&base[member][component].contains(field),"The base member has no such component field");
    patches.erase(std::remove_if(patches.begin(),patches.end(),[&](const Json& patch){return patch.value("op","")=="SetComponentField"&&patch.at("target")==member&&patch.at("component")==component&&patch.at("field")==field;}),patches.end());
    if(base[member][component][field]!=value)patches.push_back({{"op","SetComponentField"},{"target",member},{"component",component},{"field",field},{"value",value}});
}
std::vector<PrefabConsumer> prefab_consumers(const std::filesystem::path& source_root,AssetId prefab){
    std::vector<PrefabConsumer> result;const auto id=prefab.text();std::error_code failed;
    for(std::filesystem::recursive_directory_iterator file(source_root,failed),end;!failed&&file!=end;file.increment(failed)){
        if(!file->is_regular_file())continue;const auto extension=file->path().extension();if(extension!=".dascene"&&extension!=".daprefab")continue;
        try{std::ifstream input(file->path(),std::ios::binary);auto value=Json::parse(std::string(std::istreambuf_iterator<char>(input),{}));const auto relative=std::filesystem::relative(file->path(),source_root).generic_string();
            if(extension==".dascene"){for(const auto& link:value.value("links",Json::array()))if(link==id){result.push_back({relative,"scene"});break;}}
            else if(value.value("asset","")!=id){if(value.contains("base")&&value["base"]==id){result.push_back({relative,"variant"});continue;}const auto mounts=value.value("mounts",Json::object());for(const auto& [key,mount]:mounts.items())if(mount.value("asset","")==id){result.push_back({relative,"prefab"});break;}}}
        catch(const std::exception&){}}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.source<b.source;});return result;
}
}
