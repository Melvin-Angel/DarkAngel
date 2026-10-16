#include <darkangel/ui_service.hpp>
#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
constexpr std::size_t document_limit=64;
std::atomic<std::uint64_t> next_generation{1};
}
struct UiService::Impl {
    std::shared_ptr<UiRuntime> runtime=UiRuntime::acquire();UiService* owner{};Rml::RenderInterface& renderer;UiFont fallback;Loader loader;Bridge bridge;
    enum class Kind {Screen,Modal,Anchor};
    struct Slot {std::uint32_t generation{};std::unique_ptr<UiDocumentView> view;std::string document,scope;Kind kind{};UiPreviewSample model;float x{},y{};bool visible{true},close_on_cancel{true};int width{},height{};std::uint64_t order{};};
    std::vector<Slot> slots;std::vector<std::uint32_t> modal_stack;std::uint64_t next_order{};
    std::vector<Command> pending,unhandled;std::string problems;std::map<std::string,UiDocumentSource> sources;std::set<std::string> registered;
    // The local Luau presentation host: one sandboxed VM, one module table per document that has a script.
    struct Allocation {std::size_t used{},limit{8*1024*1024};} allocation;
    lua_State* vm{};unsigned depth{};std::string active_scope;std::map<std::string,int> modules;int ui_ref{LUA_NOREF};std::chrono::steady_clock::time_point deadline;
    Impl(Rml::RenderInterface& r,UiFont f,Loader l,Bridge b):renderer(r),fallback(std::move(f)),loader(std::move(l)),bridge(std::move(b)){}
    ~Impl(){if(vm)lua_close(vm);}
    void note(const std::string& text){if(problems.size()<8192)problems+=text+"\n";}
    Slot* find(Handle handle){if(handle.slot>=slots.size())return nullptr;auto& slot=slots[handle.slot];return slot.view&&slot.generation==handle.generation?&slot:nullptr;}
    const Slot* find(Handle handle)const{return const_cast<Impl*>(this)->find(handle);}
    static double pack(Handle handle){return double(handle.generation)*65536.0+double(handle.slot);}
    static Handle unpack(double value){if(!std::isfinite(value)||value<65536||value>281474976710655.0||std::floor(value)!=value)return {};const auto whole=static_cast<std::uint64_t>(value);return {static_cast<std::uint32_t>(whole%65536),static_cast<std::uint32_t>(whole/65536)};}
    const UiDocumentSource& source(const std::string& document){
        if(auto found=sources.find(document);found!=sources.end())return found->second;require(sources.size()<64,"UI document cache limit");
        auto loaded=loader(document);for(auto& font:loaded.fonts)if(registered.insert("font:"+font.family).second)runtime->register_font(font.family,font.bytes);
        for(auto& image:loaded.images)if(registered.insert(image.url).second)runtime->register_image(image.url,image.image);
        return sources.emplace(document,std::move(loaded)).first->second;}
    // ---- Luau presentation scripts ----
    static Impl* self(lua_State* s){return static_cast<Impl*>(lua_tolightuserdata(s,lua_upvalueindex(1)));}
    static int ui_open(lua_State* s){auto* impl=self(s);const std::string document=luaL_checkstring(s,2);const std::string input=luaL_optstring(s,3,"modal");
        if(input!="modal"&&input!="passive")luaL_error(s,"ui:open input is \"modal\" or \"passive\"");
        try{auto handle=impl->owner->open(document,input=="modal"?Input::Modal:Input::Passive,impl->active_scope,1280,720);lua_pushnumber(s,pack(handle));}catch(const std::exception& error){luaL_error(s,"ui:open failed: %s",error.what());}return 1;}
    static int ui_close(lua_State* s){auto* impl=self(s);const auto h=unpack(luaL_checknumber(s,2));if(auto* slot=impl->find(h);slot&&slot->scope==impl->active_scope)impl->owner->close(h);return 0;}
    static int ui_set(lua_State* s){auto* impl=self(s);auto* slot=impl->find(unpack(luaL_checknumber(s,2)));if(!slot||slot->scope!=impl->active_scope)luaL_error(s,"ui:set on a closed or foreign document");const std::string field=luaL_checkstring(s,3);auto& model=slot->model;
        if(field=="name"||field=="prompt"){std::string text=luaL_checkstring(s,4);if(text.size()>64)luaL_error(s,"ui:set text is at most 64 characters");(field=="name"?model.name:model.prompt)=text;}
        else if(field=="flag")model.flag=lua_toboolean(s,4)!=0;
        else if(field=="health"||field=="stamina"||field=="cooldown"){const double value=luaL_checknumber(s,4);if(!(value>=0&&value<=1e9))luaL_error(s,"ui:set number out of range");(field=="health"?model.health:field=="stamina"?model.stamina:model.cooldown)=value;}
        else luaL_error(s,"ui:set fields are name, prompt, flag, health, stamina and cooldown");return 0;}
    static int ui_request(lua_State* s){auto* impl=self(s);const std::string kind=luaL_checkstring(s,2),value=luaL_optstring(s,3,"");if(kind.size()>64||value.size()>256)luaL_error(s,"ui:request arguments are bounded");lua_pushboolean(s,impl->bridge&&impl->bridge(kind,value));return 1;}
    static void* allocate(void* user,void* ptr,std::size_t old,std::size_t size){auto& a=*static_cast<Allocation*>(user);if(!ptr)old=0;if(!size){std::free(ptr);a.used-=old;return nullptr;}if(size>a.limit||a.used-old>a.limit-size)return nullptr;auto* next=std::realloc(ptr,size);if(next)a.used=a.used-old+size;return next;}
    static void interrupt(lua_State* s,int gc){if(gc>=0)return;auto* impl=static_cast<Impl*>(lua_callbacks(s)->userdata);if(std::chrono::steady_clock::now()>impl->deadline)luaL_error(s,"UI script exceeded its time budget");}
    void start_vm(){
        if(vm)return;vm=lua_newstate(allocate,&allocation);require(vm!=nullptr,"UI script VM");lua_callbacks(vm)->userdata=this;lua_callbacks(vm)->interrupt=interrupt;luaL_openlibs(vm);
        lua_newtable(vm);
        auto method=[&](const char* name,lua_CFunction function){lua_pushlightuserdata(vm,this);lua_pushcclosure(vm,function,name,1);lua_setfield(vm,-2,name);};
        method("open",ui_open);method("close",ui_close);method("set",ui_set);method("request",ui_request);
        ui_ref=lua_ref(vm,-1);lua_pop(vm,1);luaL_sandbox(vm);}
    // The module table of a document's script, compiled and run once; LUA_NOREF when it has none.
    int module(const std::string& document){
        if(auto found=modules.find(document);found!=modules.end())return found->second;const auto& script=source(document).script;if(script.empty())return modules[document]=LUA_NOREF;
        start_vm();require(script.size()<=16*1024,"UI scripts are at most 16 KB");const auto bytecode=Luau::compile(script);
        auto* thread=lua_newthread(vm);luaL_sandboxthread(thread);int result=LUA_NOREF;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(50);
        if(bytecode.empty()||bytecode[0]==0||luau_load(thread,("@ui/"+document).c_str(),bytecode.data(),bytecode.size(),0)!=0)note("UI script of "+document+" does not compile: "+(bytecode.size()>1?bytecode.substr(1):std::string("empty")));
        else if(lua_pcall(thread,0,1,0)!=0){note("UI script of "+document+" failed: "+std::string(lua_tostring(thread,-1)?lua_tostring(thread,-1):"error"));}
        else if(!lua_istable(thread,-1))note("UI script of "+document+" must return a table of callbacks");
        else{lua_xmove(thread,vm,1);result=lua_ref(vm,-1);lua_pop(vm,1);}
        lua_pop(vm,1);return modules[document]=result;}
    // Calls module[name](ui, handle[, command]); returns whether it returned true.
    bool call(const std::string& document,const char* name,Handle handle,const std::string* command=nullptr){
        if(depth>=8){note("UI callback recursion limit");return false;}
        const int reference=module(document);if(reference==LUA_NOREF)return false;
        const auto previous_scope=active_scope;active_scope=find(handle)?find(handle)->scope:std::string{};++depth;
        lua_getref(vm,reference);lua_getfield(vm,-1,name);if(!lua_isfunction(vm,-1)){lua_pop(vm,2);--depth;active_scope=previous_scope;return false;}
        lua_getref(vm,ui_ref);lua_pushnumber(vm,pack(handle));int arguments=2;if(command){lua_pushstring(vm,command->c_str());++arguments;}
        deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(20);bool handled=false;
        if(lua_pcall(vm,arguments,1,0)!=0){note("UI script "+std::string(name)+" of "+document+" failed: "+std::string(lua_tostring(vm,-1)?lua_tostring(vm,-1):"error"));}
        else handled=lua_toboolean(vm,-1)!=0;
        lua_pop(vm,2);--depth;active_scope=previous_scope;return handled;}
};
UiService::UiService(Rml::RenderInterface& renderer,UiFont fallback,Loader loader,Bridge bridge):impl_(std::make_unique<Impl>(renderer,std::move(fallback),std::move(loader),std::move(bridge))){impl_->owner=this;require(bool(impl_->loader),"A UI service needs a document loader");}
UiService::~UiService()=default;
namespace {
UiService::Handle add(UiService::Impl& p,std::string_view document,UiService::Impl::Kind kind,std::string scope,int width,int height){
    require(!document.empty()&&document.size()<=64&&scope.size()<=96,"UI document and scope names are bounded");
    std::size_t live=0;for(const auto& slot:p.slots)if(slot.view)++live;require(live<document_limit,"Open UI document limit (64)");
    const std::string name(document);const auto& source=p.source(name);
    const std::string surface=kind==UiService::Impl::Kind::Anchor?"world_anchor":"screen";require(std::find(source.surfaces.begin(),source.surfaces.end(),surface)!=source.surfaces.end(),"UI document does not permit the requested surface");
    auto view=std::make_unique<UiDocumentView>(p.renderer,p.fallback,std::max(1,width),std::max(1,height));
    if(!view->load(source.markup,source.style,source.binding))throw std::runtime_error("UI document "+name+" does not load: "+view->problems());
    std::size_t index=0;while(index<p.slots.size()&&p.slots[index].view)++index;if(index==p.slots.size())p.slots.emplace_back();require(index<65536,"UI slot limit");
    const auto generation=next_generation.fetch_add(1);require(generation<=UINT32_MAX,"UI generation exhausted");auto& slot=p.slots[index];slot.generation=std::uint32_t(generation);slot.view=std::move(view);slot.document=name;slot.scope=std::move(scope);slot.kind=kind;slot.model={};slot.visible=kind!=UiService::Impl::Kind::Anchor;slot.width=width;slot.height=height;slot.order=p.next_order++;
    UiService::Handle handle{static_cast<std::uint32_t>(index),slot.generation};
    if(kind==UiService::Impl::Kind::Modal)p.modal_stack.push_back(handle.slot);
    p.call(name,"on_open",handle);return handle;}
}
UiService::Handle UiService::open(std::string_view document,Input input,std::string scope,int width,int height){return add(*impl_,document,input==Input::Modal?Impl::Kind::Modal:Impl::Kind::Screen,std::move(scope),width,height);}
UiService::Handle UiService::attach(std::string_view document,std::string scope,int width,int height){return add(*impl_,document,Impl::Kind::Anchor,std::move(scope),width,height);}
void UiService::close(Handle handle){auto* slot=impl_->find(handle);if(!slot)return;slot->view.reset();slot->generation=0;auto& stack=impl_->modal_stack;stack.erase(std::remove(stack.begin(),stack.end(),handle.slot),stack.end());}
void UiService::close_scope(std::string_view scope){for(std::uint32_t index=0;index<impl_->slots.size();++index){auto& slot=impl_->slots[index];if(slot.view&&slot.scope==scope)close({index,slot.generation});}}
bool UiService::valid(Handle handle)const{return impl_->find(handle)!=nullptr;}
UiPreviewSample& UiService::model(Handle handle){auto* slot=impl_->find(handle);require(slot!=nullptr,"Stale UI handle");return slot->model;}
void UiService::place(Handle handle,float x,float y,bool visible){if(auto* slot=impl_->find(handle)){slot->x=x;slot->y=y;slot->visible=visible;}}
void UiService::resize_screens(int width,int height){for(auto& slot:impl_->slots)if(slot.view&&slot.kind!=Impl::Kind::Anchor&&(slot.width!=width||slot.height!=height)){slot.width=width;slot.height=height;slot.view->resize(std::max(1,width),std::max(1,height));}}
bool UiService::modal_open()const{return !impl_->modal_stack.empty();}
std::optional<UiService::Handle> UiService::top_modal()const{if(impl_->modal_stack.empty())return {};const auto index=impl_->modal_stack.back();return Handle{index,impl_->slots[index].generation};}
void UiService::set_input(Handle h,Input input,bool close_on_cancel){auto* slot=impl_->find(h);require(slot&&slot->kind!=Impl::Kind::Anchor,"UI screen handle required");slot->close_on_cancel=close_on_cancel;const auto kind=input==Input::Modal?Impl::Kind::Modal:Impl::Kind::Screen;if(slot->kind==kind)return;auto& stack=impl_->modal_stack;std::erase(stack,h.slot);slot->kind=kind;if(kind==Impl::Kind::Modal)stack.push_back(h.slot);}
void UiService::update_owner(Handle h,const OwnerUiModel& model){auto* slot=impl_->find(h);require(slot!=nullptr,"Stale owner UI handle");slot->view->update_owner(model);}
void UiService::update_inventory(Handle h,const InventoryUiModel& model){auto* slot=impl_->find(h);require(slot!=nullptr,"Stale inventory UI handle");slot->view->update_inventory(model);}
std::vector<InventoryUiAction> UiService::drain_inventory_actions(Handle h){auto* slot=impl_->find(h);return slot?slot->view->drain_inventory_actions():std::vector<InventoryUiAction>{};}
void UiService::update_altar(Handle h,const AltarUiModel& model){auto* slot=impl_->find(h);require(slot!=nullptr,"Stale altar UI handle");slot->view->update_altar(model);}
std::vector<AltarUiAction> UiService::drain_altar_actions(Handle h){auto* slot=impl_->find(h);return slot?slot->view->drain_altar_actions():std::vector<AltarUiAction>{};}
std::optional<UiBox> UiService::box(Handle h,const std::string& id)const{auto* slot=impl_->find(h);return slot?slot->view->box(id):std::optional<UiBox>{};}
std::vector<UiService::Command> UiService::drain_commands(Handle h){std::vector<Command> result;for(auto it=impl_->unhandled.begin();it!=impl_->unhandled.end();)if(it->handle==h){result.push_back(std::move(*it));it=impl_->unhandled.erase(it);}else ++it;return result;}
bool UiService::key(UiKey key){
    if(impl_->modal_stack.empty())return false;const std::uint32_t index=impl_->modal_stack.back();auto& slot=impl_->slots[index];const Handle handle{index,slot.generation};
    if(key==UiKey::Cancel){impl_->unhandled.push_back({handle,slot.document,"ui.cancel"});if(slot.close_on_cancel)close(handle);else set_input(handle,Input::Passive,false);return true;}
    slot.view->key(key);return true; // a modal owns navigation even where it has nothing to focus
}
void UiService::pointer(int x,int y,bool pressed,bool released,float wheel){
    if(!impl_->modal_stack.empty()){impl_->slots[impl_->modal_stack.back()].view->pointer(x,y,pressed,released,wheel);return;}
    for(auto& slot:impl_->slots)if(slot.view&&slot.kind==Impl::Kind::Screen)slot.view->pointer(x,y,pressed,released,wheel);}
