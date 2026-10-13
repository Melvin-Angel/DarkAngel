#include <darkangel/world_ledger.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace darkangel {
namespace {
using Json=nlohmann::ordered_json;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void check_value(const LedgerValue& value){
    if(const auto* real=std::get_if<double>(&value))require(std::isfinite(*real),"A ledger number is finite");
    if(const auto* text=std::get_if<std::string>(&value))require(text->size()<=ledger_text_limit,"A ledger text value is at most 256 characters");
    if(const auto* place=std::get_if<LedgerTransform>(&value))require(std::isfinite(place->x)&&std::isfinite(place->y)&&std::isfinite(place->z)&&std::isfinite(place->yaw),"A ledger transform is finite");
}
void check_policy(LedgerPolicy policy){require((policy.scope==LedgerScope::Session||policy.scope==LedgerScope::Save)&&policy.resets<8,"Ledger policy");}
constexpr std::pair<LedgerReset,const char*> reset_names[]{{LedgerReset::SceneEntry,"scene_entry"},{LedgerReset::Rest,"rest"},{LedgerReset::PartyWipe,"party_wipe"}};
Json encode(const LedgerProperty& property){
    Json out=Json::object();
    std::visit([&](const auto& held){using T=std::decay_t<decltype(held)>;
        if constexpr(std::is_same_v<T,bool>){out["type"]="bool";out["value"]=held;}
        else if constexpr(std::is_same_v<T,std::int64_t>){out["type"]="integer";out["value"]=held;}
        else if constexpr(std::is_same_v<T,double>){out["type"]="real";out["value"]=held;}
        else if constexpr(std::is_same_v<T,std::string>){out["type"]="text";out["value"]=held;}
        else{out["type"]="transform";out["value"]=Json::array({held.x,held.y,held.z,held.yaw});}},property.value);
    out["scope"]=property.policy.scope==LedgerScope::Save?"save":"session";Json resets=Json::array();for(const auto& [trigger,name]:reset_names)if(property.policy.on(trigger))resets.push_back(name);out["resets"]=std::move(resets);return out;
}
LedgerProperty decode(const Json& in){
    require(in.is_object()&&in.size()==4&&in.contains("type")&&in.contains("value")&&in.contains("scope")&&in.contains("resets")&&in.at("type").is_string()&&in.at("scope").is_string()&&in.at("resets").is_array(),"Ledger property shape");
    LedgerProperty property;const auto type=in.at("type").get<std::string>();const auto& value=in.at("value");
    if(type=="bool"){require(value.is_boolean(),"Ledger bool value");property.value=value.get<bool>();}
    else if(type=="integer"){require(value.is_number_integer(),"Ledger integer value");property.value=value.get<std::int64_t>();}
    else if(type=="real"){require(value.is_number(),"Ledger real value");property.value=value.get<double>();}
    else if(type=="text"){require(value.is_string(),"Ledger text value");property.value=value.get<std::string>();}
    else if(type=="transform"){require(value.is_array()&&value.size()==4&&std::all_of(value.begin(),value.end(),[](const Json& part){return part.is_number();}),"Ledger transform value");property.value=LedgerTransform{value[0].get<double>(),value[1].get<double>(),value[2].get<double>(),value[3].get<double>()};}
    else throw std::runtime_error("Ledger property type");
    check_value(property.value);
    const auto scope=in.at("scope").get<std::string>();require(scope=="save"||scope=="session","Ledger property scope");property.policy.scope=scope=="save"?LedgerScope::Save:LedgerScope::Session;
    for(const auto& item:in.at("resets")){require(item.is_string(),"Ledger reset name");const auto name=item.get<std::string>();bool known=false;for(const auto& [trigger,text]:reset_names)if(name==text){require(!property.policy.on(trigger),"Ledger reset named twice");property.policy.resets=property.policy.resets|trigger;known=true;}require(known,"Ledger reset name");}
    return property;
}
}
bool ledger_name_valid(std::string_view name){return !name.empty()&&name.size()<=32&&std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});}
void LedgerWriter::set(std::string_view name,LedgerValue value,LedgerPolicy policy){ledger_.set(id_,kind_,schema_,name,std::move(value),policy);}
void LedgerWriter::clear(std::string_view name){ledger_.erase(id_,name);}
const LedgerValue* LedgerReader::find(std::string_view name)const{if(!record_)return nullptr;auto found=record_->properties.find(name);return found==record_->properties.end()?nullptr:&found->second.value;}
std::optional<bool> LedgerReader::flag(std::string_view name)const{const auto* value=find(name);if(const auto* held=value?std::get_if<bool>(value):nullptr)return *held;return std::nullopt;}
std::optional<std::int64_t> LedgerReader::integer(std::string_view name)const{const auto* value=find(name);if(const auto* held=value?std::get_if<std::int64_t>(value):nullptr)return *held;return std::nullopt;}
std::optional<double> LedgerReader::real(std::string_view name)const{const auto* value=find(name);if(const auto* held=value?std::get_if<double>(value):nullptr)return *held;return std::nullopt;}
const std::string* LedgerReader::text(std::string_view name)const{const auto* value=find(name);return value?std::get_if<std::string>(value):nullptr;}
std::optional<LedgerTransform> LedgerReader::transform(std::string_view name)const{const auto* value=find(name);if(const auto* held=value?std::get_if<LedgerTransform>(value):nullptr)return *held;return std::nullopt;}
void WorldLedger::set(PersistentId id,std::string_view kind,unsigned schema,std::string_view property,LedgerValue value,LedgerPolicy policy){
    require(bool(id.object)&&ledger_name_valid(kind)&&schema>=1&&ledger_name_valid(property),"Ledger record identity, kind or property name");check_value(value);check_policy(policy);
    auto found=records_.find(id);
    if(found==records_.end()){require(records_.size()<ledger_record_limit,"The ledger is full");LedgerRecord record;record.id=id;found=records_.emplace(id,std::move(record)).first;}
    auto& record=found->second;require(record.foreign.empty(),"A ledger record this build does not understand cannot be written");
    require(record.properties.contains(property)||record.properties.size()<ledger_property_limit,"A ledger record holds at most 32 properties");
    record.kind=std::string(kind);record.schema=schema;LedgerProperty next{std::move(value),policy};auto held=record.properties.find(property);
    if(held!=record.properties.end()){if(held->second==next)return;held->second=std::move(next);}else record.properties.emplace(std::string(property),std::move(next));
    ++revision_;
}
bool WorldLedger::erase(PersistentId id,std::string_view property){
    auto found=records_.find(id);if(found==records_.end()||!found->second.foreign.empty())return false;auto held=found->second.properties.find(property);if(held==found->second.properties.end())return false;
    found->second.properties.erase(held);if(found->second.properties.empty())records_.erase(found);++revision_;return true;
}
bool WorldLedger::erase(PersistentId id){if(records_.erase(id)==0)return false;++revision_;return true;}
const LedgerRecord* WorldLedger::find(PersistentId id)const{auto found=records_.find(id);return found==records_.end()?nullptr:&found->second;}
const LedgerValue* WorldLedger::value(PersistentId id,std::string_view property)const{const auto* record=find(id);if(!record)return nullptr;auto held=record->properties.find(property);return held==record->properties.end()?nullptr:&held->second.value;}
void WorldLedger::capture(PersistentId id,const PersistentObject& object){
    require(bool(id.object)&&ledger_name_valid(object.kind())&&object.schema()>=1,"Ledger capture identity or kind");
    // A foreign record belongs to a build that understands it: it is left exactly as it is.
    if(const auto* held=find(id)){if(!held->foreign.empty())return;erase(id);}
    LedgerWriter writer(*this,id,object.kind(),object.schema());object.capture(writer);
}
bool WorldLedger::restore(PersistentId id,PersistentObject& object)const{
    const auto* record=find(id);if(record&&(!record->foreign.empty()||record->kind!=object.kind()||record->schema>object.schema())){object.restore(LedgerReader(nullptr));return false;}
    object.restore(LedgerReader(record));return true;
}
std::vector<PersistentId> WorldLedger::reset(LedgerReset trigger,std::optional<AssetId> scene){
    require(trigger==LedgerReset::SceneEntry||trigger==LedgerReset::Rest||trigger==LedgerReset::PartyWipe,"Ledger reset trigger");require(trigger!=LedgerReset::SceneEntry||scene.has_value(),"A scene entry reset names its scene");
    // Fixed order: scene, kind, object. Within a record, properties in name order.
    std::vector<const LedgerRecord*> order;for(const auto& [id,record]:records_)if(record.foreign.empty()&&(!scene||id.scene==*scene))order.push_back(&record);
    std::sort(order.begin(),order.end(),[](const LedgerRecord* a,const LedgerRecord* b){return std::tie(a->id.scene,a->kind,a->id.object)<std::tie(b->id.scene,b->kind,b->id.object);});
    std::vector<PersistentId> touched;for(const auto* record:order)if(std::any_of(record->properties.begin(),record->properties.end(),[&](const auto& item){return item.second.policy.on(trigger);}))touched.push_back(record->id);
    for(const auto& id:touched){auto& record=records_.at(id);std::erase_if(record.properties,[&](const auto& item){return item.second.policy.on(trigger);});if(record.properties.empty())records_.erase(id);++revision_;}
    return touched;
}
std::vector<PersistentId> WorldLedger::foreign()const{std::vector<PersistentId> result;for(const auto& [id,record]:records_)if(!record.foreign.empty())result.push_back(id);return result;}
std::string WorldLedger::serialize(bool with_session)const{
    Json records=Json::array();
    for(const auto& [id,record]:records_){
        if(!record.foreign.empty()){records.push_back(Json::parse(record.foreign));continue;}
        Json properties=Json::object();for(const auto& [name,property]:record.properties)if(with_session||property.policy.scope==LedgerScope::Save)properties[name]=encode(property);
        if(properties.empty())continue;
        Json out=Json::object();if(id.scene!=AssetId{})out["scene"]=id.scene.text();out["object"]=id.object.text();out["kind"]=record.kind;out["schema"]=record.schema;out["properties"]=std::move(properties);records.push_back(std::move(out));}
    Json document=Json::object();document["ledger"]=version;document["records"]=std::move(records);return document.dump(1);
}
WorldLedger WorldLedger::deserialize(std::string_view text,std::span<const LedgerKind> known){
    require(text.size()<=ledger_document_limit,"Ledger document size bound");const auto document=Json::parse(text);
    require(document.is_object()&&document.contains("ledger")&&document.at("ledger").is_number_unsigned(),"Ledger document shape");
    require(document.at("ledger").get<unsigned>()<=version,"The ledger was written by a newer version of the engine");require(document.at("ledger").get<unsigned>()==version&&document.size()==2&&document.contains("records")&&document.at("records").is_array()&&document.at("records").size()<=ledger_record_limit,"Ledger document shape");
    WorldLedger ledger;
    for(const auto& in:document.at("records")){
        require(in.is_object()&&in.contains("object")&&in.at("object").is_string()&&in.contains("kind")&&in.at("kind").is_string()&&in.contains("schema")&&in.at("schema").is_number_unsigned()&&in.contains("properties")&&in.at("properties").is_object()&&(!in.contains("scene")||in.at("scene").is_string()),"Ledger record shape");
        LedgerRecord record;if(in.contains("scene")){record.id.scene=AssetId::parse(in.at("scene").get<std::string>());require(record.id.scene!=AssetId{},"Ledger record scene");}record.id.object=StableId::parse(in.at("object").get<std::string>());record.kind=in.at("kind").get<std::string>();record.schema=in.at("schema").get<unsigned>();
        require(bool(record.id.object)&&ledger_name_valid(record.kind)&&record.schema>=1&&!ledger.records_.contains(record.id),"Ledger record identity");
        const auto kind=std::find_if(known.begin(),known.end(),[&](const LedgerKind& item){return item.name==record.kind;});
        if(kind==known.end()||record.schema>kind->schema)record.foreign=in.dump();
        else{require(in.size()==(in.contains("scene")?5u:4u)&&in.at("properties").size()<=ledger_property_limit&&!in.at("properties").empty(),"Ledger record shape");for(const auto& [name,property]:in.at("properties").items()){require(ledger_name_valid(name),"Ledger property name");record.properties.emplace(name,decode(property));}}
        const auto id=record.id;ledger.records_.emplace(id,std::move(record));}
    return ledger;
}
}
