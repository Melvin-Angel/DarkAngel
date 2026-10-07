#include <darkangel/world.hpp>
#include <darkangel/asset_id.hpp>
#include "metadata.hpp"
#include <flecs.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace darkangel {
namespace {
using Json=nlohmann::json;
std::atomic<std::uint64_t> next_domain{1};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void keys(const Json& j, std::initializer_list<std::string_view> expected) {
    require(j.is_object() && j.size()==expected.size(),"Unexpected schema fields");
    for (auto key : expected) require(j.contains(std::string(key)),"Missing schema field");
}
Json parse(std::string_view source) {
    require(source.size()<=1024*1024,"Scene exceeds 1 MiB limit");
    std::vector<std::set<std::string>> objects;
    std::size_t values{};
    return Json::parse(source,[&](int depth, Json::parse_event_t event, Json& value) {
        require(depth<=32 && ++values<=65536,"JSON work/depth limit exceeded");
        if (event==Json::parse_event_t::object_start) objects.emplace_back();
        if (event==Json::parse_event_t::key) require(objects.back().insert(value.get<std::string>()).second,"Duplicate JSON key");
        if (event==Json::parse_event_t::object_end) objects.pop_back();
        return true;
    });
}
const void* component(const ObjectData& d, TypeId id) {
    if (id==1) return &d.transform;
    if (id==2) return &d.health;
    if (id==3) return &d.network;
    if (id==4) return &d.scripts;
    throw std::runtime_error("Unknown TypeId");
}
void* component(ObjectData& d, TypeId id) { return const_cast<void*>(component(std::as_const(d),id)); }
void validate(const ObjectData& data) {
    require(static_cast<bool>(data.id),"Zero object ID");
    for (const auto& t : metadata()) for (const auto& p : t.properties) if (p.kind==ValueKind::Number) {
        double value;
        std::memcpy(&value,static_cast<const char*>(component(data,t.id))+p.offset,sizeof(value));
        require(std::isfinite(value) && value>=p.minimum && value<=p.maximum,"Property out of range");
    }
    require(data.health.current<=data.health.maximum,"Health current exceeds maximum");
    if(!data.optional_json.empty())require(parse(data.optional_json).is_object(),"Optional authoring data must be an object");
    require(data.scripts.records.size()<=16,"ScriptBindings attachment limit");std::set<StableId> bindings;
    for(const auto& b:data.scripts.records){require(static_cast<bool>(b.id) && static_cast<bool>(b.asset) && bindings.insert(b.id).second,"Invalid/duplicate binding identity");require(b.config_json.empty() || parse(b.config_json).is_object(),"Binding config must be a record");}
}
template<class T> void register_type(flecs::world& world, const TypeMetadata& type) {
    auto c=world.component<T>(std::string(type.name).c_str());
    for (const auto& p : type.properties) {
        if (p.kind==ValueKind::Number) c.member(flecs::F64,std::string(p.name).c_str(),1,static_cast<int32_t>(p.offset));
        else c.member(flecs::U64,std::string(p.name).c_str(),1,static_cast<int32_t>(p.offset));
    }
}
}
struct World::Impl {
    struct References { StableId id,target;std::string optional; };
    flecs::world ecs;
    WorldDomain domain;
    const std::uint64_t token{next_domain.fetch_add(1)};
    std::uint64_t epoch{1};
    std::thread::id owner{std::this_thread::get_id()};
    Phase phase{Phase::Idle};
    bool scripts_claimed{};
    std::size_t capacity, limit;
    std::map<StableId,EntityHandle> entities;
    std::set<std::uint64_t> owned;
    struct Command { EntityHandle handle; double yaw{}; bool destroy{}; };
    std::vector<Command> commands;
    Impl(WorldDomain d,std::size_t c,std::size_t l):domain(d),capacity(c),limit(l) {
        register_type<Transform>(ecs,schema::types[0]);
        register_type<Health>(ecs,schema::types[1]);
        register_type<NetworkIdentity>(ecs,schema::types[2]);
        register_type<ScriptBindings>(ecs,schema::types[3]);
    }
    void thread() const { require(owner==std::this_thread::get_id(),"World accessed from wrong thread"); }
    bool valid(EntityHandle h) const {
        return h.domain==token && h.epoch==epoch && owned.contains(h.runtime) && ecs_is_alive(ecs.c_ptr(),h.runtime);
    }
    void check(EntityHandle h) const { thread(); require(valid(h),"Stale or wrong-domain entity handle"); }
    void authority(Authority a) const {
        thread();
        require((domain==WorldDomain::Server && a==Authority::Server) ||
            (domain==WorldDomain::Authoring && a==Authority::Authoring) ||
            ((domain==WorldDomain::ClientPresentation || domain==WorldDomain::Preview) && a==Authority::Presentation),"Authority denied");
    }
};
World::World(WorldDomain d,std::size_t c,std::size_t l):impl_(std::make_unique<Impl>(d,c,l)) { require(c>0 && l>0,"Invalid world limits"); }
World::~World()=default;
void World::claim_script_runtime() {impl_->thread();require(!impl_->scripts_claimed,"World already owns a script VM");impl_->scripts_claimed=true;}
void World::release_script_runtime() {impl_->thread();impl_->scripts_claimed=false;}
WorldDomain World::domain() const { impl_->thread(); return impl_->domain; }
Phase World::phase() const { impl_->thread(); return impl_->phase; }
bool World::valid(EntityHandle h) const { impl_->thread(); return impl_->valid(h); }
EntityHandle World::create(const ObjectData& d) {
    auto& p=*impl_; p.thread(); require(p.phase==Phase::Idle,"Create requires safe point"); validate(d);
    require(p.entities.size()<p.capacity && !p.entities.contains(d.id),"Object capacity or duplicate ID");
    auto scripts=d.scripts;scripts.count=scripts.records.size();auto e=p.ecs.entity().set<Transform>(d.transform).set<Health>(d.health).set<NetworkIdentity>(d.network).set<ScriptBindings>(scripts).set<Impl::References>({d.id,d.target,d.optional_json});
    EntityHandle h{p.token,p.epoch,e.id()};
    try {p.owned.insert(h.runtime);p.entities.emplace(d.id,h);}
    catch(...) {p.owned.erase(h.runtime);e.destruct();throw;}
    return h;
}
ObjectData World::read(EntityHandle h) const {
    impl_->check(h); auto e=impl_->ecs.entity(h.runtime);
    ObjectData d;const auto& refs=*e.try_get<Impl::References>();d.id=refs.id;d.target=refs.target;d.optional_json=refs.optional;
    d.transform=*e.try_get<Transform>(); d.health=*e.try_get<Health>(); d.network=*e.try_get<NetworkIdentity>();d.scripts=*e.try_get<ScriptBindings>(); return d;
}
EntityHandle World::find(StableId id) const {
    impl_->thread(); auto it=impl_->entities.find(id); return it==impl_->entities.end() ? EntityHandle{} : it->second;
}
EntityHandle World::target(EntityHandle h) const { return find(read(h).target); }
void World::destroy(EntityHandle h,Authority a) {
    auto& p=*impl_; p.check(h); p.authority(a);
    require(p.phase==Phase::Idle || p.phase==Phase::Script,"Invalid destroy phase");
    if (p.phase==Phase::Script) { require(p.commands.size()<p.limit,"Command limit exceeded"); p.commands.push_back({h,0,true}); }
    else { p.entities.erase(read(h).id);p.owned.erase(h.runtime);p.ecs.entity(h.runtime).destruct(); }
}
void World::reset() {
    auto& p=*impl_; p.thread(); require(p.phase==Phase::Idle,"Reset requires safe point");
    for (auto [id,h] : p.entities) p.ecs.entity(h.runtime).destruct();
    p.entities.clear();p.owned.clear();p.commands.clear();++p.epoch;
}
void World::begin_scripts() { impl_->thread(); require(impl_->phase==Phase::Idle,"Invalid script phase"); impl_->phase=Phase::Script; }
void World::stage_yaw(EntityHandle h,double value,Authority a) {
    auto& p=*impl_; p.check(h); p.authority(a);
    require(p.phase==Phase::Script,"Mutation outside script phase");
    require(std::isfinite(value) && std::abs(value)<=schema::finite_limit,"Invalid yaw");
    require(p.commands.size()<p.limit,"Command limit exceeded"); p.commands.push_back({h,value,false});
}
void World::commit() {
    auto& p=*impl_; p.thread(); require(p.phase==Phase::Script,"Invalid commit phase"); p.phase=Phase::Commit;
    // Flecs owns structural deferral; owned commands define visibility and authority.
    p.ecs.defer_begin();
    for (const auto& cmd : p.commands) {
        if (!p.valid(cmd.handle)) continue;
        if (cmd.destroy) { p.entities.erase(read(cmd.handle).id);p.owned.erase(cmd.handle.runtime);p.ecs.entity(cmd.handle.runtime).destruct(); }
        else if (p.entities.contains(read(cmd.handle).id)){auto transform=read(cmd.handle).transform;transform.yaw=cmd.yaw;p.ecs.entity(cmd.handle.runtime).set<Transform>(transform);}
    }
    p.ecs.defer_end(); p.commands.clear(); p.phase=Phase::Publish;
}
void World::publish() { impl_->thread(); require(impl_->phase==Phase::Publish,"Invalid publish phase"); impl_->phase=Phase::Idle; }
std::size_t World::script_checkpoint() const{impl_->thread();require(impl_->phase==Phase::Script,"Checkpoint requires script phase");return impl_->commands.size();}
void World::rollback_script(std::size_t checkpoint){impl_->thread();require(impl_->phase==Phase::Script && checkpoint<=impl_->commands.size(),"Invalid script rollback");impl_->commands.resize(checkpoint);}
void World::edit_number(EntityHandle h,TypeId type,PropertyId property,double value,Authority a) {
    const NumberEdit edit{h,type,property,value};apply_edits({&edit,1},a);
}
void World::apply_edits(std::span<const NumberEdit> edits,Authority a) {
    impl_->authority(a);require(impl_->phase==Phase::Idle && impl_->domain==WorldDomain::Authoring,"Edits require idle authoring world");
    require(edits.size()<=impl_->limit,"Edit batch exceeds command limit");
    std::map<EntityHandle,ObjectData> staged;
    for(const auto& edit : edits) {
        impl_->check(edit.entity);auto [it,inserted]=staged.try_emplace(edit.entity,read(edit.entity));auto& d=it->second;bool found=false;
        for(const auto& t : metadata())if(t.id==edit.type)for(const auto& p : t.properties)if(p.id==edit.property) {
            require(p.kind==ValueKind::Number && (p.flags&Editable) && !(p.flags&ReadOnly),"Property not editable");
            std::memcpy(static_cast<char*>(component(d,t.id))+p.offset,&edit.value,sizeof(edit.value));found=true;
        }
        require(found,"Unknown property ID");
    }
    // Validate final values together (e.g. lowering maximum and current in one command).
    for(const auto& [h,d] : staged)validate(d);
    for(const auto& [h,d] : staged) {auto e=impl_->ecs.entity(h.runtime);e.set<Transform>(d.transform);e.set<Health>(d.health);}
}
void World::set_transform(EntityHandle h,const Transform& value,Authority a){
    impl_->check(h);impl_->authority(a);require(impl_->phase==Phase::Idle,"Transform update requires idle safe point");
    require(impl_->domain==WorldDomain::Server,"Transform update requires server world");
    auto candidate=read(h);candidate.transform=value;validate(candidate);impl_->ecs.entity(h.runtime).set<Transform>(value);
}
std::string World::serialize() const {
    impl_->thread(); require(impl_->phase==Phase::Idle,"Capture requires safe point");
    Json root={{"version",1},{"objects",Json::array()}};
    for (auto [id,h] : impl_->entities) {
        auto d=read(h); Json types=Json::object();
        for (const auto& t : metadata()) {
            Json fields=Json::object();
            for (const auto& p : t.properties) if (p.flags&SaveGame) {
                double value; std::memcpy(&value,static_cast<const char*>(component(d,t.id))+p.offset,sizeof(value));
                fields[std::to_string(p.id)]=value;
            }
            if (!fields.empty()) types[std::to_string(t.id)]={{"version",t.version},{"fields",fields}};
        }
        root["objects"].push_back({{"id",id.text()},{"target",d.target ? Json(d.target.text()):Json(nullptr)},{"types",types}});
        if(!d.scripts.records.empty()){Json records=Json::object();for(const auto& b:d.scripts.records)records[b.id.text()]={{"asset",asset_id(b.asset).text()},{"enabled",b.enabled},{"config",b.config_json.empty()?Json::object():parse(b.config_json)}};root["objects"].back()["types"]["4"]={{"version",1},{"bindings",records}};}
        if(!d.optional_json.empty())root["objects"].back()["optional"]=parse(d.optional_json);
    }
    auto source=root.dump(); require(source.size()<=1024*1024,"Serialized scene exceeds limit"); return source;
}
std::vector<EntityHandle> World::load(std::string_view source) {
    auto& p=*impl_; p.thread(); require(p.phase==Phase::Idle && p.entities.empty(),"Load requires empty idle destination");
    auto root=parse(source); keys(root,{"version","objects"}); require(root["version"]==1,"Unsupported scene version");
    require(root["objects"].is_array() && root["objects"].size()<=p.capacity,"Scene object limit");
    std::vector<ObjectData> plan; std::set<StableId> ids;
    for (const auto& object : root["objects"]) {
        if(object.contains("optional"))keys(object,{"id","target","types","optional"});else keys(object,{"id","target","types"}); ObjectData d;
        if(object.contains("optional")){require(object.at("optional").is_object(),"Optional authoring data must be an object");d.optional_json=object.at("optional").dump();}
        d.id=StableId::parse(object.at("id").get<std::string>()); require(ids.insert(d.id).second,"Duplicate scene ID");
        if (!object["target"].is_null()) d.target=StableId::parse(object["target"].get<std::string>());
        if(object["types"].contains("4"))keys(object["types"],{"1","2","4"});else keys(object["types"],{"1","2"});
        if(object["types"].contains("4")){auto& record=object["types"]["4"];keys(record,{"version","bindings"});require(record["version"]==1 && record["bindings"].is_object(),"Unsupported ScriptBindings schema");for(const auto& [binding,value]:record["bindings"].items()){keys(value,{"asset","enabled","config"});require(value["enabled"].is_boolean() && value["config"].is_object(),"Invalid script binding data");d.scripts.records.push_back({StableId::parse(binding),asset_key(AssetId::parse(value["asset"].get<std::string>())),value["config"].dump(),value["enabled"].get<bool>()});}d.scripts.count=d.scripts.records.size();}
        for (const auto& t : metadata()) {
            if (t.id==3 || t.id==4) continue; const auto& record=object["types"].at(std::to_string(t.id));
            keys(record,{"version","fields"});
            auto fields=record["fields"];auto version=record["version"].get<unsigned>();
            // Registered native migration: Transform v1 yaw -> v2 spatial transform.
            if(t.id==1 && version==1){require(fields.is_object() && fields.size()==1 && fields.contains("1"),"Invalid legacy Transform");for(const auto& prop:t.properties)if(prop.id!=1){double value;std::memcpy(&value,static_cast<const char*>(component(d,t.id))+prop.offset,sizeof(value));fields[std::to_string(prop.id)]=value;}version=2;}
            require(version==t.version,"Unsupported component version; migration required");
            require(fields.is_object() && fields.size()==t.properties.size(),"Invalid property fields");
            for (const auto& prop : t.properties) {
                const auto& v=fields.at(std::to_string(prop.id)); require(v.is_number(),"Expected numeric property");
                double value=v.get<double>(); std::memcpy(static_cast<char*>(component(d,t.id))+prop.offset,&value,sizeof(value));
            }
        }
        validate(d); plan.push_back(d);
    }
    for (const auto& d : plan) require(!d.target || ids.contains(d.target),"Unresolved required scene reference");
    std::vector<EntityHandle> handles; handles.reserve(plan.size());
    try { for (const auto& d : plan) handles.push_back(create(d)); }
    catch (...) { reset(); throw; }
    return handles;
}
bool World::reflection_backed() const {
    impl_->thread();
    return impl_->ecs.component<Transform>().has<flecs::Struct>() && impl_->ecs.component<Health>().has<flecs::Struct>() && impl_->ecs.component<NetworkIdentity>().has<flecs::Struct>() && impl_->ecs.component<ScriptBindings>().has<flecs::Struct>();
}
}