void UiService::update(){
    for(std::uint32_t index=0;index<impl_->slots.size();++index){auto& slot=impl_->slots[index];if(!slot.view)continue;slot.view->update(slot.model);
        for(auto& command:slot.view->drain_commands())impl_->pending.push_back({{index,slot.generation},slot.document,std::move(command)});}
    // Scripts may open or close documents while handling commands, so the batch is taken first.
    auto batch=std::move(impl_->pending);impl_->pending.clear();
    for(auto& command:batch)if(valid(command.handle)&&!impl_->call(command.document,"on_command",command.handle,&command.command))impl_->unhandled.push_back(std::move(command));
    if(impl_->unhandled.size()>256)impl_->unhandled.erase(impl_->unhandled.begin(),impl_->unhandled.begin()+(impl_->unhandled.size()-256));
}
std::vector<UiService::Command> UiService::drain_commands(){auto result=std::move(impl_->unhandled);impl_->unhandled.clear();return result;}
void UiService::render(const std::function<void(UiDocumentView&,float,float,float,float)>& draw){
    std::vector<const Impl::Slot*> screens,anchors;for(const auto& slot:impl_->slots)if(slot.view&&slot.visible){if(slot.kind==Impl::Kind::Screen)screens.push_back(&slot);else if(slot.kind==Impl::Kind::Anchor)anchors.push_back(&slot);}
    std::sort(screens.begin(),screens.end(),[](auto* a,auto* b){return a->order<b->order;});
    for(auto* slot:screens)draw(*slot->view,0,0,float(slot->width),float(slot->height));
    for(auto* slot:anchors)draw(*slot->view,slot->x,slot->y,float(slot->width),float(slot->height));
    for(auto index:impl_->modal_stack){const auto& slot=impl_->slots[index];draw(*slot.view,0,0,float(slot.width),float(slot.height));}
}
std::size_t UiService::screens()const{return std::size_t(std::count_if(impl_->slots.begin(),impl_->slots.end(),[](const auto& slot){return slot.view&&slot.kind==Impl::Kind::Screen;}));}
std::size_t UiService::anchors()const{return std::size_t(std::count_if(impl_->slots.begin(),impl_->slots.end(),[](const auto& slot){return slot.view&&slot.kind==Impl::Kind::Anchor;}));}
std::size_t UiService::modals()const{return impl_->modal_stack.size();}
std::string UiService::text(Handle handle,const std::string& id)const{const auto* slot=impl_->find(handle);return slot?slot->view->text(id):std::string{};}
std::string UiService::problems()const{return impl_->problems;}
}
