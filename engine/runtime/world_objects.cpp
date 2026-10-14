#include <darkangel/world_objects.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace darkangel {
namespace {
using Json=nlohmann::json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
Json parse(std::string_view text,const char* what){require(text.size()<=8192,what);auto value=Json::parse(text,nullptr,false);require(!value.is_discarded()&&value.is_object(),what);return value;}
void known(const Json& value,std::initializer_list<const char*> fields,const char* error){for(const auto& [key,field]:value.items())require(std::any_of(fields.begin(),fields.end(),[&](const char* name){return key==name;}),error);}
std::string field_text(const Json& value,const char* key,std::string fallback,std::size_t limit,const char* error){if(!value.contains(key))return fallback;require(value.at(key).is_string(),error);auto result=value.at(key).get<std::string>();require(!result.empty()&&result.size()<=limit,error);return result;}
double number(const Json& value,const char* key,double fallback,double low,double high,const char* error){if(!value.contains(key))return fallback;require(value.at(key).is_number(),error);const auto result=value.at(key).get<double>();require(std::isfinite(result)&&result>=low&&result<=high,error);return result;}
bool flag(const Json& value,const char* key,bool fallback,const char* error){if(!value.contains(key))return fallback;require(value.at(key).is_boolean(),error);return value.at(key).get<bool>();}
MotorVec vector(const Json& value,const char* key,MotorVec fallback,double low,double high,const char* error){if(!value.contains(key))return fallback;const auto& v=value.at(key);require(v.is_array()&&v.size()==3&&v[0].is_number()&&v[1].is_number()&&v[2].is_number(),error);MotorVec result{v[0].get<double>(),v[1].get<double>(),v[2].get<double>()};for(double c:{result.x,result.y,result.z})require(std::isfinite(c)&&c>=low&&c<=high,error);return result;}
LedgerPolicy policy(const Json& value,const char* key,LedgerPolicy fallback){if(!value.contains("policies")||!value.at("policies").contains(key))return fallback;return decode_policy(value.at("policies").at(key).dump());}
constexpr std::pair<LedgerReset,const char*> resets[]{{LedgerReset::SceneEntry,"scene_entry"},{LedgerReset::Rest,"rest"},{LedgerReset::PartyWipe,"party_wipe"}};
Json destination(const WorldDestination& d){return Json{{"scene",d.scene.text()},{"altar",d.altar.text()},{"name",d.name}};}
WorldDestination destination(const Json& j){require(j.is_object()&&j.size()==3&&j.at("name").is_string()&&j.at("name").get<std::string>().size()<=48,"World destination shape");return {AssetId::parse(j.at("scene").get<std::string>()),StableId::parse(j.at("altar").get<std::string>()),j.at("name").get<std::string>()};}
Json ids(const std::vector<StableId>& values){auto result=Json::array();for(auto id:values)result.push_back(id.text());return result;}
std::vector<StableId> ids(const Json& j){require(j.is_array()&&j.size()<=256,"World state identity list");std::vector<StableId> result;for(const auto& id:j)result.push_back(StableId::parse(id.get<std::string>()));return result;}
}

LedgerPolicy decode_policy(std::string_view text){
    const auto value=parse(text,"Policy shape");known(value,{"scope","resets"},"A policy has a scope and resets");
    LedgerPolicy result;const auto scope=value.value("scope",std::string("save"));require(scope=="save"||scope=="session","A policy's scope is \"save\" or \"session\"");result.scope=scope=="save"?LedgerScope::Save:LedgerScope::Session;
    if(value.contains("resets")){require(value.at("resets").is_array()&&value.at("resets").size()<=3,"A policy's resets are a list");for(const auto& name:value.at("resets")){require(name.is_string(),"Reset trigger name");auto found=std::find_if(std::begin(resets),std::end(resets),[&](const auto& r){return name==r.second;});require(found!=std::end(resets),"A reset trigger is scene_entry, rest or party_wipe");result.resets=std::uint8_t(result.resets|std::uint8_t(found->first));}}
    return result;
}
std::string encode_policy(const LedgerPolicy& value){Json result{{"scope",value.scope==LedgerScope::Save?"save":"session"},{"resets",Json::array()}};for(const auto& [reset,name]:resets)if(value.on(reset))result["resets"].push_back(name);return result.dump();}

Requirement decode_requirement(std::string_view text){
    const auto value=parse(text,"Requirement shape");known(value,{"item","quantity","consume","tag","encounter"},"A requirement names an item, a tag or an encounter");Requirement result;
    if(value.contains("item")&&!value.at("item").is_null()){result.item=AssetId::parse(value.at("item").get<std::string>());result.quantity=unsigned(number(value,"quantity",1,1,1000,"A requirement's quantity is 1 to 1000"));result.consume=flag(value,"consume",false,"Requirement consume flag");}
    if(value.contains("tag")){result.tag=field_text(value,"tag","",64,"A requirement's tag is 1 to 64 characters");}
    if(value.contains("encounter")&&!value.at("encounter").is_null())result.encounter=StableId::parse(value.at("encounter").get<std::string>());
    return result;
}

