#include <darkangel/ability_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/world_session.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected checked rejection");}
void write(const std::filesystem::path& path,const Json& data){std::ofstream(path)<<data.dump(2);}
Json read(const std::filesystem::path& path){std::ifstream file(path);return Json::parse(file);}
}
int main(){try{
    auto root=std::filesystem::path(DAE_BINARY_DIR)/("ability-assets-"+AssetId::random().text());auto sources=root/"sources";std::filesystem::create_directories(sources);
    auto action=read(std::filesystem::path(DAE_SOURCE_DIR)/"content/animation/m5_attack.daaction");write(sources/"attack.daaction",action);
    auto schema_id=AssetId::random(),ability_id=AssetId::random();
    Json schema={{"schema",1},{"kind","attributes"},{"asset",schema_id.text()},{"attributes",Json::array()}};
    auto field=[&](unsigned id,const char* name,const char* kind,double base,double maximum,unsigned maximum_attribute,const char* visibility){schema["attributes"].push_back({{"id",id},{"name",name},{"kind",kind},{"base",base},{"minimum",0.0},{"maximum",maximum},{"maximum_attribute",maximum_attribute},{"unit","points"},{"visibility",visibility}});};
    field(1,"MaxHealth","statistic",100,1000,0,"public");field(2,"Health","resource",100,1000,1,"public");field(3,"Stamina","resource",60,60,0,"owner");
    write(sources/"base.daattributes",schema);
    Json ability={{"schema",1},{"kind","ability"},{"asset",ability_id.text()},{"action",action.at("asset")},{"attributes",schema_id.text()},{"sources",{{"action","attack.daaction"},{"attributes","base.daattributes"}}},{"costs",Json::array({{{"attribute",3},{"amount",20.0}}})},{"cooldown_group",1},{"cooldown_ticks",6},{"activate_on","pressed"},{"minimum_held_us",0},{"cancel_on_release",false},{"interruptible",true}};
    write(sources/"light.daability",ability);std::string original_generation;CookedAbility pinned;
    {
        AssetService service(sources,root/"cache");check(service.adopt("light.daability")==ability_id&&service.adopt("base.daattributes")==schema_id,"Embedded combat identities");
        check(service.cook("light.daability").changed&&!service.cook("light.daability").changed,"Warm closure skips conversion");service.package(ability_id,root/"registry.json");
        check(read(root/"registry.json").at("assets").size()==3,"Ability packages frozen action/schema dependencies");pinned=load_cooked_ability(root/"registry.json",service.cas_path(),ability_id);original_generation=pinned.definition->generation;
        check(pinned.attributes.fields[1].visibility==AttributeVisibility::Public&&pinned.attributes.fields[2].unit=="points","Typed attribute metadata survives cook");
        auto invalid=ability;invalid["costs"][0]["attribute"]=1;write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});service.package(ability_id,root/"registry.json");check(load_cooked_ability(root/"registry.json",service.cas_path(),ability_id).definition->generation==original_generation,"Statistic cost failure preserves usable generation");
        invalid=ability;invalid["costs"][0]["amount"]=-1;write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});
        invalid=ability;invalid["cooldown_ticks"]=6.5;write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});
        invalid=ability;invalid["activate_on"]="unknown";write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});
        invalid=ability;invalid["sources"]["action"]="../attack.daaction";write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});
        invalid=ability;invalid["action"]=schema_id.text();write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});
        invalid=ability;invalid["extra"]=true;write(sources/"light.daability",invalid);rejects([&]{service.cook("light.daability");});write(sources/"light.daability",ability);
        auto bad_schema=schema;bad_schema["attributes"][2]["id"]=2;write(sources/"base.daattributes",bad_schema);rejects([&]{service.cook("light.daability");});write(sources/"base.daattributes",schema);
        auto changed_action=action;changed_action["priority"]=21;write(sources/"attack.daaction",changed_action);check(service.cook("light.daability").changed,"Dependency-only edit invalidates parent");service.package(ability_id,root/"changed.json");auto changed=load_cooked_ability(root/"changed.json",service.cas_path(),ability_id);check(changed.definition->generation!=original_generation&&pinned.definition->generation==original_generation,"Complete closure generation changes while old lease stays pinned");
        // Independently cooked newer dependency cannot be mixed with the old parent.
        service.cook("attack.daaction");std::array<AssetId,1> other{AssetId::parse(action.at("asset").get<std::string>())};service.package(ability_id,root/"coherent.json",other);
        write(sources/"attack.daaction",action);service.cook("attack.daaction");rejects([&]{service.package(ability_id,root/"conflict.json",other);});
        auto manifest=read(root/"registry.json");
        auto mixed=manifest;auto newer_manifest=read(root/"changed.json");for(auto& record:mixed["assets"])if(record["kind"]=="action")for(auto& newer:newer_manifest["assets"])if(newer["kind"]=="action")record=newer;write(root/"bad.json",mixed);rejects([&]{load_cooked_ability(root/"bad.json",service.cas_path(),ability_id);});auto missing=manifest;missing["assets"].erase(0);write(root/"bad.json",missing);rejects([&]{load_cooked_ability(root/"bad.json",service.cas_path(),ability_id);});
        auto duplicate=manifest;duplicate["assets"].push_back(duplicate["assets"][0]);write(root/"bad.json",duplicate);rejects([&]{load_cooked_ability(root/"bad.json",service.cas_path(),ability_id);});
        auto corrupt=manifest;for(auto& record:corrupt["assets"])if(record["kind"]=="action")record["extension"]="../action.json";write(root/"bad.json",corrupt);rejects([&]{load_cooked_ability(root/"bad.json",service.cas_path(),ability_id);});
        auto record=manifest["assets"][0];auto artifact=service.cas_path()/(record["sha256"].get<std::string>()+"."+record["extension"].get<std::string>());std::ifstream in(artifact,std::ios::binary);std::string bytes{std::istreambuf_iterator<char>(in),{}};in.close();std::ofstream(artifact)<<"corrupt";rejects([&]{load_cooked_ability(root/"registry.json",service.cas_path(),ability_id);});std::ofstream(artifact,std::ios::binary)<<bytes;
        // Runtime receives no source mount or catalog. Altered/missing sources are irrelevant.
        std::filesystem::rename(sources,root/"offline-sources");pinned=load_cooked_ability(root/"registry.json",service.cas_path(),ability_id);std::filesystem::rename(root/"offline-sources",sources);
    }
    {AssetService fresh(sources,root/"fresh");fresh.cook("light.daability");fresh.package(ability_id,root/"fresh.json");check(load_cooked_ability(root/"fresh.json",fresh.cas_path(),ability_id).definition->generation==original_generation,"Fresh cache reproduces semantic generation");}
    WorldSession server(SessionRole::Server,{1,std::string(64,'a'),std::string(64,'b'),77});ObjectData object;object.id={4,1};auto network=server.create(object);auto owner=server.configure_abilities(network,pinned.attributes.definitions(),2,1);
    InputProfile input;input.id=AssetId::random();auto kit=std::make_shared<CombatKitDefinition>();kit->id=AssetId::random();kit->generation="kit";kit->locomotion_stance=AssetId::random();for(unsigned i=0;i<combat_slot_count;++i){input.actions.push_back({i+1,"combat."+std::to_string(i),InputActionKind::Button});kit->slots[i]={static_cast<CombatSlot>(i),i+1,{}};}kit->slots[0].ability=ability_id;
    std::array<std::shared_ptr<const AbilityDefinition>,1> catalogue{pinned.definition};server.equip_combat_kit(owner,kit,input,catalogue);
    AbilityRequest request{owner,1,server.ability_snapshot(owner).grant_generation,CombatSlot::Light,{1,InputEdge::Pressed,1000,0,1,false},false};auto receipt=server.request_ability(request);check(receipt.committed&&server.request_ability(request).duplicate,"Cooked ability uses existing authoritative operation commitment");
    auto snapshot=server.ability_snapshot(owner);check(snapshot.attributes[2].value==40&&snapshot.cooldowns[0].second==6,"Cooked resource/action/cooldown commits once");check(server.drain_ability_actions().size()==1,"Cooked action enters existing lifecycle exactly once");
    std::cout<<"Versioned ability/attribute frozen closure, failed cook preservation, generation pinning and WorldSession commitment passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
