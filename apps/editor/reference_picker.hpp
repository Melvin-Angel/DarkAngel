#pragma once
#include "native_authoring.hpp"
#include <fstream>
#include <algorithm>

namespace darkangel::editor_app {
inline bool reference_search(std::string_view value,std::string_view query){
 auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
 return std::search(value.begin(),value.end(),query.begin(),query.end(),[&](char a,char b){return fold(a)==fold(b);})!=value.end();
}
inline bool selectable_attribute(const nlohmann::json& definition,bool resource_only){
 return !resource_only||definition.value("kind","")=="resource";
}
struct ReferenceCandidate {AssetInfo asset;std::string diagnostic;};
// This is a view of the existing catalog, never a second asset registry.
inline std::vector<ReferenceCandidate> reference_candidates(AssetService& assets,const NativeAuthoring& author,std::span<const AssetInfo> inventory,std::string_view extension,std::string_view query,const nlohmann::json* attributes=nullptr,std::string_view importer="clip-gltf-v1"){
 std::vector<ReferenceCandidate> result;
 for(const auto& asset:inventory){
  if(std::filesystem::path(asset.path).extension()!=extension)continue;
  if(!reference_search(asset.path,query)&&!reference_search(asset.id.text(),query))continue;
  ReferenceCandidate candidate{asset,{}};
  try{
   if(extension==".glb"){
    std::ifstream file(assets.source_root()/(asset.path+".daimport"));
    if(!file||nlohmann::json::parse(file).value("importer","")!=importer)continue;
   }
   if(attributes){
    nlohmann::json value;
    auto draft=author.drafts.find(asset.id);
    if(draft!=author.drafts.end())value=draft->second.value;
    else {std::ifstream file(assets.source_root()/asset.path);value=nlohmann::json::parse(file);}
    if(!value.contains("attributes")||value["attributes"]!=*attributes)continue;
   }
  }catch(const std::exception& error){candidate.diagnostic=error.what();}
  result.push_back(std::move(candidate));
 }
 return result;
}
}
