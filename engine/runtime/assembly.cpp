#include <darkangel/assembly.hpp>
#include <darkangel/hash.hpp>
#include "script_state.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <cstring>
#include <stdexcept>
namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool test,const char* error){if(!test)throw std::runtime_error(error);}
StableId persistent(std::string_view path){return StableId::parse(sha256(path).substr(0,32));}
void migrate(Json& entity){auto& transform=entity.at("types").at("1");if(transform.at("version")==1){require(transform.at("fields").size()==1 && transform.at("fields").contains("1"),"Malformed legacy transform");Transform defaults;for(const auto& field:metadata()[0].properties)if(field.id!=1){double value;std::memcpy(&value,reinterpret_cast<const char*>(&defaults)+field.offset,sizeof(value));transform["fields"][std::to_string(field.id)]=value;}transform["version"]=2;}}
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
    else if(op=="BindScript")entities[target]["scripts"][operation.at("binding").get<std::string>()]=operation.at("value");
    else if(op=="RemoveScript"){auto& scripts=entities[target].at("scripts");require(scripts.erase(operation.at("binding").get<std::string>())==1,"Missing script binding patch target");}
    else throw std::runtime_error("Unsupported typed patch operation");
}
struct Resolver {
    const AssemblySources& sources;SpawnPlan plan;Json scene{{"version",1},{"objects",Json::array()}};
    std::set<AssetId> stack;std::map<std::string,StableId> paths;std::map<std::string,std::string> exports;
    struct Pending {std::size_t object;std::string scope;Json reference;};std::vector<Pending> references;
    struct PendingConfig {std::size_t binding;std::string scope;Json references;};std::vector<PendingConfig> configs;
    std::map<std::string,std::string> layers;
    Json source(AssetId id){require(sources.contains(id),"Missing declared AssemblyAsset");auto graph=parse_script_data(sources.at(id));require(graph.at("schema")==1 && graph.at("asset")==id.text() && graph.at("entities").is_object(),"Assembly schema/identity mismatch");for(auto& [local,entity]:graph["entities"].items())migrate(entity);return graph;}
    Json definition(AssetId id){auto graph=source(id);if(graph.contains("base") && !graph["base"].is_null()){
        auto base=source(AssetId::parse(graph["base"].get<std::string>()));require(!base.contains("base") || base["base"].is_null(),"Only one variant level is enabled");
        require(graph.at("entities").empty() && (!graph.contains("mounts") || graph["mounts"].empty()),"Variants use explicit typed patches");
        if(graph.contains("base_fingerprint"))require(graph["base_fingerprint"]==sha256(base.dump()),"Base fingerprint changed; explicit rebase required");
        for(const auto& operation:graph.value("patches",Json::array()))patch(base,operation);base["asset"]=id.text();return base;
    }return graph;}
    void visit(AssetId id,const std::string& scope,const Json& edits,unsigned depth){
        require(depth<=16 && stack.insert(id).second,"Cyclic/deep assembly composition");auto graph=definition(id);require(edits.is_array() && edits.size()<=1024,"Placement patch limit");
        for(const auto& operation:edits)patch(graph,operation);
        for(const auto& [local,entity]:graph.at("entities").items()){
            StableId::parse(local);require(plan.origins.size()<1024,"Assembly object limit");auto path=scope+"/"+local;auto object=persistent(path);require(paths.emplace(path,object).second,"Duplicate authored path");
            for(const auto& origin:plan.origins)require(origin.object!=object,"Persistent identity collision");
            Json native={{"id",object.text()},{"types",entity.at("types")},{"target",nullptr}};if(entity.contains("optional"))native["optional"]=entity["optional"];
            if(entity.contains("target") && !entity["target"].is_null())references.push_back({scene["objects"].size(),scope,entity["target"]});
            scene["objects"].push_back(native);plan.origins.push_back({object,id,path,entity.value("name",local),edits.empty()?"definition":"placement patches"});
            if(entity.contains("model") && !entity["model"].is_null())plan.models[object]=AssetId::parse(entity["model"].get<std::string>());
            auto scripts=entity.value("scripts",Json::object());for(const auto& [attachment,script]:scripts.items()){
                StableId::parse(attachment);auto asset=AssetId::parse(script.at("asset").get<std::string>());auto text=asset.text();text.erase(23,1);text.erase(18,1);text.erase(13,1);text.erase(8,1);
                auto config=script.value("config",Json::object());require(config.is_object(),"Binding config must be a record");
                plan.bindings.push_back({object,{persistent(path+"/binding/"+attachment),StableId::parse(text),config.dump(),script.value("enabled",true)}});
                if(script.contains("entity_refs"))configs.push_back({plan.bindings.size()-1,scope,script["entity_refs"]});
            }
        }
        auto export_records=graph.value("exports",Json::object());for(const auto& [export_id,local]:export_records.items()){StableId::parse(export_id);require(local.is_string(),"Export must address a stable local path");exports[scope+":"+export_id]=scope+"/"+local.get<std::string>();}
        auto mounts=graph.value("mounts",Json::object());require(mounts.is_object() && mounts.size()<=128,"Assembly mount limit");
        for(const auto& [mount_id,mount]:mounts.items()){StableId::parse(mount_id);visit(AssetId::parse(mount.at("asset").get<std::string>()),scope+"/"+mount_id,mount.value("patches",Json::array()),depth+1);}
        stack.erase(id);
    }
    StableId reference(const std::string& scope,const Json& ref){std::string path;
        if(ref.is_string())path=scope+"/"+ref.get<std::string>();
        else if(ref.contains("local"))path=scope+"/"+ref.at("local").get<std::string>();
        else{auto mount=ref.at("mount").get<std::string>(),key=ref.at("export").get<std::string>();StableId::parse(mount);StableId::parse(key);auto name=scope+"/"+mount+":"+key;require(exports.contains(name),"Unresolved declared assembly export");path=exports.at(name);}
        require(paths.contains(path),"Unresolved required assembly EntityRef");return paths.at(path);
    }
};
}
SpawnPlan resolve_assembly(AssetId root,StableId placement,const AssemblySources& sources){require(static_cast<bool>(placement) && sources.size()<=128,"Invalid placement/source closure limit");Resolver resolver{sources};resolver.visit(root,placement.text(),Json::array(),0);
    for(const auto& ref:resolver.references)resolver.scene["objects"][ref.object]["target"]=resolver.reference(ref.scope,ref.reference).text();
    for(const auto& ref:resolver.configs){auto config=parse_script_data(resolver.plan.bindings[ref.binding].binding.config_json);for(const auto& [field,target]:ref.references.items())config[field]=resolver.reference(ref.scope,target).text();resolver.plan.bindings[ref.binding].binding.config_json=config.dump();}
    for(const auto& b:resolver.plan.bindings)for(auto& object:resolver.scene["objects"])if(object["id"]==b.object.text()){auto& record=object["types"]["4"];record["version"]=1;record["bindings"][b.binding.id.text()]={{"asset",asset_id(b.binding.asset).text()},{"enabled",b.binding.enabled},{"config",parse_script_data(b.binding.config_json)}};}
    for(const auto& [key,path]:resolver.exports)require(resolver.paths.contains(path),"Export addresses removed entity");
    World validation(WorldDomain::Authoring);validation.load(resolver.scene.dump());resolver.plan.scene_json=validation.serialize();resolver.plan.fingerprint=sha256(resolver.plan.serialize());return resolver.plan;
}
std::string SpawnPlan::serialize() const{Json root={{"schema",1},{"scene",parse_script_data(scene_json)},{"origins",Json::array()},{"bindings",Json::array()},{"models",Json::object()}};
    for(const auto& origin:origins)root["origins"].push_back({{"object",origin.object.text()},{"asset",origin.definition.text()},{"path",origin.local_path},{"name",origin.name},{"layer",origin.layer}});
    for(const auto& binding:bindings)root["bindings"].push_back({{"object",binding.object.text()},{"id",binding.binding.id.text()},{"asset",asset_id(binding.binding.asset).text()},{"config",parse_script_data(binding.binding.config_json)},{"enabled",binding.binding.enabled}});
    for(const auto& [object,asset]:models)root["models"][object.text()]=asset.text();auto text=root.dump();require(text.size()<=1024*1024,"Spawn plan byte limit");return text;
}
SpawnPlan SpawnPlan::deserialize(std::string_view text){auto root=parse_script_data(text,1024*1024);require(root.at("schema")==1,"Spawn plan schema mismatch");SpawnPlan plan;plan.scene_json=root.at("scene").dump();World validation(WorldDomain::Authoring);validation.load(plan.scene_json);
    for(const auto& origin:root.at("origins"))plan.origins.push_back({StableId::parse(origin.at("object").get<std::string>()),AssetId::parse(origin.at("asset").get<std::string>()),origin.at("path"),origin.at("name"),origin.at("layer")});
    for(const auto& b:root.at("bindings"))plan.bindings.push_back({StableId::parse(b.at("object").get<std::string>()),{StableId::parse(b.at("id").get<std::string>()),asset_key(AssetId::parse(b.at("asset").get<std::string>())),b.at("config").dump(),b.at("enabled").get<bool>()}});
    for(const auto& [object,asset]:root.at("models").items())plan.models[StableId::parse(object)]=AssetId::parse(asset.get<std::string>());plan.fingerprint=sha256(plan.serialize());return plan;
}
std::string CookedScene::serialize() const{require(scripts.size()<=64,"Scene script closure limit");Json payload={{"plan",plan.serialize()},{"scripts",Json::object()}};for(const auto& [id,definition]:scripts){const auto& d=definition.descriptor;require(id==d.asset,"Scene script identity mismatch");CookedScriptPackage::deserialize(definition.cooked);payload["scripts"][id.text()]={{"version",d.state_version},{"authority",static_cast<unsigned>(d.authority)},{"writer",d.writes_transform},{"package",definition.cooked}};}auto body=payload.dump();require(body.size()<=16*1024*1024,"Cooked scene byte limit");return Json{{"schema",1},{"sha256",sha256(body)},{"payload",payload}}.dump();}
CookedScene CookedScene::deserialize(std::string_view text){auto root=parse_script_data(text,16*1024*1024);require(root.at("schema")==1 && root.at("sha256")==sha256(root.at("payload").dump()),"Cooked scene version/checksum mismatch");auto& payload=root.at("payload");CookedScene scene;scene.plan=SpawnPlan::deserialize(payload.at("plan").get<std::string>());require(payload.at("scripts").size()<=64,"Cooked scene script limit");for(const auto& [id,value]:payload.at("scripts").items()){auto asset=StableId::parse(id);auto authority=value.at("authority").get<unsigned>();require(authority<=static_cast<unsigned>(Authority::Presentation),"Invalid cooked script authority");auto artifact=value.at("package").get<std::string>();auto package=CookedScriptPackage::deserialize(artifact);ScriptDescriptor descriptor{asset,value.at("version").get<unsigned>(),static_cast<Authority>(authority),1,package.state_schema,package.config_schema,value.at("writer").get<bool>()};scene.scripts.emplace(asset,ScriptDefinition{descriptor,std::move(artifact)});}return scene;}
std::map<StableId,ScriptDefinition> cook_script_definitions(const AssemblySources& sources){std::map<StableId,ScriptDefinition> definitions;for(const auto& [asset,text]:sources){auto source=parse_script_data(text);if(source.value("kind","")!="script")continue;require(source.at("asset")==asset.text() && source.at("schema")==1,"ScriptAsset schema/identity mismatch");auto hex=asset.text();hex.erase(23,1);hex.erase(18,1);hex.erase(13,1);hex.erase(8,1);auto id=StableId::parse(hex);auto domain=source.value("domain","server");require(domain=="server" || domain=="presentation","Unsupported script domain");ScriptDescriptor d{id,source.value("state_version",1u),domain=="server"?Authority::Server:Authority::Presentation,1,source.value("state_schema",""),source.value("config_schema",""),source.value("writes_transform",true)};
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
}
