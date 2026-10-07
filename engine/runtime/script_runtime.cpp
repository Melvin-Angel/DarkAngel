#include <darkangel/script_runtime.hpp>
#include "script_state.hpp"
#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>
#include <set>
#include <cstring>
#include <algorithm>

namespace darkangel {
namespace {
void require(bool test,const char* message) { if(!test) throw std::runtime_error(message); }
}
struct ScriptRuntime::Impl {
    struct Allocation { std::size_t used{}, limit; } allocation;
    World& world;
    ScriptLimits limits;
    std::thread::id owner{std::this_thread::get_id()};
    lua_State* vm{};
    struct Generation;
    struct ImportContext {Impl* host;Generation* generation;std::string path;};
    struct Module {lua_State* thread;int export_ref;};
    struct Generation {
        lua_State* vm;CookedScriptPackage package;
        std::map<std::string,Module> loaded;
        std::set<std::string> loading;
        std::vector<int> references;
        std::vector<std::unique_ptr<ImportContext>> imports;
        ~Generation(){for(auto ref:references)lua_unref(vm,ref);}
    };
    struct Program {
        std::unique_ptr<Generation> current;
        ScriptStateSchema schema,config;
        ScriptDescriptor descriptor;
        lua_State* active{};int definition_ref{LUA_NOREF};
        std::uint64_t generation{};ScriptMetrics metrics;
    };
    std::map<StableId,std::unique_ptr<Program>> programs;
    StableId primary;
    Program* running{};
    std::string error;
    bool writes{};
    std::chrono::steady_clock::time_point deadline;
    struct Instance {EntityHandle owner;StableId asset;ScriptStateSchema::Json data,config;bool enabled{true};};
    std::map<StableId,Instance> instances;
    struct Context { Impl* host; EntityHandle entity;StableId binding; };
    struct Task {std::uint64_t token,generation;EntityHandle owner;StableId binding,asset;lua_State* coroutine;int thread_ref,state_ref;bool started{},event_ready{};std::string event;double wake{};};
    std::map<std::uint64_t,std::unique_ptr<Task>> task_queue;
    std::vector<std::string> events;
    std::uint64_t next_task{};double simulation_time{};ScriptMetrics metrics;
    static void* allocate(void* ud,void* ptr,std::size_t old,std::size_t size) {
        auto& a=*static_cast<Allocation*>(ud);
        if(!ptr) old=0;
        if(!size) { std::free(ptr); a.used-=old; return nullptr; }
        if(size>a.limit || a.used-old>a.limit-size) return nullptr;
        void* next=std::realloc(ptr,size); if(next)a.used=a.used-old+size; return next;
    }
    static void interrupt(lua_State* s,int gc) {
        auto* host=static_cast<Impl*>(lua_callbacks(s)->userdata);
        // GC notifications can occur outside protected execution; never throw there.
        if(gc>=0)return;
        if(std::chrono::steady_clock::now()>host->deadline)luaL_error(s,"DarkAngel script execution budget exceeded");
    }
    static Context* context(lua_State* s) {
        auto* c=static_cast<Context*>(lua_touserdatatagged(s,1,1));
        if(!c)luaL_error(s,"Expected DarkAngel Context");
        return c;
    }
    static int get_yaw(lua_State* s) {
        auto* c=context(s); double value{}; bool ok=true;
        try { value=c->host->world.read(c->entity).transform.yaw; } catch(...) { ok=false; }
        if(!ok)luaL_error(s,"Stale or wrong-domain Context"); lua_pushnumber(s,value); return 1;
    }
    static int set_yaw(lua_State* s) {
        auto* c=context(s); double value=luaL_checknumber(s,2); bool ok=true;
        if(!c->host->writes || !c->host->running->descriptor.writes_transform)luaL_error(s,"World writes disabled by phase/writer contract");
        try { c->host->world.stage_yaw(c->entity,value,c->host->running->descriptor.authority); } catch(...) { ok=false; }
        if(!ok)luaL_error(s,"World write rejected: handle, phase, authority, range or capacity"); return 0;
    }
    static int wait_event(lua_State* s) {
        auto* c=context(s);const char* event=luaL_checkstring(s,2);auto* task=static_cast<Task*>(lua_getthreaddata(s));
        if(!task || task->binding!=c->binding || task->generation!=c->host->running->generation || !c->host->writes)luaL_error(s,"Wait is allowed only in a scheduled task");
        auto size=std::strlen(event);if(!size || size>64)luaL_error(s,"Event name limit");task->event=event;task->wake=0;return lua_yield(s,0);
    }
    static int wait_seconds(lua_State* s) {
        auto* c=context(s);auto seconds=luaL_checknumber(s,2);auto* task=static_cast<Task*>(lua_getthreaddata(s));
        if(!task || task->binding!=c->binding || task->generation!=c->host->running->generation || !c->host->writes)luaL_error(s,"Wait is allowed only in a scheduled task");
        if(!std::isfinite(seconds) || seconds<=0 || seconds>3600)luaL_error(s,"Invalid simulation wait");task->event.clear();task->wake=c->host->simulation_time+seconds;return lua_yield(s,0);
    }
    static int require_module(lua_State* s) {
        auto* context=static_cast<ImportContext*>(lua_touserdata(s,lua_upvalueindex(1)));const char* path=luaL_checkstring(s,1);
        char error[1024]{};bool ok=true;
        try {
            auto resolved=resolve_script_import(context->path,path,context->generation->package.aliases);
            const auto& modules=context->generation->package.modules;
            auto found=std::find_if(modules.begin(),modules.end(),[&](const auto& m){return m.path==context->path;});
            require(found!=modules.end() && std::find(found->dependencies.begin(),found->dependencies.end(),resolved)!=found->dependencies.end(),"Import is not declared");
            context->host->load_module(*context->generation,resolved,s);
        }catch(const std::exception& e){ok=false;strncpy_s(error,e.what(),_TRUNCATE);}
        if(!ok)luaL_error(s,"%s",error);return 1;
    }
    Impl(World& w,ScriptLimits l):allocation{0,l.memory_bytes},world(w),limits(l) {
        require(l.memory_bytes>=1024*1024 && l.instances>0 && l.source_bytes>0 && l.call_time.count()>0,"Invalid script limits");
        world.claim_script_runtime();
        vm=lua_newstate(allocate,&allocation);
        if(!vm) {world.release_script_runtime();throw std::runtime_error("Luau allocation failed");}
        lua_callbacks(vm)->userdata=this; lua_callbacks(vm)->interrupt=interrupt;
        deadline=std::chrono::steady_clock::now()+limits.call_time;
        luaL_openlibs(vm);
        luaL_newmetatable(vm,"DarkAngel.Context");
        lua_newtable(vm);
        lua_pushcfunction(vm,get_yaw,"Context.get_yaw");lua_setfield(vm,-2,"get_yaw");
        lua_pushcfunction(vm,set_yaw,"Context.set_yaw");lua_setfield(vm,-2,"set_yaw");
        lua_pushcfunction(vm,wait_event,"Context.wait_event");lua_setfield(vm,-2,"wait_event");
        lua_pushcfunction(vm,wait_seconds,"Context.wait_seconds");lua_setfield(vm,-2,"wait_seconds");
        lua_setreadonly(vm,-1,true);lua_setfield(vm,-2,"__index");
        lua_setreadonly(vm,-1,true);lua_pop(vm,1);
        luaL_sandbox(vm);
    }
    ~Impl() {cancel_all_tasks();programs.clear();if(vm)lua_close(vm);world.release_script_runtime();}
    void thread() const { require(owner==std::this_thread::get_id(),"Script VM accessed from wrong thread"); }
    void budget() { deadline=std::chrono::steady_clock::now()+limits.call_time; }
    void protected_call(lua_State* s,int arguments,int returns) {
        budget();auto start=std::chrono::steady_clock::now();auto status=lua_pcall(s,arguments,returns,0);record(start,status!=0);
        if(status!=0) {
            const char* message=lua_tostring(s,-1); std::string result=message?message:"Luau failure";lua_settop(s,0);throw std::runtime_error(result);
        }
    }
    void record(std::chrono::steady_clock::time_point start,bool failed){auto elapsed=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());auto update=[&](ScriptMetrics& m){++m.calls;if(failed)++m.failures;m.last_microseconds=elapsed;m.total_microseconds+=elapsed;};update(metrics);if(running)update(running->metrics);}
    void drop_task(std::map<std::uint64_t,std::unique_ptr<Task>>::iterator it){lua_unref(vm,it->second->state_ref);lua_unref(vm,it->second->thread_ref);task_queue.erase(it);}
    void cancel_all_tasks(){while(!task_queue.empty())drop_task(task_queue.begin());events.clear();}
    void cancel_binding(StableId id){for(auto it=task_queue.begin();it!=task_queue.end();){auto entry=it++;if(entry->second->binding==id)drop_task(entry);}}
    void push_context(lua_State* s,EntityHandle h,StableId id){auto* c=static_cast<Context*>(lua_newuserdatatagged(s,sizeof(Context),1));*c={this,h,id};luaL_getmetatable(s,"DarkAngel.Context");lua_setmetatable(s,-2);}
    void push_config(lua_State* s,const Instance& instance){running->config.push(s,instance.config);std::set<const void*> seen;std::size_t count{};freeze(s,-1,0,seen,count);}
    StableId single(EntityHandle h) const {StableId result;for(const auto& [id,instance]:instances)if(instance.owner==h){require(!result,"Object has several bindings; use stable binding ID");result=id;}require(static_cast<bool>(result),"Object has no script binding");return result;}
    void writer(EntityHandle owner,StableId binding,StableId asset) const {if(!programs.at(asset)->descriptor.writes_transform)return;for(const auto& [id,i]:instances)if(id!=binding && i.owner==owner && i.enabled)require(!programs.at(i.asset)->descriptor.writes_transform,"Conflicting enabled transform writers");}
    void resume_tasks(double seconds){
        simulation_time+=seconds;auto signals=std::move(events);events.clear();std::size_t resumed{};
        for(auto& [token,task]:task_queue)if(!task->event.empty() && std::find(signals.begin(),signals.end(),task->event)!=signals.end())task->event_ready=true;
        for(auto it=task_queue.begin();it!=task_queue.end();){
            auto entry=it++;auto& task=*entry->second;
            if(!world.valid(task.owner) || !instances.contains(task.binding) || !instances.at(task.binding).enabled || task.generation!=programs.at(task.asset)->generation){drop_task(entry);continue;}
            if(resumed>=limits.resumes_per_tick)continue;
            if(!task.event.empty() && !task.event_ready)continue;
            if(task.wake>simulation_time)continue;
            auto* s=task.coroutine;auto& instance=instances.at(task.binding);running=programs.at(task.asset).get();int arguments{};auto checkpoint=world.script_checkpoint();
            try {
                running->config.validate_references(instance.config,world);
                // Keep the root table passed to a suspended task, refresh its declared fields.
                lua_getref(s,task.state_ref);running->schema.sync(s,-1,instance.data);lua_pop(s,1);
                if(!task.started){lua_getref(s,running->definition_ref);lua_getfield(s,-1,"task");lua_remove(s,-2);push_context(s,task.owner,task.binding);lua_getref(s,task.state_ref);push_config(s,instance);arguments=3;task.started=true;}
                task.event.clear();task.event_ready=false;task.wake=0;budget();auto start=std::chrono::steady_clock::now();writes=true;auto status=lua_resume(s,vm,arguments);writes=false;record(start,status!=0 && status!=LUA_YIELD);++resumed;
                if(status!=0 && status!=LUA_YIELD){const char* message=lua_tostring(s,-1);throw std::runtime_error(message?message:"Task failed");}
                lua_getref(s,task.state_ref);instance.data=running->schema.read(s,-1);lua_pop(s,1);
                if(status==0){drop_task(entry);continue;}
                require(!task.event.empty() || task.wake>simulation_time,"Tasks must yield through host-managed waits");
            }catch(const std::exception& e){writes=false;world.rollback_script(checkpoint);error=e.what();instance.enabled=false;drop_task(entry);}
        }
    }
    void validate_function(lua_State* s,const char* name,bool optional) {
        lua_getfield(s,-1,name);
        if(optional && lua_isnil(s,-1)) {lua_pop(s,1);return;}
        require(lua_isfunction(s,-1) && !lua_iscfunction(s,-1),"Expected declared Luau callback");
        lua_pop(s,1);
    }
    void freeze(lua_State* s,int index,unsigned depth,std::set<const void*>& seen,std::size_t& count) {
        require(depth<=16 && ++count<=4096,"Module export limit");index=lua_absindex(s,index);auto type=lua_type(s,index);
        if(type==LUA_TTABLE){
            auto ptr=lua_topointer(s,index);if(seen.contains(ptr))return;seen.insert(ptr);
            require(!lua_getmetatable(s,index),"Module exports cannot have metatables");
            lua_pushnil(s);while(lua_next(s,index)){freeze(s,-1,depth+1,seen,count);lua_pop(s,1);}lua_setreadonly(s,index,true);
        }else if(type==LUA_TFUNCTION){
            if(lua_iscfunction(s,index))return;
            auto ptr=lua_topointer(s,index);if(seen.contains(ptr))return;seen.insert(ptr);
            for(int i=1;lua_getupvalue(s,index,i);++i){freeze(s,-1,depth+1,seen,count);lua_pop(s,1);}
        }else require(type==LUA_TNIL || type==LUA_TBOOLEAN || type==LUA_TNUMBER || type==LUA_TSTRING,"Unsupported module export/captured value");
    }
    void load_module(Generation& g,const std::string& path,lua_State* destination) {
        if(auto it=g.loaded.find(path);it!=g.loaded.end()){lua_getref(destination,it->second.export_ref);return;}
        require(g.loading.insert(path).second,"Runtime module cycle");
        auto module=std::find_if(g.package.modules.begin(),g.package.modules.end(),[&](const auto& m){return m.path==path;});require(module!=g.package.modules.end(),"Missing cooked module");
        budget();auto* s=lua_newthread(vm);int ref=lua_ref(vm,-1);lua_pop(vm,1);g.references.push_back(ref);luaL_sandboxthread(s);
        auto import=std::make_unique<ImportContext>(ImportContext{this,&g,path});auto* context=import.get();g.imports.push_back(std::move(import));
        lua_pushlightuserdata(s,context);lua_pushcclosure(s,require_module,"DarkAngel.require",1);lua_setfield(s,LUA_GLOBALSINDEX,"require");lua_setreadonly(s,LUA_GLOBALSINDEX,true);
        if(luau_load(s,("@script/"+module->asset.text()+"/"+path).c_str(),module->bytecode.data(),module->bytecode.size(),0)!=0){const char* e=lua_tostring(s,-1);throw std::runtime_error(e?e:"Invalid cooked bytecode");}
        protected_call(s,0,1);require(lua_istable(s,-1),"Module must return immutable exports");
        std::set<const void*> seen;std::size_t count{};freeze(s,-1,0,seen,count);
        ref=lua_ref(s,-1);g.references.push_back(ref);lua_settop(s,0);g.loaded.emplace(path,Module{s,ref});g.loading.erase(path);lua_getref(destination,ref);
    }
    bool reload_package(const ScriptDescriptor& d,CookedScriptPackage package) {
        error.clear();
        try {
            require(world.phase()==Phase::Idle,"Reload requires idle safe point");
            require(static_cast<bool>(d.asset) && d.state_version>0 && std::isfinite(d.speed) && std::abs(d.speed)<=1e6,"Invalid script declaration");
            auto found=programs.find(d.asset);auto candidate_program=std::make_unique<Program>();auto& program=found==programs.end()?*candidate_program:*found->second;running=&program;
            auto generation=program.generation;auto& descriptor=program.descriptor;auto& schema=program.schema;
            require(!generation || d.authority==descriptor.authority,"Reload changed authority");require(!generation || d.state_version>=descriptor.state_version,"State version cannot go backwards");
            require(!generation || d.writes_transform==descriptor.writes_transform,"Reload changed writer contract; explicitly rebind");
            ScriptStateSchema next_schema(package.state_schema);
            ScriptStateSchema next_config(package.config_schema);
            require(d.config_schema.empty() || ScriptStateSchema(d.config_schema).canonical()==next_config.canonical(),"Descriptor/package config schema mismatch");
            require(d.state_schema.empty() || ScriptStateSchema(d.state_schema).canonical()==next_schema.canonical(),"Descriptor/package state schema mismatch");
            require(!generation || next_schema.canonical()==schema.canonical() || d.state_version>descriptor.state_version,"State schema change requires a version increment");
            auto root=std::find_if(package.modules.begin(),package.modules.end(),[&](const auto& m){return m.path==package.entry;});require(root!=package.modules.end() && root->asset==d.asset,"Entry asset identity mismatch");
            auto candidate=std::make_unique<Generation>();candidate->vm=vm;candidate->package=std::move(package);load_module(*candidate,candidate->package.entry,vm);lua_pop(vm,1);
            auto module=candidate->loaded.at(candidate->package.entry);auto* s=module.thread;
            lua_getref(s,module.export_ref);lua_pushnil(s);
            while(lua_next(s,-2)){require(lua_type(s,-2)==LUA_TSTRING,"Invalid behavior export key");auto key=std::string_view(lua_tostring(s,-2));require(key=="update" || key=="migrate" || key=="task" || key=="create","Undeclared behavior export");lua_pop(s,1);}
            validate_function(s,"update",false);validate_function(s,"migrate",true);validate_function(s,"task",true);validate_function(s,"create",true);lua_settop(s,0);
            auto staged=instances;
            for(auto& [binding,instance]:staged){
                if(instance.asset!=d.asset)continue;
                require(world.valid(instance.owner),"Reload instance is stale; detach before reload");
                if(d.state_version!=descriptor.state_version){lua_getref(s,module.export_ref);lua_getfield(s,-1,"migrate");lua_remove(s,-2);require(lua_isfunction(s,-1),"State version change requires migrate");lua_pushinteger(s,static_cast<int>(descriptor.state_version));schema.push(s,instance.data);protected_call(s,2,1);instance.data=next_schema.read(s,-1);lua_settop(s,0);}
                next_schema.validate(instance.data);
                next_config.validate_references(instance.config,world);
            }
            for(const auto& [binding,instance]:instances)if(instance.asset==d.asset)cancel_binding(binding);
            program.current=std::move(candidate);program.active=s;program.definition_ref=module.export_ref;descriptor=d;schema=std::move(next_schema);program.config=std::move(next_config);instances=std::move(staged);++program.generation;
            if(found==programs.end())programs.emplace(d.asset,std::move(candidate_program));if(!primary)primary=d.asset;running=nullptr;lua_gc(vm,LUA_GCCOLLECT,0);return true;
        }catch(const std::exception& e){running=nullptr;error=e.what();lua_settop(vm,0);lua_gc(vm,LUA_GCCOLLECT,0);return false;}
    }
};
ScriptRuntime::ScriptRuntime(World& w,ScriptLimits l):impl_(std::make_unique<Impl>(w,l)) {}
ScriptRuntime::~ScriptRuntime()=default;
bool ScriptRuntime::reload(const ScriptDescriptor& d,std::string_view source) {
    ScriptPackage package{"behavior",{{d.asset,"behavior",std::string(source),{}}},{},d.state_schema,d.config_schema};return reload(d,package);
}
bool ScriptRuntime::reload(const ScriptDescriptor& d,const ScriptPackage& package){auto& p=*impl_;p.thread();try {for(const auto& m:package.modules)require(m.source.size()<=p.limits.source_bytes,"Script source limit");return p.reload_package(d,cook_scripts(package));}catch(const std::exception& e){p.error=e.what();return false;}}
bool ScriptRuntime::load_cooked(const ScriptDescriptor& d,std::string_view artifact){auto& p=*impl_;p.thread();try{return p.reload_package(d,CookedScriptPackage::deserialize(artifact));}catch(const std::exception& e){p.error=e.what();return false;}}
void ScriptRuntime::attach(EntityHandle h) {
    auto& p=*impl_;p.thread();require(static_cast<bool>(p.primary),"No script loaded");auto& program=*p.programs.at(p.primary);
    attach(h,{p.world.read(h).id,p.primary,nlohmann::json{{"speed",program.descriptor.speed}}.dump(),true});
}
void ScriptRuntime::attach(EntityHandle h,const ScriptBinding& binding){auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle && p.world.valid(h) && static_cast<bool>(binding.id) && p.programs.contains(binding.asset),"Invalid binding/asset/owner");require(p.instances.size()<p.limits.instances && !p.instances.contains(binding.id),"Instance limit or duplicate binding ID");auto& program=*p.programs.at(binding.asset);if(binding.enabled)p.writer(h,binding.id,binding.asset);auto config=program.config.defaults();if(!binding.config_json.empty()){auto overrides=parse_script_data(binding.config_json);require(overrides.is_object(),"Config overrides must be a record");for(const auto& [name,value]:overrides.items()){require(config.contains(name),"Undeclared config override");config[name]=value;}}program.config.validate_references(config,p.world);auto data=program.schema.defaults();if(data.contains("angle") && data.at("angle").is_number())data["angle"]=p.world.read(h).transform.yaw;lua_getref(program.active,program.definition_ref);lua_getfield(program.active,-1,"create");lua_remove(program.active,-2);
    if(lua_isfunction(program.active,-1)){
        p.running=&program;p.push_context(program.active,h,binding.id);program.config.push(program.active,config);
        std::set<const void*> seen;std::size_t count{};p.freeze(program.active,-1,0,seen,count);
        try{p.protected_call(program.active,2,1);data=program.schema.read(program.active,-1);p.running=nullptr;lua_settop(program.active,0);}
        catch(...){p.running=nullptr;lua_settop(program.active,0);throw;}
    }else lua_settop(program.active,0);
    program.schema.validate(data);p.instances.emplace(binding.id,Impl::Instance{h,binding.asset,std::move(data),std::move(config),binding.enabled});}
