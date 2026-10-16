#include <darkangel/hud.hpp>
#include <darkangel/ui_assets.hpp>
#include <darkangel/assets.hpp>
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
    Rml::TextureHandle LoadTexture(Rml::Vector2i&,const Rml::String&)override{return 0;}
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>,Rml::Vector2i)override{return next++;}
    void ReleaseTexture(Rml::TextureHandle)override{}
    void EnableScissorRegion(bool)override{}
    void SetScissorRegion(Rml::Rectanglei)override{}
    std::set<std::uintptr_t> live;std::uintptr_t next{1};unsigned drawn{};
};
Json document(const char* purpose,AssetId id=AssetId::random()){auto t=ui_template(purpose);return {{"schema",1},{"kind","ui"},{"asset",id.text()},{"name",std::string("Test ")+purpose},{"purpose",purpose},{"surfaces",t.surfaces},{"size",{1280,720}},{"markup",t.markup},{"style",t.style}};}
}
int main(){try{
    const std::filesystem::path font="C:/Windows/Fonts/segoeui.ttf";
    // Templates are valid documents; self-contained rules are enforced.
    for(const char* purpose:{"hud","marker","menu","puzzle"})check(decode_ui_document(document(purpose).dump()).purpose==purpose,"Template decodes");
    {auto bad=document("menu");bad["markup"]=bad["markup"].get<std::string>()+"<script>x</script>";rejects([&]{decode_ui_document(bad.dump());},"Scripts are refused");}
    {auto bad=document("hud");bad["style"]="@import 'other.rcss';";rejects([&]{decode_ui_document(bad.dump());},"External style imports are refused");}
    {auto bad=document("hud");bad["surfaces"]=Json::array({"screen","screen"});rejects([&]{decode_ui_document(bad.dump());},"Duplicate surfaces are refused");}
    {auto bad=document("hud");bad["purpose"]="banner";rejects([&]{decode_ui_document(bad.dump());},"Unknown purpose refused");}
    Recorder renderer;bool evicted=false;
    {auto preview=std::make_unique<UiDocumentPreview>(renderer,font,1280,720,[&]{evicted=true;});
     // Each template loads, binds the sample model and draws.
     for(const char* purpose:{"hud","marker","menu","puzzle"}){auto t=ui_template(purpose);check(preview->load(t.markup,t.style)&&!preview->stale(),"Template preview loads");UiPreviewSample sample;preview->update(sample);renderer.drawn=0;preview->render();check(renderer.drawn>0,"Template preview draws");}
     {auto t=ui_template("hud");preview->load(t.markup,t.style);UiPreviewSample sample;sample.stamina=25;preview->update(sample);check(preview->text("stamina-text")=="25 / 100","Sample values reach the document");}
     {auto t=ui_template("marker");preview->load(t.markup,t.style);UiPreviewSample sample;sample.name="Archer";preview->update(sample);check(preview->text("nameplate-name")=="Archer","Sample name reaches the marker");}
     // Clicks run command callbacks, logged only.
     {auto t=ui_template("menu");preview->load(t.markup,t.style);preview->update({});auto box=preview->box("menu-start");check(box.has_value(),"Menu button laid out");
      const int x=int(box->x+box->width/2),y=int(box->y+box->height/2);preview->pointer(x,y,true,false);preview->pointer(x,y,false,true);auto commands=preview->drain_commands();check(commands.size()==1&&commands[0]=="menu.start","Click logs the command");}
     // A broken document keeps the last good one, marked stale.
     {auto t=ui_template("puzzle");check(preview->load(t.markup,t.style),"Puzzle loads");check(!preview->load("<rml><head></head><body><div></body></rml","")&&preview->stale()&&!preview->problems().empty(),"Broken document is refused and the last good one stays");
      UiPreviewSample sample;sample.flag=true;preview->update(sample);check(preview->text("puzzle-state")=="Solved","Stale preview still shows the last good document");}
     // A Hud takes RmlUi from the preview (Play wins); the owner is told and drops it.
     std::unique_ptr<Hud> hud;bool dropped=false;
     {auto evict=[&]{evicted=true;};(void)evict;}
     auto owner=std::make_shared<std::unique_ptr<UiDocumentPreview>>(std::move(preview));
     owner->reset();evicted=false;
     *owner=std::make_unique<UiDocumentPreview>(renderer,font,640,360,[owner,&dropped]{owner->reset();dropped=true;});
     hud=std::make_unique<Hud>(renderer,std::filesystem::path(DAE_SOURCE_DIR)/"games/AshenRoots/ui/hud.rml",font,1280,720);
     check(dropped&&!*owner,"Creating a Hud evicts the preview through its owner");
     rejects([&]{UiDocumentPreview again(renderer,font,640,360,[]{});},"No preview while the Hud runs");
     hud.reset();owner->reset();}
    // UI documents cook like the other native sources.
    {const auto folder=std::filesystem::temp_directory_path()/("ui-tests-"+AssetId::random().text());std::filesystem::create_directories(folder/"sources/ui/hud");
     {AssetService assets(folder/"sources",folder/"cache");assets.scan();check(assets.create_native("ui/hud/Stamina.daui",document("hud").dump()).changed,"UI document cooks");
      auto bad=document("menu");bad["markup"]="<div>not rml</div>";rejects([&]{assets.create_native("ui/menus/Bad.daui",bad.dump());},"Invalid UI document refused at cook");}
     std::error_code ignored;std::filesystem::remove_all(folder,ignored);}
    std::cout<<"R6.1 UI documents: templates, self-contained rules, preview load/bind/draw, sample values, command log, stale retention, Hud eviction and cook passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
