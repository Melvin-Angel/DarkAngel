#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <map>
// Explicit v1 compatibility data for the older multi-asset authoring commands.
// Production content and the gameplay integration tests remain migrated.
inline void prepare_legacy_combat_fixture(const std::filesystem::path& root){
 using Json=nlohmann::json;const auto read=[&](const auto& path){std::ifstream input(root/path);return Json::parse(input);};const auto write=[&](const auto& path,const Json& value){std::ofstream(root/path)<<value.dump(2)<<'\n';};
 std::map<std::string,Json> bindings;
 for(const auto* stem:{"light","heavy"}){const auto relative=std::string("royal_district/combat/")+stem+".daability";auto ability=read(relative);auto action=read(ability["sources"]["action"].get<std::string>());auto profiles=Json::array();auto effects=Json::array();
  for(auto& block:action["blocks"])if(block.contains("hit")){auto profile=block["hit"];profile.erase("schema");profile.erase("shape");profile["block"]=block["id"];profiles.push_back(profile);block.erase("hit");if(block.contains("effects")){for(const auto& effect:block["effects"]["entries"])effects.push_back({{"ability",ability["asset"]},{"hit_block",block["id"]},{"effect",effect["asset"]},{"power",effect["power"]}});block.erase("effects");}}
  ability["melee"]=profiles;bindings[ability["asset"].get<std::string>()]=effects;write(ability["sources"]["action"].get<std::string>(),action);write(relative,ability);
 }
 for(const auto* path:{"royal_district/combat/player.dakit","royal_district/combat/earth.dakit"}){auto kit=read(path);auto rows=kit.value("effect_bindings",Json::array());for(const auto& slot:kit["slots"])if(slot["ability"].is_string()){const auto id=slot["ability"].get<std::string>();if(bindings.contains(id))for(const auto& binding:bindings[id])rows.push_back(binding);}kit["effect_bindings"]=rows;write(path,kit);}
}
