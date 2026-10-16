#include "assets_internal.hpp"
#include <darkangel/ui_assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
namespace darkangel {
using namespace assets_detail;
namespace {
constexpr std::size_t ui_text_limit=24*1024;
const char* base_style="body{font-family:HUD;font-size:16px;color:#f2e8d5;width:100%;height:100%;}\n";
}
UiDocumentDefinition decode_ui_document(std::string_view text){
    require(text.size()<=60000,"UI document source size bound (60 KB, markup and style together)");auto value=json(text,60000);
    require(value.is_object()&&value.at("schema")==1&&value.at("kind")=="ui","UI document schema/type");
    for(const auto& [key,unused]:value.items())require(key=="schema"||key=="kind"||key=="asset"||key=="name"||key=="purpose"||key=="surfaces"||key=="size"||key=="markup"||key=="style","UI document fields: schema, kind, asset, name, purpose, surfaces, size, markup and style");
    UiDocumentDefinition result;result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"UI document identity");
    result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"UI document name bounds");
    result.purpose=value.at("purpose").get<std::string>();require(result.purpose=="hud"||result.purpose=="marker"||result.purpose=="menu"||result.purpose=="puzzle","UI document purpose is hud, marker, menu or puzzle");
    const auto& surfaces=value.at("surfaces");require(surfaces.is_array()&&!surfaces.empty()&&surfaces.size()<=3,"UI document permits one to three surfaces");
    for(const auto& surface:surfaces){auto name=surface.get<std::string>();require(name=="screen"||name=="world_anchor"||name=="world_surface","UI surfaces are screen, world_anchor and world_surface");require(std::find(result.surfaces.begin(),result.surfaces.end(),name)==result.surfaces.end(),"UI surfaces are listed once");result.surfaces.push_back(name);}
    const auto& size=value.at("size");require(size.is_array()&&size.size()==2&&size[0].is_number_integer()&&size[1].is_number_integer(),"UI document size is two whole numbers");
    for(int i=0;i<2;++i){result.size[i]=size[i].get<int>();require(result.size[i]>=64&&result.size[i]<=8192,"UI document size is 64 to 8192 pixels");}
    result.markup=value.at("markup").get<std::string>();result.style=value.at("style").get<std::string>();
    require(result.markup.size()<=ui_text_limit&&result.style.size()<=ui_text_limit,"UI markup and style are at most 24 KB each");
    require(result.markup.find("<rml")!=std::string::npos&&result.markup.find("</head>")!=std::string::npos&&result.markup.find("</rml>")!=std::string::npos,"UI markup is an <rml> document with a <head>");
    auto lower=result.markup+result.style;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(const char* forbidden:{"<link","<script","<template","@import","url("})require(lower.find(forbidden)==std::string::npos,"UI documents are self-contained for now: no external stylesheets, templates, images or scripts");
    return result;
}
UiTemplate ui_template(std::string_view purpose){
    if(purpose=="hud")return {R"ui(<rml><head><title>Stamina</title></head>
<body data-model="ui">
  <div id="stamina-panel">
    <div class="label">Stamina</div>
    <div class="bar"><div class="fill" data-style-width="stamina_percent + '%'"></div></div>
    <div id="stamina-text" class="value">{{stamina_text}}</div>
  </div>
</body></rml>)ui",std::string(base_style)+R"ui(#stamina-panel{position:absolute;left:24px;bottom:24px;width:280px;}
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
    throw std::runtime_error("UI template purpose is hud, marker, menu or puzzle");
}
namespace assets_detail {
Import import_ui(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    auto bytes=read(source,60000);auto document=decode_ui_document(bytes);if(!inspect)require(ids.size()==1&&ids.at("$source")==document.id,"UI document source identity");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(document.id);result.product_count=1;
    if(!inspect)result.products.push_back({document.id,"ui","ui.json",json(bytes,60000).dump(),{}});return result;
}
}
}
