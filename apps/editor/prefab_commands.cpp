#include "prefab_commands.hpp"
#include <darkangel/prefab_assets.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace darkangel::editor_app {
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
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
bool selected(const Json& patch,const std::vector<OverrideKey>& keys){
    if(!component_patch(patch))return false;
    for(const auto& key:keys)if(patch.at("target")==key.member&&patch.at("component")==key.component&&(key.field.empty()||patch.value("field","")==key.field))return true;return false;}
std::set<std::string> contract(const Json& entity){std::set<std::string> result;for(const auto& [key,value]:entity.items())if(world_object(key)&&!value.is_null())result.insert(key);return result;}
std::vector<std::string> members(const std::string& prefab_text){std::vector<std::string> result;const auto value=parse(prefab_text);for(const auto& [local,entity]:value.at("entities").items())result.push_back(local);return result;}
}
std::optional<PrefabInstanceRef> prefab_instance_of(EditorDocument& document,StableId object){
    const auto prefix=document.placement().text()+"/";
    for(const auto& origin:document.plan().origins)if(origin.object==object){
        if(!origin.local_path.starts_with(prefix)||!document.links().contains(origin.definition))return std::nullopt;
        auto rest=origin.local_path.substr(prefix.size());auto slash=rest.find('/');if(slash==rest.npos||rest.find('/',slash+1)!=rest.npos)return std::nullopt;
        auto graph=root_graph(document);auto mount=rest.substr(0,slash);
        if(!graph.contains("mounts")||!graph["mounts"].contains(mount)||graph["mounts"][mount].value("asset","")!=origin.definition.text())return std::nullopt;
        return PrefabInstanceRef{mount,rest.substr(slash+1),origin.definition};}
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
std::string place_prefab(EditorDocument& document,AssetId prefab,const std::string& prefab_text,const PrefabPlacement& at){
    auto definition=decode_prefab(prefab_text);require(definition.id==prefab,"Prefab source identity");
    auto graph=root_graph(document);if(!graph.contains("mounts")||graph["mounts"].is_null())graph["mounts"]=Json::object();
    require(graph["mounts"].size()<128,"Assembly mount limit");auto mount=fresh_key();
    graph["mounts"][mount]={{"asset",prefab.text()},{"placement",placement_json(at)}};
    // Link first: the new mount resolves against the published definition. Linking alone changes no owned source.
    if(!document.links().contains(prefab)||document.links().at(prefab)!=prefab_text)document.link({{prefab,prefab_text}});
    commit_root(document,graph,"Place prefab");return mount;
}
void set_instance_placement(EditorDocument& document,const std::string& mount,const PrefabPlacement& at){auto graph=root_graph(document);mount_of(graph,mount)["placement"]=placement_json(at);commit_root(document,graph,"Move prefab instance");}
std::string duplicate_instance(EditorDocument& document,const std::string& mount,const PrefabPlacement& offset){
    auto graph=root_graph(document);auto copy=mount_of(graph,mount);copy.erase("retain");auto at=instance_placement(document,mount);
    copy["placement"]=placement_json({at.x+offset.x,at.y+offset.y,at.z+offset.z,at.yaw+offset.yaw});
    require(graph["mounts"].size()<128,"Assembly mount limit");auto fresh=fresh_key();graph["mounts"][fresh]=std::move(copy);commit_root(document,graph,"Duplicate prefab instance");return fresh;
}
void delete_instance(EditorDocument& document,const std::string& mount){auto graph=root_graph(document);mount_of(graph,mount);graph["mounts"].erase(mount);commit_root(document,graph,"Delete prefab instance");}
Json prefab_member(const EditorDocument& document,AssetId prefab,const std::string& member){auto value=parse(linked_text(document,prefab));require(value.at("entities").contains(member),"The prefab has no such member");return value["entities"][member];}
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
    for(const auto& patch:mounted.value("patches",Json::array()))if(selected(patch,keys)){auto member=patch.at("target").get<std::string>();require(prefab["entities"].contains(member),"The prefab no longer has this member");apply_component_patch(prefab["entities"][member],patch);++applied;}
    require(applied>0,"The selected overrides are no longer present");decode_prefab(prefab.dump());return prefab.dump();
}
SwapPreview prepare_swap(EditorDocument& document,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_defaults){
    SwapPreview preview;auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto current=AssetId::parse(mounted.at("asset").get<std::string>());
    auto definition=decode_prefab(replacement_text);require(definition.id==replacement,"Replacement prefab identity");
    if(replacement==current){preview.reason="The instance already links this prefab";return preview;}
    auto old_members=members(linked_text(document,current)),new_members=definition.members;
    if(old_members.size()!=1||new_members.size()!=1){preview.reason="Swap supports single-object prefabs; multi-object member mapping is later work";return preview;}
    auto old_entity=prefab_member(document,current,old_members[0]);auto new_entity=parse(replacement_text)["entities"][new_members[0]];
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
void commit_swap(EditorDocument& document,const std::string& mount,AssetId replacement,const std::string& replacement_text,bool use_defaults){
    auto preview=prepare_swap(document,mount,replacement,replacement_text,use_defaults);require(preview.compatible,preview.reason.c_str());
    auto graph=root_graph(document);auto& mounted=mount_of(graph,mount);auto current=AssetId::parse(mounted.at("asset").get<std::string>());
    const auto old_member=members(linked_text(document,current))[0],new_member=decode_prefab(replacement_text).members[0];
    // The gameplay member keeps the authored path its identity already derives from.
    auto retained=mounted.value("retain",Json::object());const std::string path=retained.contains(old_member)?retained[old_member].get<std::string>():document.placement().text()+"/"+mount+"/"+old_member;
    Json patches=Json::array();for(auto patch:mounted.value("patches",Json::array())){if(use_defaults&&component_patch(patch))continue;if(patch.at("target")==old_member)patch["target"]=new_member;patches.push_back(std::move(patch));}
    mounted["asset"]=replacement.text();mounted["patches"]=std::move(patches);mounted.erase("retain");
    if(path!=document.placement().text()+"/"+mount+"/"+new_member)mounted["retain"]={{new_member,path}};
    if(!document.links().contains(replacement)||document.links().at(replacement)!=replacement_text)document.link({{replacement,replacement_text}});
    commit_root(document,graph,"Swap prefab");
}
namespace {
// The scene's own entity behind an object, by its local key: refused for objects that belong to mounts.
std::string own_local(EditorDocument& document,StableId object){
    const auto prefix=document.placement().text()+"/";
    for(const auto& origin:document.plan().origins)if(origin.object==object){require(origin.definition==document.root()&&origin.local_path.starts_with(prefix)&&origin.local_path.find('/',prefix.size())==origin.local_path.npos,"Select one of the scene's own objects, not a member of an instance");return origin.local_path.substr(prefix.size());}
    throw std::runtime_error("The selected object is no longer in the scene");
}
bool names(const Json& value,const std::string& local){if(value.is_string())return value==local;if(value.is_object()){if(value.contains("local")&&value["local"]==local)return true;for(const auto& [key,item]:value.items())if(names(item,local))return true;}if(value.is_array())for(const auto& item:value)if(names(item,local))return true;return false;}
}
std::string prefab_from_object(EditorDocument& document,StableId object,AssetId prefab,const std::string& name){
    auto local=own_local(document,object);auto graph=root_graph(document);auto entity=graph.at("entities").at(local);
    // References across the new mount boundary would need exports and a reference rewrite; the first version refuses them.
    if(entity.contains("target")&&!entity["target"].is_null())throw std::runtime_error("The object targets another scene object; clear its target before making a prefab");
    const auto scripts=entity.value("scripts",Json::object());for(const auto& [attachment,script]:scripts.items())if(script.contains("entity_refs")&&!script["entity_refs"].empty())throw std::runtime_error("A script on the object references other scene objects");
    for(const auto& [other,value]:graph.at("entities").items())if(other!=local){Json probe=value;probe.erase("types");probe.erase("name");probe.erase("model");if(names(probe,local))throw std::runtime_error("Other scene objects reference this object; prefabs of referenced objects are not supported yet");}
    const auto exports=graph.value("exports",Json::object());for(const auto& [key,value]:exports.items())if(value==local)throw std::runtime_error("The scene exports this object");
    auto& fields=entity.at("types").at("1").at("fields");for(const char* key:{"1","2","3","4"})fields[key]=0.0;
    Json value={{"schema",1},{"kind","prefab"},{"asset",prefab.text()},{"name",name},{"entities",{{local,entity}}}};decode_prefab(value.dump());return value.dump();
}
std::string replace_with_instance(EditorDocument& document,StableId object,AssetId prefab,const std::string& prefab_text){
    auto local=own_local(document,object);auto definition=decode_prefab(prefab_text);require(definition.id==prefab&&definition.members.size()==1&&definition.members[0]==local,"The prefab was not made from this object");
    auto graph=root_graph(document);const auto& fields=graph.at("entities").at(local).at("types").at("1").at("fields");
    PrefabPlacement at{fields.at("2").get<double>(),fields.at("3").get<double>(),fields.at("4").get<double>(),fields.at("1").get<double>()};
    graph["entities"].erase(local);if(!graph.contains("mounts")||graph["mounts"].is_null())graph["mounts"]=Json::object();require(graph["mounts"].size()<128,"Assembly mount limit");
    auto mount=fresh_key();graph["mounts"][mount]={{"asset",prefab.text()},{"placement",placement_json(at)},{"retain",{{local,document.placement().text()+"/"+local}}}};
    if(!document.links().contains(prefab)||document.links().at(prefab)!=prefab_text)document.link({{prefab,prefab_text}});
    commit_root(document,graph,"Replace object with prefab instance");return mount;
}
}
