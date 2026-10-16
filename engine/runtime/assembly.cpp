#include <darkangel/encounter.hpp>
#include <darkangel/interaction.hpp>
#include <darkangel/scene_travel.hpp>
#include <darkangel/spline.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/world_objects.hpp>
#include <darkangel/hash.hpp>
#include "script_state.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>
#include <stdexcept>
namespace darkangel {
// An assembly source is bounded like the editor document that holds it.
constexpr std::size_t document_source_limit=scene_document_byte_limit;
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
StableId persistent(std::string_view path){return StableId::parse(sha256(path).substr(0,32));}
void migrate(Json& entity){auto& transform=entity.at("types").at("1");if(transform.at("version")==1){require(transform.at("fields").size()==1 && transform.at("fields").contains("1"),"Malformed legacy transform");Transform defaults;for(const auto& field:metadata()[0].properties)if(field.id!=1){double value;std::memcpy(&value,reinterpret_cast<const char*>(&defaults)+field.offset,sizeof(value));transform["fields"][std::to_string(field.id)]=value;}transform["version"]=2;}}
// Gameplay components an instance may add, remove or override field by field. Structural roles (spawn points, encounters,
// volumes, splines, entries, transitions, audio and collision profiles) stay definition-only.
bool patchable_component(const std::string& component){return component=="altar"||component=="mover"||component=="pickup"||component=="interactable"||component=="trigger";}
// A mount's placement (R4): position and yaw of the mounted assembly in its parent's space, scale one. Identity when absent,
// so existing mounts resolve exactly as before.
struct Placement {double x{},y{},z{},yaw{};bool identity()const{return x==0&&y==0&&z==0&&yaw==0;}
    Placement then(const Placement& child)const{const double c=std::cos(yaw),s=std::sin(yaw);return {x+c*child.x+s*child.z,y+child.y,z-s*child.x+c*child.z,yaw+child.yaw};}};
Placement mount_placement(const Json& mount){
    if(!mount.contains("placement")||mount["placement"].is_null())return {};const auto& value=mount["placement"];
    require(value.is_object()&&value.size()<=4,"Mount placement is a record of x, y, z and yaw");Placement result;
    auto number=[&](const char* key,double& out,double bound){if(!value.contains(key))return;require(value[key].is_number(),"Mount placement values are numbers");out=value[key].get<double>();require(std::isfinite(out)&&std::abs(out)<=bound,"Mount placement is out of bounds");};
    for(const auto& [key,unused]:value.items())require(key=="x"||key=="y"||key=="z"||key=="yaw","Mount placement supports x, y, z and yaw only (scale one)");
    number("x",result.x,100000);number("y",result.y,100000);number("z",result.z,100000);number("yaw",result.yaw,6.2831853071795864*4);return result;}
// Applies a placement to an entity's transform (type 1, version 2: 1 yaw, 2 x, 3 y, 4 z). Rotation matches the engine's yaw
// convention (local +Z faces (sin yaw, cos yaw)); mover offsets and altar arrivals are object-local and turn with the object.
void place(Json& entity,const Placement& placement){
    if(placement.identity())return;auto& fields=entity.at("types").at("1").at("fields");
    const double x=fields.at("2").get<double>(),z=fields.at("4").get<double>(),c=std::cos(placement.yaw),s=std::sin(placement.yaw);
    fields["1"]=fields.at("1").get<double>()+placement.yaw;fields["2"]=placement.x+c*x+s*z;fields["3"]=placement.y+fields.at("3").get<double>();fields["4"]=placement.z-s*x+c*z;}
// A mount's retained identities (R4 conversion and Swap): member local key -> the authored path whose persistent identity the
// member keeps, so a converted or swapped object keeps its world-ledger state. Scene-owned; bounded.
std::map<std::string,std::string> mount_retained(const Json& mount){
    std::map<std::string,std::string> result;if(!mount.contains("retain")||mount["retain"].is_null())return result;const auto& value=mount["retain"];
    require(value.is_object()&&value.size()<=64,"Mount retained identity map");for(const auto& [local,path]:value.items()){StableId::parse(local);require(path.is_string()&&!path.get<std::string>().empty()&&path.get<std::string>().size()<=1024,"Retained identity is an authored path");result.emplace(local,path.get<std::string>());}return result;}
void patch(Json& graph,const Json& operation){
    auto op=operation.at("op").get<std::string>();auto target=operation.at("target").get<std::string>();StableId::parse(target);
    auto& entities=graph.at("entities");
    if(op=="AddEntity"){require(!entities.contains(target),"AddEntity target exists");entities[target]=operation.at("value");return;}
    require(entities.contains(target),"Patch target removed or unresolved; preserve source for explicit repair");
    if(op=="RemoveEntity"){entities.erase(target);return;}
    if(op=="SetProperty"){
        auto type=std::to_string(operation.at("type").get<TypeId>()),property=std::to_string(operation.at("property").get<PropertyId>());
        auto& fields=entities[target].at("types").at(type).at("fields");require(fields.contains(property),"Patch property removed/unknown");fields[property]=operation.at("value");
    }else if(op=="ReplaceSlot")entities[target]["model"]=operation.at("asset");
    else if(op=="SetSpawn")entities[target]["spawn"]=operation.at("value");
    else if(op=="BindScript")entities[target]["scripts"][operation.at("binding").get<std::string>()]=operation.at("value");
    else if(op=="RemoveScript"){auto& scripts=entities[target].at("scripts");require(scripts.erase(operation.at("binding").get<std::string>())==1,"Missing script binding patch target");}
    // Component operations (R4 instance overrides): a registered gameplay component of the target entity and one of its
    // top-level fields. The entity's decoders validate the result; a removed component or field is an unresolved target.
    else if(op=="SetComponentField"){auto component=operation.at("component").get<std::string>(),field=operation.at("field").get<std::string>();require(patchable_component(component),"Patch component is not overridable");
        auto& entity=entities[target];require(entity.contains(component)&&entity[component].is_object()&&entity[component].contains(field),"Patch component field removed or unresolved; preserve source for explicit repair");entity[component][field]=operation.at("value");}
    else if(op=="AddComponent"){auto component=operation.at("component").get<std::string>();require(patchable_component(component),"Patch component is not overridable");auto& entity=entities[target];require(!entity.contains(component)||entity[component].is_null(),"AddComponent target already has the component");require(operation.at("value").is_object(),"A component is a record");entity[component]=operation.at("value");}
    else if(op=="RemoveComponent"){auto component=operation.at("component").get<std::string>();require(patchable_component(component),"Patch component is not overridable");require(entities[target].erase(component)==1,"RemoveComponent target has no such component");}
    else if(op=="SetName"){require(operation.at("value").is_string()&&operation.at("value").get<std::string>().size()<=128,"Entity name patch");entities[target]["name"]=operation.at("value");}
    else throw std::runtime_error("Unsupported typed patch operation");
}
// An origin and a binding as SpawnPlan::serialize writes them.
Json origin_json(const ObjectOrigin&);
Json binding_json(const PlannedBinding&);
// One authored entity's own share of a plan (SCENE_SCALE_PLAN.md phase 8): its origin, script bindings and component entries in
// `entries` (a plan of this object alone), its native scene object, and the references it makes. A full resolve and a record
// edit both build every object through entity_part and resolve_part, so their plans are the same.
struct EntityPart {
    std::string local,scope,path;StableId object;SpawnPlan entries;Json native;
    Json target;                                     // the unresolved target reference, or null
    std::vector<std::pair<std::size_t,Json>> configs; // a binding (index in entries.bindings) and its unresolved entity_refs
    std::vector<std::string> references;             // the paths its references address, once resolved
    ObjectData data;std::string record;              // the resolved scene object, and its record as World::serialize writes it
    std::string origin;std::vector<std::string> bindings; // its origin and bindings as SpawnPlan::serialize writes them
};
EntityPart entity_part(AssetId id,const std::string& scope,const std::string& local,Json entity,const char* layer,const std::map<std::string,std::string>* retained=nullptr){
    StableId::parse(local);migrate(entity);EntityPart part;part.local=local;part.scope=scope;part.path=scope+"/"+local;
    // The authored path a member's identities derive from: its own, or the one its mount retains for it.
    auto identity_path=[&](const std::string& key){if(retained)if(auto found=retained->find(key);found!=retained->end())return found->second;return scope+"/"+key;};
    part.object=persistent(identity_path(local));const auto own_path=identity_path(local);
    const auto& path=part.path;const auto object=part.object;auto& plan=part.entries;
    part.native={{"id",object.text()},{"types",entity.at("types")},{"target",nullptr}};if(entity.contains("optional"))part.native["optional"]=entity["optional"];
    if(entity.contains("target") && !entity["target"].is_null())part.target=entity["target"];
    plan.origins.push_back({object,id,path,entity.value("name",local),layer});
    if(entity.contains("model") && !entity["model"].is_null())plan.models[object]=AssetId::parse(entity["model"].get<std::string>());
    // A spawn point names the NPC definition it spawns. respawn and encounter are reserved: only "never" and no owner are accepted yet.
    if(entity.contains("spawn") && !entity["spawn"].is_null()){const auto& spawn=entity["spawn"];require(spawn.is_object()&&spawn.contains("npc")&&!entity.contains("model"),"Spawn point needs an NPC definition and no model");for(const auto& [key,value]:spawn.items())require(key=="npc"||(key=="respawn"&&value=="never")||(key=="encounter"&&(value.is_null()||value.is_string())),"Unsupported spawn point field");plan.spawns[object]=AssetId::parse(spawn["npc"].get<std::string>());
        // A spawn point that names an encounter (by its local identity in the same source) is used by it and does not spawn at Play start.
        if(spawn.contains("encounter")&&spawn["encounter"].is_string()){StableId::parse(spawn["encounter"].get<std::string>());plan.spawn_owners[object]=persistent(identity_path(spawn["encounter"].get<std::string>()));}}
    // Gameplay volumes: an encounter, a box of an encounter's bounds, a trigger volume. None of them is a model or a spawn point.
    if(entity.contains("encounter") && !entity["encounter"].is_null()){require(!entity.contains("model")&&!entity.contains("spawn"),"An encounter is its own object");decode_encounter(entity["encounter"].dump());plan.encounters[object]=entity["encounter"].dump();}
    if(entity.contains("volume") && !entity["volume"].is_null()){const auto& volume=entity["volume"];require(volume.is_object()&&volume.size()==2&&volume.contains("encounter")&&volume["encounter"].is_string()&&volume.contains("half")&&volume["half"].is_array()&&volume["half"].size()==3&&!entity.contains("model")&&!entity.contains("spawn"),"An encounter volume names its encounter and three half extents");StableId::parse(volume["encounter"].get<std::string>());SpawnPlan::Volume made;made.encounter=persistent(identity_path(volume["encounter"].get<std::string>()));for(unsigned axis=0;axis<3;++axis){require(volume["half"][axis].is_number(),"Encounter volume half extent type");made.half[axis]=volume["half"][axis].get<double>();require(made.half[axis]>0&&made.half[axis]<=200,"Encounter volume half extents are above 0, up to 200 m");}plan.volumes[object]=made;}
    if(entity.contains("spline") && !entity["spline"].is_null()){require(!entity.contains("model")&&!entity.contains("spawn"),"A spline is its own object");decode_spline(entity["spline"].dump());plan.splines[object]=entity["spline"].dump();}
    if(entity.contains("spline_point") && !entity["spline_point"].is_null()){const auto& point=entity["spline_point"];require(point.is_object()&&point.size()==2&&point.contains("spline")&&point["spline"].is_string()&&point.contains("index")&&point["index"].is_number_unsigned()&&point["index"].get<std::uint64_t>()<4096&&!entity.contains("model")&&!entity.contains("spawn"),"A spline point names its spline and its place in the order");StableId::parse(point["spline"].get<std::string>());plan.spline_points[object]={persistent(identity_path(point["spline"].get<std::string>())),point["index"].get<unsigned>()};}
    if(entity.contains("audio") && !entity["audio"].is_null()){const auto& audio=entity["audio"];require(audio.is_object()&&audio.size()==1&&audio.contains("profile")&&audio["profile"].is_string()&&!entity.contains("model")&&!entity.contains("spawn"),"A scene audio object names one audio profile and nothing else");const auto profile=AssetId::parse(audio["profile"].get<std::string>());require(profile!=AssetId{},"Scene audio profile identity");plan.audio_profiles[object]=profile;}
    if(entity.contains("interactable") && !entity["interactable"].is_null()){require(!entity.contains("spawn"),"A spawn point is not an interactable");decode_interactable(entity["interactable"].dump());plan.interactables[object]=entity["interactable"].dump();}
    if(entity.contains("entry") && !entity["entry"].is_null()){require(!entity.contains("model")&&!entity.contains("spawn"),"A scene entry point is its own object");decode_entry(entity["entry"].dump());plan.entries[object]=entity["entry"].dump();}
    if(entity.contains("transition") && !entity["transition"].is_null()){require(!entity.contains("model")&&!entity.contains("spawn"),"A scene transition trigger is its own object");decode_transition(entity["transition"].dump());plan.transitions[object]=entity["transition"].dump();}
    if(entity.contains("pickup") && !entity["pickup"].is_null()){require(!entity.contains("spawn"),"A spawn point is not a pickup");decode_pickup(entity["pickup"].dump());plan.pickups[object]=entity["pickup"].dump();}
    if(entity.contains("altar") && !entity["altar"].is_null()){require(!entity.contains("spawn"),"A spawn point is not an altar");decode_altar(entity["altar"].dump());plan.altars[object]=entity["altar"].dump();}
    if(entity.contains("mover") && !entity["mover"].is_null()){require(!entity.contains("spawn"),"A spawn point is not a door or platform");decode_mover(entity["mover"].dump());plan.movers[object]=entity["mover"].dump();}
    if(entity.contains("collision") && !entity["collision"].is_null()){const auto& chosen=entity["collision"];require(chosen.is_object()&&chosen.size()==1&&chosen.contains("profile")&&chosen["profile"].is_string()&&!entity.contains("model")&&!entity.contains("spawn"),"A scene collision object names one collision asset and nothing else");const auto profile=AssetId::parse(chosen["profile"].get<std::string>());require(profile!=AssetId{},"Scene collision profile identity");plan.collision_profiles[object]=profile;}
    if(entity.contains("trigger") && !entity["trigger"].is_null()){require(!entity.contains("model")&&!entity.contains("spawn"),"A trigger volume is its own object");decode_trigger(entity["trigger"].dump());plan.triggers[object]=entity["trigger"].dump();}
    auto scripts=entity.value("scripts",Json::object());for(const auto& [attachment,script]:scripts.items()){
        StableId::parse(attachment);auto asset=AssetId::parse(script.at("asset").get<std::string>());auto text=asset.text();text.erase(23,1);text.erase(18,1);text.erase(13,1);text.erase(8,1);
        auto config=script.value("config",Json::object());require(config.is_object(),"Binding config must be a record");
        plan.bindings.push_back({object,{persistent(own_path+"/binding/"+attachment),StableId::parse(text),config.dump(),script.value("enabled",true)}});
        if(script.contains("entity_refs"))part.configs.push_back({plan.bindings.size()-1,script["entity_refs"]});
    }
    return part;
}
// The authored path an EntityRef addresses from the scope it is written in: a local identity, or a mounted assembly's export.
std::string reference_path(const std::string& scope,const Json& ref,const std::map<std::string,std::string>& exports){
    if(ref.is_string())return scope+"/"+ref.get<std::string>();
    if(ref.contains("local"))return scope+"/"+ref.at("local").get<std::string>();
    auto mount=ref.at("mount").get<std::string>(),key=ref.at("export").get<std::string>();StableId::parse(mount);StableId::parse(key);auto name=scope+"/"+mount+":"+key;require(exports.contains(name),"Unresolved declared assembly export");return exports.at(name);
}
// Resolves a part's references (find: the object at a path, or null) and builds its scene object, validated as World::load does.
template<class Find> void resolve_part(EntityPart& part,const std::map<std::string,std::string>& exports,Find&& find){
    part.references.clear();
    auto address=[&](const Json& ref){auto path=reference_path(part.scope,ref,exports);const StableId* object=find(path);require(object!=nullptr,"Unresolved required assembly EntityRef");part.references.push_back(path);return *object;};
    if(!part.target.is_null())part.native["target"]=address(part.target).text();
    for(const auto& [binding,refs]:part.configs){auto& b=part.entries.bindings[binding].binding;auto config=parse_script_data(b.config_json);for(const auto& [field,target]:refs.items())config[field]=address(target).text();b.config_json=config.dump();}
    for(const auto& b:part.entries.bindings){auto& record=part.native["types"]["4"];record["version"]=1;record["bindings"][b.binding.id.text()]={{"asset",asset_id(b.binding.asset).text()},{"enabled",b.binding.enabled},{"config",parse_script_data(b.binding.config_json)}};}
    part.data=World::decode(part.native.dump());part.record=World::encode(part.data);
    part.origin=origin_json(part.entries.origins.front()).dump();part.bindings.clear();for(const auto& b:part.entries.bindings)part.bindings.push_back(binding_json(b).dump());
}
// Every plan entry keyed by object, as pairs of the same member of two plans.
template<class F> void each_entry(SpawnPlan& a,const SpawnPlan& b,F&& f){
    f(a.models,b.models);f(a.spawns,b.spawns);f(a.interactables,b.interactables);f(a.entries,b.entries);f(a.transitions,b.transitions);f(a.splines,b.splines);
    f(a.spline_points,b.spline_points);f(a.audio_profiles,b.audio_profiles);f(a.pickups,b.pickups);f(a.altars,b.altars);f(a.movers,b.movers);
    f(a.collision_profiles,b.collision_profiles);f(a.encounters,b.encounters);f(a.triggers,b.triggers);f(a.volumes,b.volumes);f(a.spawn_owners,b.spawn_owners);
}
// The scene's object and component limits. A scene resolves only while every count is within them.
void check_limits(const SpawnPlan& plan){
    require(plan.origins.size()<=scene_object_limit,"Assembly object limit");require(plan.spawns.size()<=64,"Scene spawn point limit");
    require(plan.encounters.size()<=16,"Scene encounter limit");require(plan.volumes.size()<=128,"Scene volume limit");
    require(plan.splines.size()<=spline_limit,"Scene spline limit");require(plan.spline_points.size()<=spline_limit*spline_point_limit,"Scene spline point limit");
    require(plan.audio_profiles.size()<=1,"A scene has one audio profile");require(plan.interactables.size()<=64,"Scene interactable limit");
    require(plan.entries.size()<=64,"Scene entry point limit");require(plan.transitions.size()<=16,"Scene transition limit");
    require(plan.pickups.size()<=64,"Scene pickup limit");require(plan.altars.size()<=16,"Scene altar limit");require(plan.movers.size()<=16,"Scene door and platform limit");
    require(plan.collision_profiles.size()<=1,"A scene has one collision profile");require(plan.triggers.size()<=64,"Scene trigger volume limit");
}
Json bounded_fields(const SpawnPlan&);
// A model entry of the plan's "models" record, as SpawnPlan::serialize writes it.
std::string model_text(StableId object,AssetId asset){return Json(object.text()).dump()+":"+Json(asset.text()).dump();}
// SpawnPlan::serialize assembled from text: the bounded fields, each object's cached origin, binding and model text (in plan
// order), and a scene that is a World serialization (canonical JSON). Keys in order, as nlohmann::json writes an object.
std::string serialize_plan(const SpawnPlan& plan,const std::vector<std::string>& origins,const std::vector<std::string>& bindings,const std::map<StableId,std::string>& models,const std::string& scene){
    auto join=[](const auto& texts,const char* open,const char* close){std::size_t bytes=2;for(const auto& text:texts)bytes+=text.size()+1;std::string joined;joined.reserve(bytes);joined+=open;bool first=true;for(const auto& text:texts){if(!first)joined+=",";first=false;joined+=text;}joined+=close;return joined;};
    std::vector<std::string_view> entries;entries.reserve(models.size());for(const auto& [object,text]:models)entries.push_back(text);
    std::map<std::string,std::string> fields;auto bounded=bounded_fields(plan);for(auto it=bounded.begin();it!=bounded.end();++it)fields[it.key()]=it.value().dump();
    fields["origins"]=join(origins,"[","]");fields["bindings"]=join(bindings,"[","]");fields["models"]=join(entries,"{","}");fields["scene"]=scene;
    std::size_t bytes=2;for(const auto& [key,value]:fields)bytes+=key.size()+value.size()+4;std::string text;text.reserve(bytes);text+="{";
    bool first=true;for(const auto& [key,value]:fields){if(!first)text+=",";first=false;text+=Json(key).dump();text+=":";text+=value;}
    text+="}";require(text.size()<=scene_document_byte_limit,"Spawn plan byte limit");return text;
}
struct Resolver {
    const AssemblySources& sources;SpawnPlan plan;
    std::set<AssetId> stack,variants;std::map<std::string,StableId> paths;std::map<std::string,std::string> exports;std::set<StableId> objects;
    std::vector<EntityPart> parts;std::size_t own{}; // every object in visit order; the first `own` are the root's own entities
    Json source(AssetId id){require(sources.contains(id),"Missing declared AssemblyAsset");auto graph=parse_script_data(sources.at(id),document_source_limit);require(graph.at("schema")==1 && graph.at("asset")==id.text() && graph.at("entities").is_object(),"Assembly schema/identity mismatch");for(auto& [local,entity]:graph["entities"].items())migrate(entity);return graph;}
    Json definition(AssetId id){auto graph=source(id);if(graph.contains("base") && !graph["base"].is_null()){variants.insert(id);
        auto base=source(AssetId::parse(graph["base"].get<std::string>()));require(!base.contains("base") || base["base"].is_null(),"Only one variant level is enabled");
        require(graph.at("entities").empty() && (!graph.contains("mounts") || graph["mounts"].empty()),"Variants use explicit typed patches");
        if(graph.contains("base_fingerprint"))require(graph["base_fingerprint"]==sha256(base.dump()),"Base fingerprint changed; explicit rebase required");
        for(const auto& operation:graph.value("patches",Json::array()))patch(base,operation);base["asset"]=id.text();return base;
    }return graph;}
    void visit(AssetId id,const std::string& scope,const Json& edits,unsigned depth,const Placement& placement={},const std::map<std::string,std::string>& retained={}){
        require(depth<=16 && stack.insert(id).second,"Cyclic/deep assembly composition");auto graph=definition(id);require(edits.is_array() && edits.size()<=1024,"Placement patch limit");
        for(const auto& operation:edits)patch(graph,operation);
        for(const auto& [local,unused]:retained)require(graph.at("entities").contains(local),"Retained identity names a member the mounted assembly does not have");
        for(auto [local,entity]:graph.at("entities").items()){
            require(plan.origins.size()<scene_object_limit,"Assembly object limit");Json placed=entity;migrate(placed);place(placed,placement);auto part=entity_part(id,scope,local,std::move(placed),edits.empty()?"definition":"placement patches",retained.empty()?nullptr:&retained);
            require(paths.emplace(part.path,part.object).second,"Duplicate authored path");require(objects.insert(part.object).second,"Persistent identity collision");
            plan.origins.push_back(part.entries.origins.front());each_entry(plan,part.entries,[](auto& into,const auto& from){into.insert(from.begin(),from.end());});check_limits(plan);
            parts.push_back(std::move(part));
        }
        if(depth==0)own=parts.size();
        auto export_records=graph.value("exports",Json::object());for(const auto& [export_id,local]:export_records.items()){StableId::parse(export_id);require(local.is_string(),"Export must address a stable local path");exports[scope+":"+export_id]=scope+"/"+local.get<std::string>();}
        auto mounts=graph.value("mounts",Json::object());require(mounts.is_object() && mounts.size()<=128,"Assembly mount limit");
        for(const auto& [mount_id,mount]:mounts.items()){StableId::parse(mount_id);visit(AssetId::parse(mount.at("asset").get<std::string>()),scope+"/"+mount_id,mount.value("patches",Json::array()),depth+1,placement.then(mount_placement(mount)),mount_retained(mount));}
        stack.erase(id);
    }
};
// The scene: every object's record in identity order, as World::serialize writes it.
std::string scene_text(const std::map<StableId,std::string>& records){
    std::size_t bytes=32;for(const auto& [object,record]:records)bytes+=record.size()+1;std::string text;text.reserve(bytes);text+="{\"objects\":[";
    bool first=true;for(const auto& [object,record]:records){if(!first)text+=",";first=false;text+=record;}text+="],\"version\":1}";
    require(text.size()<=scene_document_byte_limit,"Serialized scene exceeds limit");return text;
}
}
struct SceneAssembly::Staged {std::uint64_t generation;std::map<std::string,std::optional<EntityPart>> parts;std::string scene_json,fingerprint;};
struct SceneAssembly::Impl {
    AssetId root;std::string scope;bool editable{};SpawnPlan plan;std::uint64_t generation{};
    std::map<std::string,StableId> paths;std::map<std::string,std::string> exports;
    std::map<StableId,std::string> records;    // every object's scene record, in identity (scene) order
    std::map<std::string,unsigned> referenced; // how many references address each path
    std::map<std::string,EntityPart> parts;    // the root's own entities by local identity: the first origins, in this order
    // The plan's origins and bindings (in plan order) and models as SpawnPlan::serialize writes them, so a fingerprint is text.
    std::vector<std::string> origin_texts,binding_texts;std::map<StableId,std::string> model_texts;
    std::string serialized(const std::string& scene) const{return serialize_plan(plan,origin_texts,binding_texts,model_texts,scene);}
    Impl(AssetId id,StableId placement,const AssemblySources& sources):root(id),scope(placement.text()){
        require(static_cast<bool>(placement) && sources.size()<=128,"Invalid placement/source closure limit");Resolver resolver{sources};resolver.visit(root,scope,Json::array(),0);
        auto find=[&](const std::string& path)->const StableId*{auto found=resolver.paths.find(path);return found==resolver.paths.end()?nullptr:&found->second;};
        plan=std::move(resolver.plan);plan.bindings.clear();
        for(auto& part:resolver.parts){resolve_part(part,resolver.exports,find);plan.bindings.insert(plan.bindings.end(),part.entries.bindings.begin(),part.entries.bindings.end());
            origin_texts.push_back(part.origin);binding_texts.insert(binding_texts.end(),part.bindings.begin(),part.bindings.end());for(const auto& [object,asset]:part.entries.models)model_texts.emplace(object,model_text(object,asset));records.emplace(part.object,part.record);for(const auto& path:part.references)++referenced[path];}
        for(const auto& [key,path]:resolver.exports)require(resolver.paths.contains(path),"Export addresses removed entity");
        editable=!resolver.variants.contains(root);if(editable)for(std::size_t index=0;index<resolver.own;++index)parts.emplace(resolver.parts[index].local,std::move(resolver.parts[index]));
        paths=std::move(resolver.paths);exports=std::move(resolver.exports);plan.scene_json=scene_text(records);plan.fingerprint=sha256(serialized(plan.scene_json));
    }
    // Where a root entity's origin and bindings sit: its index among the root's entities, and its first binding.
    std::pair<std::size_t,std::size_t> place(std::map<std::string,EntityPart>::const_iterator at) const{std::size_t index=0,binding=0;for(auto it=parts.begin();it!=at;++it){++index;binding+=it->second.entries.bindings.size();}return {index,binding};}
    EntityPart remove(const std::string& local){
        auto it=parts.find(local);auto [index,binding]=place(it);auto& part=it->second;
        plan.origins.erase(plan.origins.begin()+index);plan.bindings.erase(plan.bindings.begin()+binding,plan.bindings.begin()+binding+part.entries.bindings.size());
        each_entry(plan,part.entries,[](auto& into,const auto& from){for(const auto& entry:from)into.erase(entry.first);});
        origin_texts.erase(origin_texts.begin()+index);binding_texts.erase(binding_texts.begin()+binding,binding_texts.begin()+binding+part.bindings.size());for(const auto& [object,asset]:part.entries.models)model_texts.erase(object);
        records.erase(part.object);paths.erase(part.path);for(const auto& path:part.references)if(--referenced[path]==0)referenced.erase(path);
        auto removed=std::move(part);parts.erase(it);return removed;
    }
    void insert(EntityPart part){
        auto it=parts.emplace(part.local,std::move(part)).first;auto [index,binding]=place(it);const auto& added=it->second;
        plan.origins.insert(plan.origins.begin()+index,added.entries.origins.front());plan.bindings.insert(plan.bindings.begin()+binding,added.entries.bindings.begin(),added.entries.bindings.end());
        each_entry(plan,added.entries,[](auto& into,const auto& from){into.insert(from.begin(),from.end());});
        origin_texts.insert(origin_texts.begin()+index,added.origin);binding_texts.insert(binding_texts.begin()+binding,added.bindings.begin(),added.bindings.end());for(const auto& [object,asset]:added.entries.models)model_texts.emplace(object,model_text(object,asset));
        records.emplace(added.object,added.record);paths.emplace(added.path,added.object);for(const auto& path:added.references)++referenced[path];
    }
};
SceneAssembly::SceneAssembly(AssetId root,StableId placement,const AssemblySources& sources):impl_(std::make_unique<Impl>(root,placement,sources)){}
SceneAssembly::~SceneAssembly()=default;
const SpawnPlan& SceneAssembly::plan() const{return impl_->plan;}
bool SceneAssembly::editable() const{return impl_->editable;}
std::shared_ptr<const SceneAssembly::Staged> SceneAssembly::stage(const Records& edits){
    auto& p=*impl_;require(p.editable,"A variant scene's entities are its base's: edit its source");require(!edits.empty() && edits.size()<=scene_object_limit,"Scene record edit limit");
    auto staged=std::make_shared<Staged>();staged->generation=p.generation;std::map<std::string,const StableId*> edited;
    for(const auto& [local,text]:edits){
        if(text){auto part=entity_part(p.root,p.scope,local,parse_script_data(*text,document_source_limit),"definition");auto& slot=staged->parts[local];slot=std::move(part);edited[slot->path]=&slot->object;}
        else{require(p.parts.contains(local),"Removed scene record is not in the scene");staged->parts[local]=std::nullopt;edited[p.scope+"/"+local]=nullptr;}
    }
    // The edited records' own paths resolve to their new objects (or nothing); every other path to the scene as it is.
    auto find=[&](const std::string& path)->const StableId*{if(auto it=edited.find(path);it!=edited.end())return it->second;auto found=p.paths.find(path);return found==p.paths.end()?nullptr:&found->second;};
    for(auto& [local,part]:staged->parts)if(part){resolve_part(*part,p.exports,find);if(!p.parts.contains(local))require(!p.records.contains(part->object),"Persistent identity collision");}
    // A removed entity must be addressed by nothing that stays: no other entity's reference and no export.
    for(const auto& [local,part]:staged->parts)if(!part){const auto path=p.scope+"/"+local;auto count=p.referenced.contains(path)?p.referenced.at(path):0u;
        for(const auto& [edited_local,edit]:staged->parts)if(auto old=p.parts.find(edited_local);old!=p.parts.end())for(const auto& reference:old->second.references)if(reference==path)--count;
        require(count==0,"Unresolved required assembly EntityRef");for(const auto& [key,target]:p.exports)require(target!=path,"Export addresses removed entity");}
    // Apply, measure, and put everything back: the limits and the plan's scene and fingerprint need the edited plan.
    std::vector<EntityPart> before;std::vector<std::string> inserted;
    auto restore=[&]{for(const auto& local:inserted)p.remove(local);for(auto& part:before)p.insert(std::move(part));};
    try{
        for(const auto& [local,part]:staged->parts)if(p.parts.contains(local))before.push_back(p.remove(local));
        for(const auto& [local,part]:staged->parts)if(part){p.insert(*part);inserted.push_back(local);}
        check_limits(p.plan);staged->scene_json=scene_text(p.records);staged->fingerprint=sha256(p.serialized(staged->scene_json));
    }catch(...){restore();throw;}
    restore();return staged;
}
std::vector<std::pair<StableId,std::optional<ObjectData>>> SceneAssembly::apply(const Staged& staged){
    auto& p=*impl_;require(staged.generation==p.generation,"Stale staged scene edit");std::vector<std::pair<StableId,std::optional<ObjectData>>> changes;
    for(const auto& [local,part]:staged.parts)if(p.parts.contains(local)){auto removed=p.remove(local);if(!part)changes.push_back({removed.object,std::nullopt});}
    for(const auto& [local,part]:staged.parts)if(part){p.insert(*part);changes.push_back({part->object,part->data});}
    p.plan.scene_json=staged.scene_json;p.plan.fingerprint=staged.fingerprint;++p.generation;return changes;
}
SpawnPlan resolve_assembly(AssetId root,StableId placement,const AssemblySources& sources){return SceneAssembly(root,placement,sources).plan();}
StableId encounter_actor(StableId encounter,unsigned index){require(bool(encounter),"Encounter enemy needs an encounter identity");return persistent("encounter/"+encounter.text()+"/enemy/"+std::to_string(index));}
StableId party_member(unsigned seat){require(seat>=1&&seat<=3,"A party has seats 1 to 3 beside the first");return persistent("party/member/"+std::to_string(seat));}
StableId scatter_instance(StableId spline,unsigned index){require(bool(spline),"A scattered instance needs a spline identity");return persistent("spline/"+spline.text()+"/scatter/"+std::to_string(index));}
StableId spawned_actor(StableId spawn_point){require(bool(spawn_point),"Spawned actor needs a spawn point identity");return persistent("spawn/"+spawn_point.text()+"/actor");}
namespace {
Json origin_json(const ObjectOrigin& origin){return {{"object",origin.object.text()},{"asset",origin.definition.text()},{"path",origin.local_path},{"name",origin.name},{"layer",origin.layer}};}
Json binding_json(const PlannedBinding& binding){return {{"object",binding.object.text()},{"id",binding.binding.id.text()},{"asset",asset_id(binding.binding.asset).text()},{"config",parse_script_data(binding.binding.config_json)},{"enabled",binding.binding.enabled}};}
// The plan's fields whose size is bounded by the component limits: everything but its origins, bindings, models and scene.
Json bounded_fields(const SpawnPlan& plan){Json root={{"schema",1}};
    // Written only when present so plans without spawn points keep their fingerprint.
    if(!plan.spawns.empty()){root["spawns"]=Json::object();for(const auto& [object,asset]:plan.spawns)root["spawns"][object.text()]=asset.text();}
    if(!plan.splines.empty()||!plan.spline_points.empty()){auto& curves=root["splines"]=Json::object();for(const auto& [object,text]:plan.splines)curves[object.text()]=Json::parse(text);auto& points=root["spline_points"]=Json::object();for(const auto& [object,point]:plan.spline_points)points[object.text()]={{"spline",point.spline.text()},{"index",point.index}};}
    // Written only when the scene has one, so plans without interactables serialize as before.
    if(!plan.audio_profiles.empty()){auto& listed=root["audio"]=Json::object();for(const auto& [object,profile]:plan.audio_profiles)listed[object.text()]=profile.text();}
    // Written only when the scene has either, so plans without party travel serialize as before.
    if(!plan.entries.empty()||!plan.transitions.empty()){auto& travel=root["travel"]={{"entries",Json::object()},{"transitions",Json::object()}};for(const auto& [object,text]:plan.entries)travel["entries"][object.text()]=Json::parse(text);for(const auto& [object,text]:plan.transitions)travel["transitions"][object.text()]=Json::parse(text);}
    if(!plan.interactables.empty()){auto& listed=root["interactables"]=Json::object();for(const auto& [object,text]:plan.interactables)listed[object.text()]=Json::parse(text);}
    // Written only when the scene has any, so older plans keep their fingerprint.
    if(!plan.pickups.empty()||!plan.altars.empty()||!plan.movers.empty()){auto& objects=root["world_objects"]={{"pickups",Json::object()},{"altars",Json::object()},{"movers",Json::object()}};for(const auto& [object,text]:plan.pickups)objects["pickups"][object.text()]=Json::parse(text);for(const auto& [object,text]:plan.altars)objects["altars"][object.text()]=Json::parse(text);for(const auto& [object,text]:plan.movers)objects["movers"][object.text()]=Json::parse(text);}
    if(!plan.collision_profiles.empty()){auto& listed=root["collision"]=Json::object();for(const auto& [object,profile]:plan.collision_profiles)listed[object.text()]=profile.text();}
    if(!plan.encounters.empty()||!plan.triggers.empty()||!plan.volumes.empty()||!plan.spawn_owners.empty()){auto& gameplay=root["gameplay"]={{"encounters",Json::object()},{"triggers",Json::object()},{"volumes",Json::object()},{"spawn_owners",Json::object()}};
        for(const auto& [object,text]:plan.encounters)gameplay["encounters"][object.text()]=Json::parse(text);for(const auto& [object,text]:plan.triggers)gameplay["triggers"][object.text()]=Json::parse(text);
        for(const auto& [object,volume]:plan.volumes)gameplay["volumes"][object.text()]={{"encounter",volume.encounter.text()},{"half",volume.half}};for(const auto& [object,owner]:plan.spawn_owners)gameplay["spawn_owners"][object.text()]=owner.text();}
    return root;}
Json plan_fields(const SpawnPlan& plan){auto root=bounded_fields(plan);root["origins"]=Json::array();root["bindings"]=Json::array();root["models"]=Json::object();
    for(const auto& origin:plan.origins)root["origins"].push_back(origin_json(origin));
    for(const auto& binding:plan.bindings)root["bindings"].push_back(binding_json(binding));
    for(const auto& [object,asset]:plan.models)root["models"][object.text()]=asset.text();return root;}
}
std::string SpawnPlan::serialize() const{auto root=plan_fields(*this);root["scene"]=parse_script_data(scene_json,document_source_limit);auto text=root.dump();require(text.size()<=scene_document_byte_limit,"Spawn plan byte limit");return text;
}
SpawnPlan SpawnPlan::deserialize(std::string_view text){auto root=parse_script_data(text,scene_document_byte_limit);require(root.at("schema")==1,"Spawn plan schema mismatch");SpawnPlan plan;plan.scene_json=root.at("scene").dump();World validation(WorldDomain::Authoring);validation.load(plan.scene_json);
    for(const auto& origin:root.at("origins"))plan.origins.push_back({StableId::parse(origin.at("object").get<std::string>()),AssetId::parse(origin.at("asset").get<std::string>()),origin.at("path"),origin.at("name"),origin.at("layer")});
    for(const auto& b:root.at("bindings"))plan.bindings.push_back({StableId::parse(b.at("object").get<std::string>()),{StableId::parse(b.at("id").get<std::string>()),asset_key(AssetId::parse(b.at("asset").get<std::string>())),b.at("config").dump(),b.at("enabled").get<bool>()}});
    for(const auto& [object,asset]:root.at("models").items())plan.models[StableId::parse(object)]=AssetId::parse(asset.get<std::string>());if(root.contains("spawns")){require(root.at("spawns").size()<=64,"Spawn plan spawn point limit");for(const auto& [object,asset]:root.at("spawns").items())plan.spawns[StableId::parse(object)]=AssetId::parse(asset.get<std::string>());}
    if(root.contains("splines")){const auto& curves=root.at("splines");const auto& points=root.at("spline_points");require(curves.is_object()&&points.is_object()&&curves.size()<=spline_limit&&points.size()<=spline_limit*spline_point_limit,"Spawn plan spline limits");for(const auto& [object,value]:curves.items()){decode_spline(value.dump());plan.splines[StableId::parse(object)]=value.dump();}for(const auto& [object,value]:points.items())plan.spline_points[StableId::parse(object)]={StableId::parse(value.at("spline").get<std::string>()),value.at("index").get<unsigned>()};}
    if(root.contains("audio")){const auto& listed=root.at("audio");require(listed.is_object()&&listed.size()<=1,"Spawn plan audio profile limit");for(const auto& [object,value]:listed.items())plan.audio_profiles[StableId::parse(object)]=AssetId::parse(value.get<std::string>());}
    if(root.contains("travel")){const auto& travel=root.at("travel");require(travel.at("entries").is_object()&&travel.at("transitions").is_object()&&travel.at("entries").size()<=64&&travel.at("transitions").size()<=16,"Spawn plan travel limits");for(const auto& [object,value]:travel.at("entries").items()){decode_entry(value.dump());plan.entries[StableId::parse(object)]=value.dump();}for(const auto& [object,value]:travel.at("transitions").items()){decode_transition(value.dump());plan.transitions[StableId::parse(object)]=value.dump();}}
    if(root.contains("world_objects")){const auto& objects=root.at("world_objects");require(objects.at("pickups").is_object()&&objects.at("altars").is_object()&&objects.at("movers").is_object()&&objects.at("pickups").size()<=64&&objects.at("altars").size()<=16&&objects.at("movers").size()<=16,"Spawn plan world object limits");
        for(const auto& [object,value]:objects.at("pickups").items()){decode_pickup(value.dump());plan.pickups[StableId::parse(object)]=value.dump();}for(const auto& [object,value]:objects.at("altars").items()){decode_altar(value.dump());plan.altars[StableId::parse(object)]=value.dump();}for(const auto& [object,value]:objects.at("movers").items()){decode_mover(value.dump());plan.movers[StableId::parse(object)]=value.dump();}}
    if(root.contains("collision")){const auto& listed=root.at("collision");require(listed.is_object()&&listed.size()<=1,"Spawn plan collision profile limit");for(const auto& [object,value]:listed.items())plan.collision_profiles[StableId::parse(object)]=AssetId::parse(value.get<std::string>());}
    if(root.contains("interactables")){const auto& listed=root.at("interactables");require(listed.is_object()&&listed.size()<=64,"Spawn plan interactable limit");for(const auto& [object,value]:listed.items()){decode_interactable(value.dump());plan.interactables[StableId::parse(object)]=value.dump();}}
    if(root.contains("gameplay")){const auto& gameplay=root.at("gameplay");require(gameplay.at("encounters").size()<=16&&gameplay.at("triggers").size()<=64&&gameplay.at("volumes").size()<=128&&gameplay.at("spawn_owners").size()<=64,"Spawn plan gameplay volume limits");
        for(const auto& [object,value]:gameplay.at("encounters").items()){decode_encounter(value.dump());plan.encounters[StableId::parse(object)]=value.dump();}for(const auto& [object,value]:gameplay.at("triggers").items()){decode_trigger(value.dump());plan.triggers[StableId::parse(object)]=value.dump();}
        for(const auto& [object,value]:gameplay.at("volumes").items()){SpawnPlan::Volume made;made.encounter=StableId::parse(value.at("encounter").get<std::string>());made.half=value.at("half").get<std::array<double,3>>();plan.volumes[StableId::parse(object)]=made;}for(const auto& [object,value]:gameplay.at("spawn_owners").items())plan.spawn_owners[StableId::parse(object)]=StableId::parse(value.get<std::string>());}
    plan.fingerprint=sha256(plan.serialize());return plan;
}
std::string CookedScene::serialize() const{require(scripts.size()<=64,"Scene script closure limit");Json payload={{"plan",plan.serialize()},{"scripts",Json::object()}};for(const auto& [id,definition]:scripts){const auto& d=definition.descriptor;require(id==d.asset,"Scene script identity mismatch");CookedScriptPackage::deserialize(definition.cooked);payload["scripts"][id.text()]={{"version",d.state_version},{"authority",static_cast<unsigned>(d.authority)},{"writer",d.writes_transform},{"package",definition.cooked}};}auto body=payload.dump();require(body.size()<=16*1024*1024,"Cooked scene byte limit");return Json{{"schema",1},{"sha256",sha256(body)},{"payload",payload}}.dump();}
CookedScene CookedScene::deserialize(std::string_view text){auto root=parse_script_data(text,16*1024*1024);require(root.at("schema")==1 && root.at("sha256")==sha256(root.at("payload").dump()),"Cooked scene version/checksum mismatch");auto& payload=root.at("payload");CookedScene scene;scene.plan=SpawnPlan::deserialize(payload.at("plan").get<std::string>());require(payload.at("scripts").size()<=64,"Cooked scene script limit");for(const auto& [id,value]:payload.at("scripts").items()){auto asset=StableId::parse(id);auto authority=value.at("authority").get<unsigned>();require(authority<=static_cast<unsigned>(Authority::Presentation),"Invalid cooked script authority");auto artifact=value.at("package").get<std::string>();auto package=CookedScriptPackage::deserialize(artifact);ScriptDescriptor descriptor{asset,value.at("version").get<unsigned>(),static_cast<Authority>(authority),1,package.state_schema,package.config_schema,value.at("writer").get<bool>()};scene.scripts.emplace(asset,ScriptDefinition{descriptor,std::move(artifact)});}return scene;}
std::map<StableId,ScriptDefinition> cook_script_definitions(const AssemblySources& sources){std::map<StableId,ScriptDefinition> definitions;for(const auto& [asset,text]:sources){auto source=parse_script_data(text,document_source_limit);if(source.value("kind","")!="script")continue;require(source.at("asset")==asset.text() && source.at("schema")==1,"ScriptAsset schema/identity mismatch");auto hex=asset.text();hex.erase(23,1);hex.erase(18,1);hex.erase(13,1);hex.erase(8,1);auto id=StableId::parse(hex);auto domain=source.value("domain","server");require(domain=="server" || domain=="presentation","Unsupported script domain");ScriptDescriptor d{id,source.value("state_version",1u),domain=="server"?Authority::Server:Authority::Presentation,1,source.value("state_schema",""),source.value("config_schema",""),source.value("writes_transform",true)};
    ScriptPackage package{source.value("entry","behavior"),{},source.value("aliases",std::map<std::string,std::string>{}),d.state_schema,d.config_schema};
    if(source.contains("modules")){for(const auto& module:source.at("modules"))package.modules.push_back({StableId::parse(module.at("id").get<std::string>()),module.at("path"),module.at("source"),module.at("dependencies").get<std::vector<std::string>>()});}
    else package.modules.push_back({id,package.entry,source.at("source").get<std::string>(),{}});
    definitions.emplace(id,ScriptDefinition{d,cook_scripts(package).serialize()});
}return definitions;}
std::unique_ptr<SceneSession::Prepared> SceneSession::prepare(const SpawnPlan& plan,const std::map<StableId,ScriptDefinition>& definitions,const std::function<void(AssetId)>& validate_resource) const{
    auto prepared=std::make_unique<Prepared>();prepared->plan=plan;prepared->world=std::make_unique<World>(domain_);prepared->world->load(plan.scene_json);prepared->scripts=std::make_unique<ScriptRuntime>(*prepared->world);
    for(const auto& [object,asset]:plan.models){require(prepared->world->valid(prepared->world->find(object)),"Model binding has no object");if(domain_==WorldDomain::Server && !validate_resource)continue;require(static_cast<bool>(validate_resource),"Required model validator missing");validate_resource(asset);}
    std::set<StableId> loaded;for(const auto& binding:plan.bindings){require(definitions.contains(binding.binding.asset),"Missing required cooked ScriptAsset");const auto& definition=definitions.at(binding.binding.asset);
        if((domain_==WorldDomain::Server && definition.descriptor.authority==Authority::Presentation) || (domain_==WorldDomain::Preview && definition.descriptor.authority==Authority::Server))continue;
        const auto object=prepared->world->read(prepared->world->find(binding.object));bool declared=false;for(const auto& b:object.scripts.records)if(b.id==binding.binding.id)declared=b.asset==binding.binding.asset && b.config_json==binding.binding.config_json && b.enabled==binding.binding.enabled;require(declared,"Spawn plan differs from native ScriptBindings");
        if(loaded.insert(binding.binding.asset).second)require(prepared->scripts->load_cooked(definition.descriptor,definition.cooked),prepared->scripts->diagnostic().c_str());
        prepared->scripts->attach(prepared->world->find(binding.object),binding.binding);
    }return prepared;
}
void SceneSession::activate(std::unique_ptr<Prepared> prepared){require(prepared && prepared->world->domain()==domain_ && prepared->world->phase()==Phase::Idle && (!active_ || active_->world->phase()==Phase::Idle),"Scene activation requires a matching prepared scene and safe point");active_=std::move(prepared);}
void SceneSession::retire(){require(!active_ || active_->world->phase()==Phase::Idle,"Scene retirement requires safe point");active_.reset();}
void SceneSession::step(double seconds){require(active_!=nullptr,"No active scene");auto& state=*active_;state.world->begin_scripts();state.scripts->tick(seconds);state.world->commit();state.world->publish();}
bool static_scenery(const SpawnPlan& plan,StableId object,AssetId skin){
    const auto model=plan.models.find(object);if(model==plan.models.end()||model->second==skin)return false;
    for(const auto* roles:{&plan.interactables,&plan.entries,&plan.transitions,&plan.splines,&plan.pickups,&plan.altars,&plan.movers,&plan.encounters,&plan.triggers})if(roles->contains(object))return false;
    if(plan.spawns.contains(object)||plan.spline_points.contains(object)||plan.audio_profiles.contains(object)||plan.collision_profiles.contains(object)||plan.volumes.contains(object)||plan.spawn_owners.contains(object))return false;
    return std::none_of(plan.bindings.begin(),plan.bindings.end(),[&](const auto& binding){return binding.object==object;});
}
}
