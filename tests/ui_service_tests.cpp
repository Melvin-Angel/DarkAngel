#include <darkangel/ui_service.hpp>
#include <darkangel/ui_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/ui_runtime.hpp>
#include <fstream>
#include <RmlUi/Core/RenderInterface.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <set>
using namespace darkangel;using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
class Recorder final:public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,Rml::Span<const int>)override{live.insert(next);return next++;}
    void RenderGeometry(Rml::CompiledGeometryHandle handle,Rml::Vector2f,Rml::TextureHandle)override{check(live.contains(handle),"Preview drew released geometry");++drawn;}
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle)override{live.erase(handle);}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size,const Rml::String& source)override{auto image=UiRuntime::image(source);if(!image)return 0;size={image->width,image->height};return next++;}
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,Rml::Vector2i)override{return next++;}
    void ReleaseTexture(Rml::TextureHandle)override{}
    void EnableScissorRegion(bool)override{}
    void SetScissorRegion(Rml::Rectanglei)override{}
    std::set<std::uintptr_t> live;std::uintptr_t next{1};unsigned drawn{};
};
}
int main(){try{
 Recorder renderer;std::ifstream file(std::filesystem::path(DAE_SOURCE_DIR)/"content/ui/fonts/Roboto-Medium.ttf",std::ios::binary);std::string bytes(std::istreambuf_iterator<char>(file),{});file.close();check(!bytes.empty(),"Packaged font");
 std::vector<std::string> requests;
 UiService service(renderer,{"HUD",bytes},[](std::string_view id){auto t=ui_template((id=="script"||id=="spawn")?"menu":id);UiDocumentSource s;s.markup=t.markup;s.style=t.style;
 if(id=="spawn")s.script="return {on_open=function(ui,h) ui:open(\"marker\",\"passive\") end}";
 if(id=="script")s.script=R"lua(return {on_open=function(ui,h) ui:set(h,"name","Script") end,on_command=function(ui,h,c) if c=="menu.start" then return ui:request("play", "") end return false end})lua";return s;},[&](auto kind,auto value){requests.emplace_back(std::string(kind)+":"+std::string(value));return true;});
 auto hud=service.open("hud",UiService::Input::Passive,"scene",1280,720);auto a=service.attach("marker","a",200,80),b=service.attach("marker","b",200,80);service.model(a).name="Archer";service.model(b).name="Guard";service.place(a,10,20,true);service.place(b,30,40,true);service.update();check(service.text(a,"nameplate-name")=="Archer"&&service.text(b,"nameplate-name")=="Guard","Independent instances");
 unsigned draws=0;service.render([&](auto& view,float,float,float,float){view.render();++draws;});check(draws==3,"Layer and anchors draw");
 check(!service.key(UiKey::Confirm),"Passive UI does not consume gameplay");auto modal=service.open("menu",UiService::Input::Modal,"menu",1280,720);service.key(UiKey::Confirm);service.update();auto commands=service.drain_commands();check(commands.size()==1&&commands[0].command=="menu.start","Modal confirm");service.key(UiKey::Down);service.key(UiKey::Confirm);service.update();commands=service.drain_commands();check(commands.size()==1&&commands[0].command=="menu.settings","Modal focus navigation");service.key(UiKey::Cancel);check(!service.valid(modal)&&!service.modal_open(),"Cancel closes modal");service.update();service.drain_commands();
 auto script=service.open("script",UiService::Input::Modal,"script",1280,720);service.key(UiKey::Confirm);service.update();check(requests==std::vector<std::string>{"play:"}&&service.drain_commands().empty(),"Luau bridge handles command");check(service.problems().empty(),"Luau callback diagnostics");service.close(script);service.close_scope("a");check(!service.valid(a)&&service.valid(b),"Scope cleanup");auto next=service.attach("marker","next",200,80);check(next!=a&&!service.valid(a),"Generation rejects stale handle");rejects([&]{service.model(a);},"Stale model rejected");service.close(a);check(service.valid(next),"Stale close harmless");service.close_scope("scene");check(!service.valid(hud),"Scene cleanup");
 {UiService other(renderer,{"HUD",bytes},[](auto){auto t=ui_template("marker");UiDocumentSource s;s.markup=t.markup;s.style=t.style;return s;});auto h=other.attach("marker","other",200,80);check(!other.valid(next)&&!service.valid(h),"Viewport handles cannot cross services");}
 auto parent=service.open("spawn",UiService::Input::Modal,"nested",1280,720);const auto before=service.screens();check(before>=1,"Luau opens child document");service.close_scope("nested");check(!service.valid(parent)&&service.screens()+1==before,"Script-created child inherits owning scope");
 {auto loader=[](auto){auto t=ui_template("marker");UiDocumentSource s;s.markup=t.markup;s.style=t.style;return s;};UiService one(renderer,{"HUD",bytes},loader),two(renderer,{"HUD",bytes},loader);auto a=one.attach("marker","a",200,80),b=two.attach("marker","b",200,80);check(a!=b&&!one.valid(b)&&!two.valid(a),"First handles of separate viewports differ");}
 std::cout<<"UiService tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