PickupDefinition decode_pickup(std::string_view text){
    const auto value=parse(text,"Pickup shape");known(value,{"item","quantity","prompt","radius","policies"},"Pickup component has an unknown field");
    require(value.contains("item")&&value.at("item").is_string(),"A pickup names its item");PickupDefinition result;result.item=AssetId::parse(value.at("item").get<std::string>());require(result.item!=AssetId{},"Pickup item identity");
    result.quantity=unsigned(number(value,"quantity",1,1,1000,"A pickup's quantity is 1 to 1000"));result.prompt=field_text(value,"prompt","Pick up",48,"A pickup's prompt is 1 to 48 characters");result.radius=number(value,"radius",2,.5,10,"A pickup's radius is 0.5 to 10 m");
    result.collected=policy(value,"collected",result.collected);return result;
}

AltarDefinition decode_altar(std::string_view text){
    const auto value=parse(text,"Altar shape");known(value,{"name","prompt","radius","checkpoint","rest","fast_travel","save","arrival"},"Altar component has an unknown field");
    AltarDefinition result;result.name=field_text(value,"name","",48,"An altar's name is 1 to 48 characters");result.prompt=field_text(value,"prompt","Altar",48,"An altar's prompt is 1 to 48 characters");result.radius=number(value,"radius",2.5,.5,10,"An altar's radius is 0.5 to 10 m");
    const auto checkpoint=value.value("checkpoint",std::string("rest"));require(checkpoint=="never"||checkpoint=="discover"||checkpoint=="rest","An altar sets the checkpoint \"never\", on \"discover\" or on \"rest\"");result.checkpoint=checkpoint=="never"?AltarCheckpoint::Never:checkpoint=="discover"?AltarCheckpoint::Discover:AltarCheckpoint::Rest;
    result.rest=flag(value,"rest",true,"Altar rest flag");result.fast_travel=flag(value,"fast_travel",true,"Altar fast travel flag");result.save=flag(value,"save",true,"Altar save flag");
    if(value.contains("arrival")){const auto& a=value.at("arrival");require(a.is_array()&&a.size()==2&&a[0].is_number()&&a[1].is_number(),"An altar's arrival is [forward, right] in metres");result.forward=a[0].get<double>();result.right=a[1].get<double>();require(std::isfinite(result.forward)&&std::isfinite(result.right)&&std::abs(result.forward)<=10&&std::abs(result.right)<=10,"An altar's arrival is within 10 m");}
    require(!result.name.empty(),"An altar has a name");return result;
}
MotorVec altar_arrival(const AltarDefinition& altar,MotorVec position,double yaw){
    // Facing follows the scene convention: yaw 0 faces +z.
    const double fx=std::sin(yaw),fz=std::cos(yaw),rx=std::cos(yaw),rz=-std::sin(yaw);
    return {position.x+fx*altar.forward+rx*altar.right,position.y,position.z+fz*altar.forward+rz*altar.right};
}

