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
Json document(const char* purpose,AssetId id=AssetId::random()){auto t=ui_template(purpose);return {{"schema",1},{"kind","ui"},{"asset",id.text()},{"name",std::string("Test ")+purpose},{"purpose",purpose},{"surfaces",t.surfaces},{"size",{1280,720}},{"markup",t.markup},{"style",t.style}};}
}
int main(){try{
    const std::filesystem::path font=std::filesystem::path(DAE_SOURCE_DIR)/"content/ui/fonts/Roboto-Medium.ttf";
    // Templates are valid documents; self-contained rules are enforced.
    for(const char* purpose:{"hud","marker","menu","puzzle"})check(decode_ui_document(document(purpose).dump()).purpose==purpose,"Template decodes");
    {auto bad=document("menu");bad["markup"]=bad["markup"].get<std::string>()+"<script>x</script>";rejects([&]{decode_ui_document(bad.dump());},"Scripts are refused");}
    {auto bad=document("hud");bad["style"]="@import 'other.rcss';";rejects([&]{decode_ui_document(bad.dump());},"External style imports are refused");}
    {auto bad=document("hud");bad["surfaces"]=Json::array({"screen","screen"});rejects([&]{decode_ui_document(bad.dump());},"Duplicate surfaces are refused");}
    {auto bad=document("hud");bad["purpose"]="banner";rejects([&]{decode_ui_document(bad.dump());},"Unknown purpose refused");}
    Recorder renderer;auto font_bytes=[&]{std::ifstream in(font,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});}();
    {auto preview=std::make_unique<UiDocumentPreview>(renderer,UiFont{"HUD",font_bytes},1280,720);
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
     // Runtime UI documents and authoring previews share UiRuntime without a legacy HUD owner.
     {UiService service(renderer,UiFont{"HUD",font_bytes},[](auto){auto t=ui_template("hud");UiDocumentSource source;source.markup=t.markup;source.style=t.style;return source;});service.open("hud",UiService::Input::Passive,"play",1280,720);
      UiDocumentPreview second(renderer,UiFont{"HUD",font_bytes},640,360);auto t=ui_template("hud");check(second.load(t.markup,t.style),"Preview loads beside runtime UI");UiPreviewSample sample;sample.stamina=60;preview->update(sample);second.update({});renderer.drawn=0;preview->render();second.render();service.update();service.render([](auto& view,float,float,float,float){view.render();});check(renderer.drawn>0&&UiRuntime::alive(),"Runtime UI and previews coexist");}
     check(UiRuntime::alive(),"The runtime stays while a preview holds it");
     // Images are packaged assets addressed as da://<id>: a registered image is found by URL.
     {const auto url=ui_image_url(AssetId::random().text());preview->register_image(url,{2,2,std::vector<unsigned char>(16,255)});check(UiRuntime::image(url).has_value()&&!UiRuntime::image("C:/file.png"),"Images resolve by virtual URL only");}
     preview.reset();}
    check(!UiRuntime::alive(),"RmlUi shuts down with its last holder");
    // UI documents cook like the other native sources.
    {const auto folder=std::filesystem::temp_directory_path()/("ui-tests-"+AssetId::random().text());std::filesystem::create_directories(folder/"sources/ui/hud");
     {AssetService assets(folder/"sources",folder/"cache");assets.scan();check(assets.create_native("ui/hud/Stamina.daui",document("hud").dump()).changed,"UI document cooks");
      auto bad=document("menu");bad["markup"]="<div>not rml</div>";rejects([&]{assets.create_native("ui/menus/Bad.daui",bad.dump());},"Invalid UI document refused at cook");}
     std::error_code ignored;std::filesystem::remove_all(folder,ignored);}
    // R6 U0: multi-file documents (descriptor plus owned .rml/.rcss), shared style documents, packaged fonts and images, and an
    // explicit migration of an inline document, all through the coordinated native Save.
    {const auto folder=std::filesystem::temp_directory_path()/("ui-u0-"+AssetId::random().text());const auto source=folder/"sources";std::filesystem::create_directories(source/"ui/hud");std::filesystem::create_directories(source/"ui/shared");std::filesystem::create_directories(source/"ui/fonts");std::filesystem::create_directories(source/"ui/images");
     auto put=[&](const std::filesystem::path& path,const std::string& bytes){std::ofstream(path,std::ios::binary)<<bytes;};auto get=[&](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
     const auto project=std::filesystem::path(DAE_SOURCE_DIR);std::filesystem::copy_file(project/"content/ui/fonts/Roboto-Medium.ttf",source/"ui/fonts/Roboto-Medium.ttf");std::filesystem::copy_file(project/"content/ui/fonts/Roboto-Medium.ttf.license.txt",source/"ui/fonts/Roboto-Medium.ttf.license.txt");
     std::filesystem::copy_file(project/"content/royal_district/static/mud_huts/mudhut-trim.png",source/"ui/images/rune.png");
     AssetService assets(source,folder/"cache");const auto font_id=assets.adopt("ui/fonts/Roboto-Medium.ttf"),image_id=assets.adopt("ui/images/rune.png");assets.scan();check(assets.cook("ui/fonts/Roboto-Medium.ttf").changed,"A packaged font cooks");assets.cook("ui/images/rune.png");
     {std::filesystem::copy_file(project/"content/ui/fonts/Roboto-Medium.ttf",source/"ui/fonts/NoLicence.ttf");rejects([&]{assets.adopt("ui/fonts/NoLicence.ttf");assets.scan();assets.cook("ui/fonts/NoLicence.ttf");},"A font without its licence is refused");}
     const auto style_id=AssetId::random(),doc_id=AssetId::random();
     Json style={{"schema",2},{"kind","ui"},{"asset",style_id.text()},{"name","Shared"},{"purpose","style"},{"surfaces",Json::array()},{"size",{1280,720}},{"members",{{"style","Shared.rcss"}}}};
     const std::string markup="<rml><head><title>Rune</title></head><body data-model=\"ui\"><div id=\"rune\" class=\"panel\"><img id=\"rune-image\" src=\"da://"+image_id.text()+"\"/>{{name}}</div></body></rml>";
     Json doc={{"schema",2},{"kind","ui"},{"asset",doc_id.text()},{"name","Rune"},{"purpose","hud"},{"surfaces",{"screen"}},{"size",{1280,720}},{"members",{{"markup","Rune.rml"},{"style","Rune.rcss"},{"script","Rune.luau"}}},
        {"styles",{{{"asset",style_id.text()},{"source","ui/shared/Shared.daui"}}}},{"fonts",{{{"asset",font_id.text()},{"source","ui/fonts/Roboto-Medium.ttf"},{"family","Rune Font"}}}},{"images",{{{"asset",image_id.text()},{"source","ui/images/rune.png"}}}}};
     std::vector<NativeSourceEdit> edits{{"ui/shared/Shared.daui","",style.dump(2),true},{"ui/shared/Shared.rcss","",".panel{color:#ffffff;}",true},{"ui/hud/Rune.daui","",doc.dump(2),true},{"ui/hud/Rune.rml","",markup,true},{"ui/hud/Rune.rcss","","body{font-family:\"Rune Font\";}",true}};
     edits.push_back({"ui/hud/Rune.luau","","return {on_command=function(ui,h,c) return false end}",true});
     assets.commit_native(assets.prepare_native(edits));check(std::filesystem::exists(source/"ui/hud/Rune.rml")&&std::filesystem::exists(source/"ui/shared/Shared.rcss"),"Descriptor and member files publish together");
     assets.package(doc_id,folder/"ui.json");auto cooked=load_cooked_ui_document(folder/"ui.json",assets.cas_path(),doc_id);
     check(cooked.definition.schema==2&&cooked.definition.style.find(".panel{color:#ffffff;}")<cooked.definition.style.find("Rune Font")&&cooked.fonts.size()==1&&cooked.fonts[0].first=="Rune Font"&&cooked.images.size()==1&&cooked.images[0].url=="da://"+image_id.text()&&cooked.images[0].width>0,"Cooked document: shared style first, packaged font and image in its frozen closure");
     check(cooked.definition.script.find("on_command")!=std::string::npos,"Cooked local script preserved");
     {NativeSourceEdit invalid{"ui/hud/Rune.luau",sha256(get(source/"ui/hud/Rune.luau")),"return function ("};rejects([&]{assets.prepare_native({&invalid,1});},"Malformed Luau cannot publish");}
     // The cooked document loads in a preview from its packaged font and image alone.
     {Recorder packaged;UiDocumentPreview preview(packaged,UiFont{"HUD",font_bytes},800,600);for(auto& [family,bytes]:cooked.fonts)preview.register_font({family,bytes});for(const auto& image:cooked.images)preview.register_image(image.url,{image.width,image.height,image.rgba});
      check(preview.load(cooked.definition.markup,cooked.definition.style),"Packaged document loads");UiPreviewSample sample;sample.name="Rune";preview.update(sample);check(preview.box("rune-image").has_value(),"Packaged image element laid out");}
     // A member edit alone republishes its document; a stale member edit is refused.
     {const auto before=get(source/"ui/hud/Rune.rcss");NativeSourceEdit member{"ui/hud/Rune.rcss",sha256(before),"body{font-family:\"Rune Font\";font-size:20px;}"};auto results=assets.commit_native(assets.prepare_native({&member,1}));
      check(std::any_of(results.begin(),results.end(),[&](const CookResult& r){return r.root==doc_id&&r.changed;}),"A member edit recooks its document");
      NativeSourceEdit stale{"ui/hud/Rune.rcss",sha256(before),"body{}"};rejects([&]{assets.prepare_native({&stale,1});},"A stale member edit is refused");
      NativeSourceEdit orphan{"ui/hud/Orphan.rml","","<rml><head></head><body></body></rml>",true};rejects([&]{assets.prepare_native({&orphan,1});},"A member file no document lists is refused");}
     // References outside the packaged dependencies are refused.
     {auto bad=doc;bad["asset"]=AssetId::random().text();bad["images"]=Json::array();bad["members"]={{"markup","Bad.rml"},{"style","Bad.rcss"}};std::vector<NativeSourceEdit> refused{{"ui/hud/Bad.daui","",bad.dump(2),true},{"ui/hud/Bad.rml","",markup,true},{"ui/hud/Bad.rcss","","",true}};
      rejects([&]{assets.commit_native(assets.prepare_native(refused));},"An undeclared da:// image is refused");
      auto file_url=bad;file_url["images"]=doc["images"];std::vector<NativeSourceEdit> url_edits{{"ui/hud/Bad.daui","",file_url.dump(2),true},{"ui/hud/Bad.rml","",markup,true},{"ui/hud/Bad.rcss","","div{decorator:image(x.png);background:url(c:/x.png);}",true}};
      rejects([&]{assets.commit_native(assets.prepare_native(url_edits));},"A file url() is refused");
      auto escape=doc;escape["members"]["markup"]="../Escape.rml";rejects([&]{decode_ui_document(escape.dump());},"A member outside the document folder is refused");}
     // Migration: an inline (schema 1) document becomes descriptor plus members in one Save.
     {const auto inline_id=AssetId::random();auto old=document("menu",inline_id);assets.create_native("ui/hud/Old.daui",old.dump());const auto before=get(source/"ui/hud/Old.daui");
      auto migrated=old;migrated["schema"]=2;migrated["members"]={{"markup","Old.rml"},{"style","Old.rcss"}};migrated.erase("markup");migrated.erase("style");
      std::vector<NativeSourceEdit> migration{{"ui/hud/Old.daui",sha256(before),migrated.dump(2)},{"ui/hud/Old.rml","",old["markup"].get<std::string>(),true},{"ui/hud/Old.rcss","",old["style"].get<std::string>(),true}};
      assets.commit_native(assets.prepare_native(migration));assets.package(inline_id,folder/"old.json");auto loaded=load_cooked_ui_document(folder/"old.json",assets.cas_path(),inline_id);check(loaded.definition.schema==2&&loaded.definition.markup==old["markup"].get<std::string>(),"Migrated document cooks from its member files");}
     std::error_code ignored;std::filesystem::remove_all(folder,ignored);}
    std::cout<<"R6.1 UI documents: templates, self-contained rules, preview load/bind/draw, sample values, command log, stale retention, UiService/preview coexistence through the shared UiRuntime, cook, multi-file documents, shared styles, packaged fonts and images, member Save and migration passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
