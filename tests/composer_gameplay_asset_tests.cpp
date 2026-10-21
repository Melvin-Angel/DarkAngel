#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);check(bool(input),"Missing fixture/product");return {std::istreambuf_iterator<char>(input),{}};}
}
int main(){try{
 const auto source=std::filesystem::path(DAE_SOURCE_DIR)/"content";
 const auto root=std::filesystem::path(DAE_BINARY_DIR)/("composer-gameplay-"+AssetId::random().text());std::filesystem::create_directories(root);
 const auto kit_path=std::filesystem::path("royal_district/combat/earth.dakit");auto kit_source=Json::parse(read(source/kit_path));const auto id=AssetId::parse(kit_source.at("asset").get<std::string>());
 const auto baseline=sha256(read(source/kit_path));AssetService assets(source,root/"cache");assets.scan();check(assets.cook(kit_path.generic_string()).changed,"Cold Earth cook did not publish");check(!assets.cook(kit_path.generic_string()).changed,"Composer dependency closure did not warm-cache");assets.package(id,root/"package.json");
 // Exercise a real private edit over the complete mount once. The transactional
 // command suite uses a smaller closed fixture instead of repeating this copy.
 {const auto relative=std::string("royal_district/combat/earth/earth_light.daability");const auto original=read(source/relative);auto edit=Json::parse(original);edit["cooldown_ticks"]=edit.at("cooldown_ticks").get<unsigned>()+1;const NativeSourceEdit change{relative,sha256(original),edit.dump(2)+"\n"};const std::array roots{id};auto candidate=assets.preview_native({&change,1},roots);const auto prepared=load_cooked_combat_kit(candidate.registry(),assets.cas_path(),id);check(!prepared.abilities.empty()&&read(source/relative)==original,"Complete production snapshot failed or published preview edits");}
 auto loaded=load_cooked_combat_kit(root/"package.json",assets.cas_path(),id);unsigned hits=0,effects=0,shots=0;
 auto inspect=[&](const ActionDefinition& action){for(const auto& block:action.blocks){hits+=block.hit.has_value();for(const auto& effect:block.effects){check(effect.definition&&effect.definition->id==effect.asset&&(effect.power==1||effect.power==5),"Composer effect not resolved with authored power");++effects;}if(block.projectile){const auto& shot=*block.projectile;check(shot.visual_model!=AssetId{}&&shot.preparation&&shot.preparation->release_block==block.id&&shot.speed==20&&shot.range==30,"Spear lost typed model/motion/preparation");++shots;}}};
 for(const auto& ability:loaded.abilities){if(ability->action->id==AssetId::parse(Json::parse(read(source/"royal_district/combat/earth/earth_spear.daaction")).at("asset").get<std::string>()))check(ability->aim&&ability->aim->projectile&&ability->aim->projectile->composer_owned,"Prepared projectile adapter is not derived from composer");inspect(*ability->action);for(const auto& step:ability->combo_steps)inspect(*step.action);}
 std::cout<<"Earth typed hits="<<hits<<" effects="<<effects<<" projectiles="<<shots<<'\n';check(hits==4&&effects==4&&shots==1,"Migrated Earth actions/effect counts changed");
 const auto registry=Json::parse(read(root/"package.json"));auto corrupted=registry;bool changed=false;
 for(const auto& record:registry.at("assets"))if(record.at("kind")=="action"){const auto bytes=read(assets.cas_path()/(record.at("sha256").get<std::string>()+".action.json"));check(bytes.find("model_source")==std::string::npos&&bytes.find("royal_district/combat/flinch.daeffect")==std::string::npos,"Composer runtime product retained editor source paths");}
 const auto effect_id=AssetId::parse(Json::parse(read(source/"royal_district/combat/flinch.daeffect")).at("asset").get<std::string>());
 for(auto& record:corrupted["assets"])if(record["id"]==effect_id.text()){record["sha256"]=std::string(64,'f');changed=true;}check(changed,"Effect missing from source-free package");std::ofstream(root/"corrupted.json")<<corrupted.dump();bool rejected=false;try{load_cooked_combat_kit(root/"corrupted.json",assets.cas_path(),id);}catch(...){rejected=true;}check(rejected,"Tampered composer effect generation accepted");check(sha256(read(source/kit_path))==baseline,"Asset test changed production content");
 std::cout<<"Earth composer-owned effects/projectile preparation, warm cook, source-free load and frozen-generation rejection passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