MoverDefinition decode_mover(std::string_view text){
    const auto value=parse(text,"Mover shape");known(value,{"half","offset","speed","wait","radius","activation","behaviour","prompt","locked_prompt","event","locked","open","door","requires","travel","policies"},"Mover component has an unknown field");
    MoverDefinition result;result.door=flag(value,"door",true,"Mover door flag");
    if(!result.door){result.prompt="Activate";result.opened={LedgerScope::Session,LedgerReset::SceneEntry};}
    result.half=vector(value,"half",result.half,.05,50,"A mover's half extents are 0.05 to 50 m");result.offset=vector(value,"offset",result.offset,-200,200,"A mover's offset is within 200 m");
    require(std::hypot(result.offset.x,result.offset.y,result.offset.z)>=.01,"A mover moves at least 1 cm");
    result.speed=number(value,"speed",2,.05,50,"A mover's speed is 0.05 to 50 m/s");result.wait=number(value,"wait",2,0,600,"A mover waits 0 to 600 s");result.radius=number(value,"radius",2.5,.5,10,"A mover's radius is 0.5 to 10 m");
    const auto activation=value.value("activation",std::string("interact"));require(activation=="interact"||activation=="event"||activation=="auto","A mover is activated by \"interact\", \"event\" or \"auto\"");result.activation=activation=="interact"?MoverActivation::Interact:activation=="event"?MoverActivation::Event:MoverActivation::Auto;
    const auto behaviour=value.value("behaviour",std::string("toggle"));require(behaviour=="toggle"||behaviour=="return"||behaviour=="loop","A mover's behaviour is \"toggle\", \"return\" or \"loop\"");result.behaviour=behaviour=="toggle"?MoverBehaviour::Toggle:behaviour=="return"?MoverBehaviour::Return:MoverBehaviour::Loop;
    result.prompt=field_text(value,"prompt",result.prompt,48,"A mover's prompt is 1 to 48 characters");result.locked_prompt=field_text(value,"locked_prompt",result.locked_prompt,48,"A mover's locked prompt is 1 to 48 characters");result.event=value.contains("event")?field_text(value,"event","",64,"A mover's event is 1 to 64 characters"):std::string{};
    require(result.activation!=MoverActivation::Event||!result.event.empty(),"A mover activated by an event names it");
    result.locked=flag(value,"locked",false,"Mover locked flag");result.open=flag(value,"open",false,"Mover open flag");
    if(value.contains("requires"))result.requirement=decode_requirement(value.at("requires").dump());
    require(!result.locked||!result.requirement.empty(),"A locked mover has a requirement that unlocks it");
    require(!result.locked||result.activation==MoverActivation::Interact,"Only a mover used on purpose can be locked");
    if(value.contains("travel")&&!value.at("travel").is_null()){const auto& t=value.at("travel");require(t.is_object()&&t.contains("scene"),"A mover's travel names a scene");StableId entry;if(t.contains("entry")&&!t.at("entry").is_null())entry=StableId::parse(t.at("entry").get<std::string>());result.travel=std::pair{AssetId::parse(t.at("scene").get<std::string>()),entry};require(result.activation==MoverActivation::Interact,"Only a mover used on purpose leads to another scene");}
    result.unlocked=policy(value,"unlocked",result.unlocked);result.opened=policy(value,"open",result.opened);return result;
}

MoverState::MoverState(MoverDefinition definition):definition_(std::move(definition)),at_(definition_.open?1:0),target_(at_),unlocked_(!definition_.locked){}
bool MoverState::activate(){
    if(!unlocked_)return false;
    if(definition_.behaviour==MoverBehaviour::Toggle){target_=target_>0?0:1;return true;}
    if(at_!=target_||target_>0)return false;target_=1;waited_=0;return true;
}
void MoverState::restore(bool unlocked,bool open){unlocked_=unlocked||!definition_.locked;at_=target_=open?1:0;waited_=0;}
MotorVec MoverState::step(double seconds){
    if(definition_.activation==MoverActivation::Auto&&at_==target_&&target_==0&&definition_.behaviour!=MoverBehaviour::Toggle){waited_+=seconds;if(waited_>=definition_.wait){target_=1;waited_=0;}}
    if(at_==target_){
        // Return and loop wait at the end, then go back; loop also waits at the start (above, for Auto).
        if(target_>0&&definition_.behaviour!=MoverBehaviour::Toggle){waited_+=seconds;if(waited_>=definition_.wait){target_=0;waited_=0;}}
        if(at_==target_)return {};
    }
    const double length=std::hypot(definition_.offset.x,definition_.offset.y,definition_.offset.z),step=definition_.speed*seconds/length;
    const double before=at_;at_=target_>at_?std::min(target_,at_+step):std::max(target_,at_-step);const double moved=(at_-before)/seconds;
    return {definition_.offset.x*moved,definition_.offset.y*moved,definition_.offset.z*moved};
}

