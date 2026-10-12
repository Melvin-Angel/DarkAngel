#include <darkangel/behavior.hpp>
#include <nlohmann/json.hpp>
#include <Luau/Compiler.h>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void fields(const Json& value,std::initializer_list<const char*> keys){require(value.is_object()&&value.size()==keys.size(),"Behavior node field count");for(auto key:keys)require(value.contains(key),"Behavior node missing/unknown field");}
CombatSlot slot(const Json& value){
    require(value.is_string(),"Behavior attack slot type");return behavior_slot(value.get<std::string>());
}
BehaviorNode node(const Json& value,unsigned depth,unsigned& count){
    require(value.is_object()&&value.contains("type")&&value.at("type").is_string()&&depth<=8&&++count<=64,"Behavior tree depth/size bounds");BehaviorNode result;result.type=value.at("type").get<std::string>();
    if(result.type=="sequence"||result.type=="selector"){fields(value,{"type","children"});const auto& children=value.at("children");require(children.is_array()&&!children.empty()&&children.size()<=8,"Behavior composite needs one to eight children");for(const auto& child:children)result.children.push_back(node(child,depth+1,count));}
    else if(result.type=="opponent_alive"||result.type=="opponent_visible"||result.type=="idle"||result.type=="face"||result.type=="investigate")fields(value,{"type"});
    else if(result.type=="in_range"||result.type=="approach"){fields(value,{"type","range"});if(value.at("range").is_string()){require(value.at("range")=="preferred","Behavior range must be a number or \"preferred\"");result.preferred=true;}else{require(value.at("range").is_number(),"Behavior range type");result.range=value.at("range").get<double>();require(std::isfinite(result.range)&&result.range>0&&result.range<=60,"Behavior range bounds");}}
    else if(result.type=="attack"){fields(value,{"type","slot"});result.slot=slot(value.at("slot"));}
    else if(result.type=="script"){fields(value,{"type","name","source"});require(value.at("name").is_string()&&value.at("source").is_string(),"Behavior script name/source type");result.name=value.at("name").get<std::string>();result.source=value.at("source").get<std::string>();require(!result.name.empty()&&result.name.size()<=behavior_script_name_limit,"Behavior script name bounds (1 to 48 characters)");compile_behavior_script(result.source);}
    else if(result.type=="recover"){fields(value,{"type","ticks"});require(value.at("ticks").is_number_unsigned()&&value.at("ticks").get<std::uint64_t>()>=1&&value.at("ticks").get<std::uint64_t>()<=3600,"Behavior recover ticks bounds");result.ticks=value.at("ticks").get<unsigned>();}
    else throw std::runtime_error("Unknown behavior node type");
    return result;
}
}
CombatSlot behavior_slot(std::string_view name){
    static const char* names[]={"light","heavy","ranged_light","ranged_heavy","spell1","spell2","block","parry"};
    for(unsigned index=0;index<canonical_combat_slot_count;++index)if(name==names[index])return static_cast<CombatSlot>(index);throw std::runtime_error("Behavior attack slot name");
}
std::string compile_behavior_script(std::string_view source){
    require(!source.empty()&&source.size()<=behavior_script_source_limit,"Behavior script source bounds (1 to 4096 bytes)");auto bytecode=Luau::compile(std::string(source));
    // A failed compilation is a zero byte followed by the message.
    require(!bytecode.empty(),"Behavior script compiled to nothing");if(bytecode[0]==0)throw std::runtime_error("Behavior script does not compile: "+bytecode.substr(1));return bytecode;
}
void validate_behavior_tuning(const BehaviorTuning& tuning){require(std::isfinite(tuning.preferred_range)&&tuning.preferred_range>0&&tuning.preferred_range<=60&&std::isfinite(tuning.recovery_scale)&&tuning.recovery_scale>=.25&&tuning.recovery_scale<=4,"NPC tuning bounds: preferred range up to 60 m, recovery scale 0.25 to 4");}
BehaviorDefinition decode_behavior(std::string_view text){
    require(text.size()<=65536,"Behavior source size bound");auto value=Json::parse(text);fields(value,{"schema","kind","asset","name","tree"});require(value.at("schema")==1&&value.at("kind")=="behavior","Behavior definition schema/type");
    BehaviorDefinition result;result.id=AssetId::parse(value.at("asset").get<std::string>());require(result.id!=AssetId{},"Behavior identity");require(value.at("name").is_string(),"Behavior name type");result.name=value.at("name").get<std::string>();require(!result.name.empty()&&result.name.size()<=64,"Behavior display name bounds");
    unsigned count{};result.root=node(value.at("tree"),1,count);std::size_t scripts{};const auto tally=[&](const auto& self,const BehaviorNode& at)->void{if(at.type=="script")++scripts;for(const auto& child:at.children)self(self,child);};tally(tally,result.root);require(scripts<=behavior_script_limit,"A behavior tree holds at most eight script leaves");return result;
}
std::string default_behavior_tree_json(){
    return R"({"type":"selector","children":[{"type":"sequence","children":[{"type":"opponent_alive"},{"type":"in_range","range":1.4},{"type":"idle"},{"type":"face"},{"type":"attack","slot":"light"},{"type":"recover","ticks":90}]},{"type":"sequence","children":[{"type":"opponent_alive"},{"type":"approach","range":1.4}]}]})";
}
}