void ScriptRuntime::detach(EntityHandle h) {auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle,"Detach requires idle");for(auto it=p.instances.begin();it!=p.instances.end();){auto entry=it++;if(entry->second.owner==h){p.cancel_binding(entry->first);p.instances.erase(entry);}}}
void ScriptRuntime::detach_binding(StableId id){auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle && p.instances.contains(id),"Invalid detach/safe point");p.cancel_binding(id);p.instances.erase(id);}
void ScriptRuntime::enable_binding(StableId id,bool enabled){auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle,"Enable requires safe point");auto& instance=p.instances.at(id);require(p.world.valid(instance.owner),"Stale binding owner");if(enabled)p.writer(instance.owner,id,instance.asset);else p.cancel_binding(id);instance.enabled=enabled;}
void ScriptRuntime::configure_binding(StableId id,std::string_view json){auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle,"Configure requires safe point");auto& instance=p.instances.at(id);auto config=parse_script_data(json);p.programs.at(instance.asset)->config.validate_references(config,p.world);p.cancel_binding(id);instance.config=std::move(config);}
std::vector<ScriptBindingInfo> ScriptRuntime::bindings() const{auto& p=*impl_;p.thread();std::vector<ScriptBindingInfo> out;for(const auto& [id,i]:p.instances){const auto& program=*p.programs.at(i.asset);out.push_back({id,i.asset,i.owner,i.enabled,program.generation,program.metrics});}return out;}
std::string ScriptRuntime::binding_state(StableId id) const{auto& p=*impl_;p.thread();const auto& i=p.instances.at(id);require(p.world.valid(i.owner),"Stale binding owner");const auto& program=*p.programs.at(i.asset);return nlohmann::json{{"version",program.descriptor.state_version},{"fields",program.schema.stable_fields(i.data)}}.dump();}
void ScriptRuntime::tick(double seconds) {
    auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Script && std::isfinite(seconds) && seconds>=0 && seconds<=1,"Invalid script tick");
    for(auto it=p.instances.begin();it!=p.instances.end();) {
        if(!p.world.valid(it->second.owner)) {p.cancel_binding(it->first);it=p.instances.erase(it);continue;}
        auto& [binding,instance]=*it++;if(!instance.enabled)continue;p.running=p.programs.at(instance.asset).get();auto& program=*p.running;
        auto checkpoint=p.world.script_checkpoint();try {
            program.config.validate_references(instance.config,p.world);
            lua_State* s=program.active;lua_settop(s,0);lua_getref(s,program.definition_ref);lua_getfield(s,-1,"update");lua_remove(s,-2);
            p.push_context(s,instance.owner,binding);
            program.schema.push(s,instance.data);int state_ref=lua_ref(s,-1);
            p.push_config(s,instance);
            lua_pushnumber(s,seconds);p.writes=true;
            try {p.protected_call(s,4,0);p.writes=false;lua_getref(s,state_ref);instance.data=program.schema.read(s,-1);lua_unref(s,state_ref);lua_settop(s,0);}
            catch(...) {p.writes=false;lua_unref(s,state_ref);lua_settop(s,0);throw;}
        } catch(const std::exception& e) {p.world.rollback_script(checkpoint);instance.enabled=false;p.error=e.what(); }
    }
    p.resume_tasks(seconds);
    p.running=nullptr;
}
std::uint64_t ScriptRuntime::start_task(EntityHandle h){impl_->thread();return start_binding_task(impl_->single(h));}
std::uint64_t ScriptRuntime::start_binding_task(StableId binding){auto& p=*impl_;p.thread();auto& instance=p.instances.at(binding);auto& program=*p.programs.at(instance.asset);require(p.world.phase()==Phase::Idle && p.world.valid(instance.owner) && instance.enabled,"Invalid task owner/safe point");require(p.task_queue.size()<p.limits.tasks,"Task queue limit");
    lua_getref(program.active,program.definition_ref);lua_getfield(program.active,-1,"task");bool exists=lua_isfunction(program.active,-1);lua_settop(program.active,0);require(exists,"Behavior has no declared task");
    auto* s=lua_newthread(p.vm);int ref=lua_ref(p.vm,-1);lua_pop(p.vm,1);program.schema.push(s,instance.data);int state_ref=lua_ref(s,-1);lua_pop(s,1);
    auto task=std::make_unique<Impl::Task>(Impl::Task{++p.next_task,program.generation,instance.owner,binding,instance.asset,s,ref,state_ref});lua_setthreaddata(s,task.get());auto token=task->token;p.task_queue.emplace(token,std::move(task));return token;
}
void ScriptRuntime::cancel_task(std::uint64_t token){auto& p=*impl_;p.thread();require(p.world.phase()==Phase::Idle,"Task cancel requires safe point");auto it=p.task_queue.find(token);require(it!=p.task_queue.end(),"Unknown task token");p.drop_task(it);}
void ScriptRuntime::signal_event(std::string_view event){auto& p=*impl_;p.thread();require(!event.empty() && event.size()<=64 && p.events.size()<p.limits.events,"Event limit");p.events.emplace_back(event);}
std::vector<ScriptTaskInfo> ScriptRuntime::tasks() const{auto& p=*impl_;p.thread();std::vector<ScriptTaskInfo> result;for(const auto& [token,task]:p.task_queue)result.push_back({token,task->generation,task->owner,!task->event.empty()?"event:"+task->event:task->wake>p.simulation_time?"time":"ready"});return result;}
ScriptMetrics ScriptRuntime::metrics() const{impl_->thread();return impl_->metrics;}
std::uint64_t ScriptRuntime::generation() const {impl_->thread();return impl_->primary?impl_->programs.at(impl_->primary)->generation:0;}
double ScriptRuntime::state(EntityHandle h) const {impl_->thread();require(impl_->world.valid(h),"Stale script state handle");return impl_->instances.at(impl_->single(h)).data.at("angle").get<double>();}
std::string ScriptRuntime::state_json(EntityHandle h) const {impl_->thread();return binding_state(impl_->single(h));}
bool ScriptRuntime::enabled(EntityHandle h) const {impl_->thread();for(const auto& [id,i]:impl_->instances)if(i.owner==h && i.enabled)return true;return false;}
std::size_t ScriptRuntime::instance_count() const {impl_->thread();return impl_->instances.size();}
std::size_t ScriptRuntime::memory_bytes() const {impl_->thread();return impl_->allocation.used;}
std::string ScriptRuntime::diagnostic() const {impl_->thread();return impl_->error;}
}
