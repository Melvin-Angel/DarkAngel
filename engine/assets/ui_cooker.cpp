#include "assets_internal.hpp"
#include <darkangel/ui_assets.hpp>
#include <darkangel/hash.hpp>
#include <Luau/Compiler.h>
#include <algorithm>
#include <cctype>
#include <set>
namespace darkangel {
using namespace assets_detail;
namespace {
constexpr std::size_t ui_text_limit=24*1024;
const char* base_style="body{font-family:HUD;font-size:16px;color:#f2e8d5;width:100%;height:100%;}div{display:block;}\n";
std::string lowercase(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;}
// A member file name: in the descriptor's own folder, with the member's extension.
void check_member(const std::string& name,const char* extension){
    require(!name.empty()&&name.size()<=96&&name.find_first_of("/\\:")==std::string::npos&&name!=".."&&name.ends_with(extension),std::string("A UI member is a ")+extension+" file in the document's folder");}
// A dependency record: an AssetID and its source locator in the project (and a font family name).
UiDependency dependency(const Json& value,const char* what,std::initializer_list<const char*> extensions,bool family){
    require(value.is_object()&&value.size()==(family?3u:2u)&&value.contains("asset")&&value.contains("source"),std::string("A UI ")+what+" names its asset and source"+(family?" and family":""));
    UiDependency result;result.asset=AssetId::parse(value.at("asset").get<std::string>());require(result.asset!=AssetId{},std::string("UI ")+what+" identity");
    result.source=value.at("source").get<std::string>();require(!result.source.empty()&&result.source.size()<=256,std::string("UI ")+what+" source");
    bool known=false;for(const char* extension:extensions)known|=lowercase(result.source).ends_with(extension);require(known,std::string("UI ")+what+" source type");
    if(family){result.family=value.at("family").get<std::string>();require(!result.family.empty()&&result.family.size()<=32&&std::all_of(result.family.begin(),result.family.end(),[](unsigned char c){return std::isalnum(c)||c==' '||c=='-'||c=='_';}),"A UI font family is letters, digits, spaces, hyphens or underscores");}
    return result;}
}
UiDocumentDefinition decode_ui_document(std::string_view text,const UiMemberReader& members){
    require(text.size()<=60000,"UI document source size bound (60 KB)");auto value=json(text,60000);
    require(value.is_object()&&value.at("kind")=="ui","UI document schema/type");UiDocumentDefinition result;result.schema=value.at("schema").get<int>();require(result.schema==1||result.schema==2,"UI document schema is 1 (inline) or 2 (member files)");
    for(const auto& [key,unused]:value.items()){const bool common=key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="purpose"||key=="surfaces"||key=="size"||key=="binding";
        if(result.schema==1)require(common||key=="markup"||key=="style","UI document fields: schema, kind, asset, name, purpose, surfaces, size, markup and style");
        else require(common||key=="members"||key=="styles"||key=="fonts"||key=="images","UI document fields: schema, kind, asset, name, purpose, surfaces, size, members, styles, fonts and images");}
    result.binding=value.value("binding",std::string("ui-v1"));require(result.binding=="ui-v1"||result.binding=="owner-v1"||result.binding=="inventory-v1"||result.binding=="altar-v1","Unknown UI binding contract");
    result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"UI document identity");
    result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"UI document name bounds");
    result.purpose=value.at("purpose").get<std::string>();const bool style_only=result.purpose=="style";
    require(result.purpose=="hud"||result.purpose=="marker"||result.purpose=="menu"||result.purpose=="puzzle"||(style_only&&result.schema==2),"UI document purpose is hud, marker, menu, puzzle or (member-file documents) style");
    const auto& surfaces=value.at("surfaces");require(surfaces.is_array()&&surfaces.size()<=3&&(style_only?surfaces.empty():!surfaces.empty()),style_only?"A shared style document has no surfaces":"UI document permits one to three surfaces");
    for(const auto& surface:surfaces){auto name=surface.get<std::string>();require(name=="screen"||name=="world_anchor"||name=="world_surface","UI surfaces are screen, world_anchor and world_surface");require(std::find(result.surfaces.begin(),result.surfaces.end(),name)==result.surfaces.end(),"UI surfaces are listed once");result.surfaces.push_back(name);}
    if(result.binding!="ui-v1")require(result.surfaces==std::vector<std::string>{"screen"},"Native owner/inventory/altar bindings are screen-only");
    const auto& size=value.at("size");require(size.is_array()&&size.size()==2&&size[0].is_number_integer()&&size[1].is_number_integer(),"UI document size is two whole numbers");
    for(int i=0;i<2;++i){result.size[i]=size[i].get<int>();require(result.size[i]>=64&&result.size[i]<=8192,"UI document size is 64 to 8192 pixels");}
    if(result.schema==1){result.markup=value.at("markup").get<std::string>();result.style=value.at("style").get<std::string>();validate_ui_text(result);return result;}
    const auto& owned=value.at("members");require(owned.is_object()&&owned.size()<=3,"UI members are markup and style");
    for(const auto& [key,unused]:owned.items())require(key=="markup"||key=="style"||key=="script","UI members are markup and style");
    require(owned.contains("style")&&(style_only?!owned.contains("markup"):owned.contains("markup")),style_only?"A shared style document has a style member only":"A UI document has markup and style members");
    result.style_member=owned.at("style").get<std::string>();check_member(result.style_member,".rcss");
    if(!style_only){result.markup_member=owned.at("markup").get<std::string>();check_member(result.markup_member,".rml");}
    if(owned.contains("script")){require(!style_only,"Shared styles have no script");result.script_member=owned.at("script").get<std::string>();check_member(result.script_member,".luau");}
    auto list=[&](const char* key,std::size_t limit,const char* what,std::initializer_list<const char*> extensions,bool family){std::vector<UiDependency> found;if(!value.contains(key))return found;const auto& entries=value.at(key);
        require(entries.is_array()&&entries.size()<=limit,std::string("Too many UI ")+what+"s");std::set<AssetId> unique;
        for(const auto& entry:entries){found.push_back(dependency(entry,what,extensions,family));require(unique.insert(found.back().asset).second,std::string("A UI ")+what+" is listed once");}return found;};
    result.styles=list("styles",8,"shared style",{".daui"},false);result.fonts=list("fonts",4,"font",{".ttf",".otf"},true);result.images=list("images",32,"image",{".png",".jpg",".jpeg"},false);
    require(!style_only||(result.styles.empty()&&result.images.empty()),"A shared style document has no style or image dependencies of its own");
    if(members){if(!result.script_member.empty()){result.script=members(result.script_member);require(result.script.size()<=16*1024,"UI script is at most 16 KB");}result.style=members(result.style_member);if(!style_only)result.markup=members(result.markup_member);validate_ui_text(result);}
    return result;
}
void validate_ui_text(const UiDocumentDefinition& document){
    require(document.script.size()<=16*1024,"UI script is at most 16 KB");
    if(!document.script.empty()){const auto code=Luau::compile(document.script);require(!code.empty()&&code[0]!=0,"UI presentation script does not compile: "+(code.size()>1?code.substr(1):std::string{}));}
    require(document.markup.size()<=ui_text_limit&&document.style.size()<=ui_text_limit,"UI markup and style are at most 24 KB each");
    if(document.purpose!="style")require(document.markup.find("<rml")!=std::string::npos&&document.markup.find("</head>")!=std::string::npos&&document.markup.find("</rml>")!=std::string::npos,"UI markup is an <rml> document with a <head>");
    auto lower=lowercase(document.markup+"\n"+document.style);
    for(const char* forbidden:{"<link","<script","<template","@import"})require(lower.find(forbidden)==std::string::npos,"UI documents may not use external stylesheets, templates or scripts; list shared styles in the document instead");
    // The only external references are packaged images by virtual URL: url(da://<id>), image(da://<id>) or src="da://<id>".
    std::set<std::string> declared;for(const auto& image:document.images)declared.insert(lowercase(image.asset.text()));
    for(std::size_t at=lower.find("url(");at!=std::string::npos;at=lower.find("url(",at+4))require(lower.compare(at+4,5,"da://")==0||lower.compare(at+4,6,"'da://")==0||lower.compare(at+4,6,"\"da://")==0,"UI documents reference images only as da://<image asset> (url(...) to anything else is refused)");
    for(std::size_t at=lower.find("da://");at!=std::string::npos;at=lower.find("da://",at+5)){auto id=lower.substr(at+5,36);require(declared.contains(id),"A da:// reference names an image the document does not list");}
    for(std::size_t at=lower.find("src=");at!=std::string::npos;at=lower.find("src=",at+4)){auto rest=lower.substr(at+4,7);require(rest.starts_with("\"da://")||rest.starts_with("'da://"),"UI images use src=\"da://<image asset>\"");}
}
UiTemplate ui_template(std::string_view purpose){
    if(purpose=="style")return {"",R"ui(/* Shared style: listed by documents in their Styles; applied before each document's own style. */
.panel{background-color:#000000aa;padding:8px;}
.caption{font-size:14px;color:#c9b48a;})ui",{}};
    if(purpose=="hud")return {R"ui(<rml><head><title>Stamina</title></head>
<body data-model="ui">
  <div id="stamina-panel">
    <div class="label">Stamina</div>
    <div class="bar"><div class="fill" data-style-width="stamina_percent + '%'"></div></div>
    <div id="stamina-text" class="value">{{stamina_text}}</div>
  </div>
</body></rml>)ui",std::string(base_style)+R"ui(#stamina-panel{position:absolute;right:24px;top:24px;width:280px;}
.label{font-size:14px;}
.bar{height:14px;background-color:#00000099;border:1px #c9b48a;}
.fill{height:100%;background-color:#d9a441;}
.value{font-size:13px;})ui",{"screen"}};
    if(purpose=="marker")return {R"ui(<rml><head><title>Nameplate</title></head>
<body data-model="ui">
  <div id="nameplate">
    <div id="nameplate-name" class="name">{{name}}</div>
    <div class="bar"><div class="fill" data-style-width="health_percent + '%'"></div></div>
  </div>
</body></rml>)ui",std::string(base_style)+R"ui(#nameplate{position:absolute;left:0px;top:0px;width:160px;text-align:center;}
.name{font-size:14px;}
.bar{height:6px;background-color:#00000099;}
.fill{height:100%;background-color:#c0392b;})ui",{"world_anchor","screen"}};
    if(purpose=="menu")return {R"ui(<rml><head><title>Main menu</title></head>
<body data-model="ui">
  <div id="menu">
    <h1 id="menu-title">Ashen Roots</h1>
    <button id="menu-start" data-event-click="command('menu.start')">Start</button>
    <button id="menu-settings" data-event-click="command('menu.settings')">Settings</button>
    <button id="menu-quit" data-event-click="command('menu.quit')">Quit</button>
  </div>
</body></rml>)ui",std::string(base_style)+R"ui(#menu{position:absolute;left:80px;top:120px;width:320px;}
h1{display:block;font-size:36px;margin-bottom:24px;}
button{display:block;font-size:20px;padding:8px 16px;margin-bottom:8px;background-color:#00000099;}
button:hover{background-color:#d9a44155;})ui",{"screen"}};
    if(purpose=="puzzle")return {R"ui(<rml><head><title>Rune panel</title></head>
<body data-model="ui">
  <div id="puzzle">
    <div class="title">Rune panel</div>
    <div class="runes">
      <button id="rune-1" data-event-click="command('puzzle.rune.1')">I</button>
      <button id="rune-2" data-event-click="command('puzzle.rune.2')">II</button>
      <button id="rune-3" data-event-click="command('puzzle.rune.3')">III</button>
    </div>
    <div id="puzzle-state" data-if="flag">Solved</div>
  </div>
</body></rml>)ui",std::string(base_style)+R"ui(#puzzle{position:absolute;left:80px;top:80px;width:360px;}
.title{font-size:22px;margin-bottom:12px;}
button{display:inline-block;width:80px;font-size:24px;padding:12px 0px;margin-right:8px;text-align:center;background-color:#00000099;}
button:hover{background-color:#d9a44155;}
#puzzle-state{font-size:18px;color:#8fd18f;margin-top:12px;})ui",{"screen","world_surface"}};
    throw std::runtime_error("UI template purpose is hud, marker, menu, puzzle or style");
}
namespace assets_detail {
// A packaged font: TrueType/OpenType bytes with a licence file beside it (<file>.license.txt), which is an input of the cook.
Import import_font(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,16*1024*1024);require(bytes.size()>=12,"Font file too short");const auto magic=bytes.substr(0,4);
    require(magic==std::string("\x00\x01\x00\x00",4)||magic=="OTTO"||magic=="true","A UI font is a single TrueType or OpenType face (.ttf/.otf)");
    auto license=source;license+=".license.txt";require(std::filesystem::is_regular_file(license),"A packaged font needs its licence beside it: "+license.filename().string());
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.inputs[license.lexically_relative(root).generic_string()]=sha256(read(license,1024*1024));result.product_count=1;
    if(!inspect){require(ids.size()==1&&ids.contains("$source"),"Font UUID mapping mismatch");result.products.push_back({ids.at("$source"),"font","font",std::move(bytes),{}});}
    return result;
}
Import import_ui(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,60000);Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    // Member files and shared style documents are inputs of this cook: editing one recooks the document.
    auto reader=[&](const std::filesystem::path& folder){return [&,folder](std::string_view name){auto path=folder/std::string(name);auto text=read(path,ui_text_limit);result.inputs[path.lexically_relative(root).generic_string()]=sha256(text);return text;};};
    auto document=decode_ui_document(bytes,reader(source.parent_path()));if(!inspect)require(ids.size()==1&&ids.at("$source")==document.id,"UI document source identity");
    result.identities.push_back(document.id);result.product_count=1;
    if(document.schema==1){if(!inspect)result.products.push_back({document.id,"ui","ui.json",json(bytes,60000).dump(),{}});return result;}
    std::string style;std::vector<AssetId> required;
    for(const auto& shared:document.styles){auto path=within(root,shared.source);auto shared_bytes=read(path,60000);result.inputs[path.lexically_relative(root).generic_string()]=sha256(shared_bytes);
        auto definition=decode_ui_document(shared_bytes,reader(path.parent_path()));require(definition.id==shared.asset&&definition.purpose=="style","A listed shared style must be a style document with that identity");style+=definition.style+"\n";}
    style+=document.style;
    auto checked=[&](const UiDependency& dependency,const char* importer){auto path=within(root,dependency.source);auto metadata_path=path;metadata_path+=".daimport";auto metadata=json(read(metadata_path,1024*1024),1024*1024);
        require(metadata.at("importer")==importer&&AssetId::parse(metadata.at("id").get<std::string>())==dependency.asset,std::string("A UI dependency must be an imported asset of type ")+importer+" with the listed identity");result.inputs[metadata_path.lexically_relative(root).generic_string()]=sha256(read(metadata_path,1024*1024));return path;};
    Json fonts=Json::array(),images=Json::array();
    for(const auto& font:document.fonts){auto imported=import_font(root,checked(font,"font-v1"),{{"$source",font.asset}},inspect);for(const auto& [path,hash]:imported.inputs)result.inputs[path]=hash;for(auto& product:imported.products)result.products.push_back(std::move(product));required.push_back(font.asset);fonts.push_back({{"asset",font.asset.text()},{"family",font.family}});}
    for(const auto& image:document.images){auto imported=import_texture(root,checked(image,"texture-color-v1"),{{"$source",image.asset}},inspect);for(const auto& [path,hash]:imported.inputs)result.inputs[path]=hash;for(auto& product:imported.products)result.products.push_back(std::move(product));required.push_back(image.asset);images.push_back(image.asset.text());}
    result.product_count+=required.size();
    if(!inspect){Json product={{"schema",2},{"id",document.id.text()},{"name",document.name},{"purpose",document.purpose},{"binding",document.binding},{"surfaces",document.surfaces},{"size",document.size},{"markup",document.markup},{"style",style},{"script",document.script},{"fonts",fonts},{"images",images}};
        result.products.push_back({document.id,"ui","ui.json",product.dump(),required});}
    return result;
}
}
namespace {
// One product of a runtime registry by identity, kind and extension, verified against its digest.
std::string product(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id,const char* kind,const char* extension){
    const auto held=registry_json(registry);const Json* found{};for(const auto& record:held->at("assets"))if(record.at("id")==id.text())found=&record;
    require(found&&found->at("kind")==kind&&found->at("extension")==extension,std::string("Cooked ")+kind+" product missing from the registry");const auto digest=found->at("sha256").get<std::string>();
    require(digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==std::string::npos,"Cooked product digest");auto bytes=read(cas/(digest+"."+extension));require(sha256(bytes)==digest,"Cooked product hash mismatch");return bytes;}
}
std::string load_cooked_font(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){return product(registry,cas,id,"font","font");}
CookedUiDocument load_cooked_ui_document(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    const auto bytes=product(registry,cas,id,"ui","ui.json");const auto value=json(bytes,1024*1024);CookedUiDocument result;
    if(value.at("schema")==1){result.definition=decode_ui_document(bytes);require(result.definition.id==id,"Cooked UI identity");return result;}
    auto& definition=result.definition;definition.schema=2;definition.id=AssetId::parse(value.at("id").get<std::string>());require(definition.id==id,"Cooked UI identity");
    definition.binding=value.value("binding",std::string("ui-v1"));definition.script=value.value("script",std::string{});require(definition.script.size()<=16*1024,"Cooked UI script bound");definition.name=value.at("name").get<std::string>();definition.purpose=value.at("purpose").get<std::string>();definition.markup=value.at("markup").get<std::string>();definition.style=value.at("style").get<std::string>();definition.size=value.at("size").get<std::array<int,2>>();for(const auto& surface:value.at("surfaces"))definition.surfaces.push_back(surface.get<std::string>());
    for(const auto& font:value.at("fonts")){const auto asset=AssetId::parse(font.at("asset").get<std::string>());const auto family=font.at("family").get<std::string>();definition.fonts.push_back({asset,{},family});result.fonts.push_back({family,load_cooked_font(registry,cas,asset)});}
    for(const auto& image:value.at("images")){const auto asset=AssetId::parse(image.get<std::string>());definition.images.push_back({asset,{},{}});auto texture=load_cooked_texture(registry,cas,asset);require(!texture.mips.empty(),"Cooked UI image");
        const auto& top=texture.mips.front();result.images.push_back({"da://"+asset.text(),int(top.width),int(top.height),top.rgba});}
    validate_ui_text(definition);return result;
}
}