std::string encode_world_state(const WorldStateView& v){
    Json result{{"schema",1},{"revision",v.revision},{"collected",ids(v.collected)},{"discovered",ids(v.discovered)},{"unlocked",ids(v.unlocked)},{"open",ids(v.open)},{"members",Json::array()},{"destinations",Json::array()},{"wiped",v.wiped},{"wipe",v.wipe}};
    for(const auto& m:v.members)result["members"].push_back(Json{{"network",m.network},{"state",int(m.state)},{"revive",m.revive}});
    for(const auto& d:v.destinations)result["destinations"].push_back(destination(d));
    if(v.checkpoint)result["checkpoint"]=destination(*v.checkpoint);
    auto text=result.dump();require(text.size()<=24*1024,"World state view bound");return text;
}
WorldStateView decode_world_state(std::string_view text){
    require(text.size()<=24*1024,"World state view bound");const auto j=Json::parse(text);require(j.is_object()&&j.at("schema")==1,"World state schema");WorldStateView v;v.revision=j.at("revision").get<std::uint64_t>();
    v.collected=ids(j.at("collected"));v.discovered=ids(j.at("discovered"));v.unlocked=ids(j.at("unlocked"));v.open=ids(j.at("open"));v.wiped=j.at("wiped").get<bool>();v.wipe=j.at("wipe").get<double>();
    require(j.at("members").is_array()&&j.at("members").size()<=4&&j.at("destinations").is_array()&&j.at("destinations").size()<=128,"World state bounds");
    for(const auto& m:j.at("members")){const auto state=m.at("state").get<int>();require(state==0||state==1,"Party member state");v.members.push_back({m.at("network").get<std::uint64_t>(),PartyMemberState(state),std::clamp(m.at("revive").get<double>(),0.,1.)});}
    for(const auto& d:j.at("destinations"))v.destinations.push_back(destination(d));
    if(j.contains("checkpoint"))v.checkpoint=destination(j.at("checkpoint"));return v;
}
std::string encode_world_request(const WorldRequest& r){Json result{{"schema",1},{"kind",r.kind==WorldRequestKind::Rest?"rest":"travel"},{"altar",r.altar.text()}};if(r.kind==WorldRequestKind::Travel)result["destination"]=destination(r.destination);return result.dump();}
WorldRequest decode_world_request(std::string_view text){
    require(text.size()<=1024,"World request bound");const auto j=Json::parse(text);require(j.is_object()&&j.at("schema")==1,"World request schema");WorldRequest r;const auto kind=j.at("kind").get<std::string>();require(kind=="rest"||kind=="travel","World request kind");
    r.kind=kind=="rest"?WorldRequestKind::Rest:WorldRequestKind::Travel;r.altar=StableId::parse(j.at("altar").get<std::string>());if(r.kind==WorldRequestKind::Travel)r.destination=destination(j.at("destination"));return r;
}

std::vector<std::byte> world_bytes(std::string_view text){std::vector<std::byte> result(text.size());for(std::size_t i=0;i<text.size();++i)result[i]=std::byte(static_cast<unsigned char>(text[i]));return result;}
std::string world_text(std::span<const std::byte> bytes){std::string result(bytes.size(),char{});for(std::size_t i=0;i<bytes.size();++i)result[i]=char(std::to_integer<unsigned char>(bytes[i]));return result;}
PersistentId party_checkpoint_id(){return {AssetId{},StableId{0x6368656b706f696eULL,1}};}
std::optional<WorldDestination> read_checkpoint(const WorldLedger& ledger){
    const auto* scene=ledger.value(party_checkpoint_id(),"scene");const auto* altar=ledger.value(party_checkpoint_id(),"altar");const auto* name=ledger.value(party_checkpoint_id(),"name");
    if(!scene||!altar||!std::holds_alternative<std::string>(*scene)||!std::holds_alternative<std::string>(*altar))return std::nullopt;
    return WorldDestination{AssetId::parse(std::get<std::string>(*scene)),StableId::parse(std::get<std::string>(*altar)),name&&std::holds_alternative<std::string>(*name)?std::get<std::string>(*name):std::string{}};
}
void write_checkpoint(WorldLedger& ledger,const WorldDestination& at){const LedgerPolicy keep{LedgerScope::Save};ledger.set(party_checkpoint_id(),"checkpoint",1,"scene",at.scene.text(),keep);ledger.set(party_checkpoint_id(),"checkpoint",1,"altar",at.altar.text(),keep);ledger.set(party_checkpoint_id(),"checkpoint",1,"name",at.name,keep);}
void record_altar(WorldLedger& ledger,AssetId scene,StableId altar,const AltarDefinition& definition){const LedgerPolicy keep{LedgerScope::Save};const PersistentId id{scene,altar};ledger.set(id,"altar",1,"discovered",true,keep);ledger.set(id,"altar",1,"name",definition.name,keep);ledger.set(id,"altar",1,"fast_travel",definition.fast_travel,keep);}
bool altar_discovered(const WorldLedger& ledger,AssetId scene,StableId altar){const auto* value=ledger.value({scene,altar},"discovered");return value&&std::holds_alternative<bool>(*value)&&std::get<bool>(*value);}
std::vector<WorldDestination> fast_travel_destinations(const WorldLedger& ledger){
    std::vector<WorldDestination> result;for(const auto& [id,record]:ledger.records()){if(record.kind!="altar"||!record.foreign.empty())continue;auto flag=[&](const char* name){auto found=record.properties.find(name);return found!=record.properties.end()&&std::holds_alternative<bool>(found->second.value)&&std::get<bool>(found->second.value);};
        if(!flag("discovered")||!flag("fast_travel"))continue;auto name=record.properties.find("name");result.push_back({id.scene,id.object,name!=record.properties.end()&&std::holds_alternative<std::string>(name->second.value)?std::get<std::string>(name->second.value):std::string("Altar")});if(result.size()>=128)break;}
    return result;
}
}
