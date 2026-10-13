#include <darkangel/world_ledger.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <limits>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
// A lever with two logical properties under different policies, as the brief's gate example: unlocked for good, open until a rest.
struct Gate final:PersistentObject {
    bool unlocked{},open{};unsigned uses{};
    std::string_view kind()const override{return "gate";}
    void capture(LedgerWriter& out)const override{if(unlocked)out.set("unlocked",true,{LedgerScope::Save});if(open)out.set("open",true,{LedgerScope::Save,LedgerReset::Rest});if(uses)out.set("uses",std::int64_t(uses),{LedgerScope::Session,LedgerReset::SceneEntry});}
    void restore(const LedgerReader& in)override{unlocked=in.flag("unlocked").value_or(false);open=in.flag("open").value_or(false);uses=unsigned(in.integer("uses").value_or(0));}
};
}
int main(){try{
    const AssetId scene_a=AssetId::random(),scene_b=AssetId::random();const PersistentId lever{scene_a,{1,10}},door{scene_a,{1,11}},altar{scene_b,{1,10}},party{{},{7,1}};
    const std::array<LedgerKind,3> known{LedgerKind{"gate",1},LedgerKind{"interactable",1},LedgerKind{"party",1}};
    // Write, read, erase. An absent property is "as authored"; the same identity in another scene is another object.
    {WorldLedger ledger;check(!ledger.find(lever)&&!ledger.value(lever,"used")&&ledger.revision()==0,"An empty ledger holds something");
        ledger.set(lever,"interactable",1,"used",std::int64_t(1),{LedgerScope::Save});const auto first=ledger.revision();
        check(ledger.find(lever)&&ledger.find(lever)->kind=="interactable"&&std::get<std::int64_t>(*ledger.value(lever,"used"))==1&&!ledger.value(altar,"used")&&first==1,"A written property was not read back, or leaked to another scene");
        ledger.set(lever,"interactable",1,"used",std::int64_t(1),{LedgerScope::Save});check(ledger.revision()==first,"Writing the same value again counted as a change");
        ledger.set(lever,"interactable",1,"used",std::int64_t(2),{LedgerScope::Save});check(std::get<std::int64_t>(*ledger.value(lever,"used"))==2&&ledger.revision()==first+1,"A changed value was not stored");
        ledger.set(party,"party",1,"checkpoint",std::string("altar"),{LedgerScope::Save});check(ledger.find(party)&&ledger.records().begin()->first==party,"A party-level record with a nil scene was not kept, or does not sort first");
        check(ledger.erase(lever,"used")&&!ledger.find(lever)&&!ledger.erase(lever,"used")&&ledger.erase(party)&&ledger.records().empty(),"Erasing did not return the record to as authored");
        check(rejects([&]{ledger.set(lever,"Interactable",1,"used",true,{});})&&rejects([&]{ledger.set(lever,"interactable",1,"Used Up",true,{});})&&rejects([&]{ledger.set(lever,"interactable",0,"used",true,{});})&&rejects([&]{ledger.set({scene_a,{}},"interactable",1,"used",true,{});})&&rejects([&]{ledger.set(lever,"interactable",1,"used",std::string(300,'x'),{});})&&rejects([&]{ledger.set(lever,"interactable",1,"used",std::numeric_limits<double>::infinity(),{});}),"An invalid kind, property, schema, identity or value was accepted");
        for(unsigned index=0;index<ledger_property_limit;++index)ledger.set(door,"gate",1,"p"+std::to_string(index),true,{});check(rejects([&]{ledger.set(door,"gate",1,"one_more",true,{});}),"A record took more properties than its bound");}
    // The capture and restore interface: one mechanism, per-property policies, nothing recorded for a default state.
    {WorldLedger ledger;Gate gate;ledger.capture(door,gate);check(!ledger.find(door),"An object in its authored state left a record");
        gate.unlocked=gate.open=true;gate.uses=3;ledger.capture(door,gate);const auto* record=ledger.find(door);
        check(record&&record->kind=="gate"&&record->properties.size()==3&&record->properties.at("unlocked").policy==LedgerPolicy{LedgerScope::Save}&&record->properties.at("open").policy.on(LedgerReset::Rest)&&record->properties.at("uses").policy.scope==LedgerScope::Session,"Capture did not write each property with its own policy");
        Gate loaded;check(ledger.restore(door,loaded)&&loaded.unlocked&&loaded.open&&loaded.uses==3,"Restore did not bring the logical state back");
        gate.open=false;ledger.capture(door,gate);check(ledger.find(door)->properties.size()==2&&!ledger.value(door,"open"),"A property the object no longer reports stayed in its record");
        Gate elsewhere;elsewhere.unlocked=true;check(ledger.restore(altar,elsewhere)&&!elsewhere.unlocked,"An object without a record was not restored to as authored");}
    // Resets: per property, in a fixed order, idempotent, and over a scene that is not loaded (the ledger never knew it was).
    {WorldLedger ledger;Gate gate;gate.unlocked=gate.open=true;gate.uses=2;ledger.capture(door,gate);ledger.capture(altar,gate);ledger.set(lever,"interactable",1,"used",std::int64_t(1),{LedgerScope::Save,LedgerReset::Rest|LedgerReset::PartyWipe});ledger.set(party,"party",1,"checkpoint",std::string("x"),{LedgerScope::Save});
        check(rejects([&]{ledger.reset(LedgerReset::SceneEntry);}),"A scene entry reset ran without a scene");
        const auto entered=ledger.reset(LedgerReset::SceneEntry,scene_a);check(entered.size()==1&&entered[0]==door&&!ledger.value(door,"uses")&&ledger.value(altar,"uses")&&ledger.value(door,"open"),"Scene entry did not reset exactly that scene's scene-entry properties");
        const auto before=ledger.revision();const auto rested=ledger.reset(LedgerReset::Rest);
        // Order: scene, then kind, then object. Scene A's gate before its interactable; scene B after, whichever identity sorts first.
        const bool a_first=scene_a<scene_b;check(rested.size()==3&&rested[a_first?0:1]==door&&rested[a_first?1:2]==lever&&rested[a_first?2:0]==altar,"A rest did not reset in scene, kind, object order");
        check(!ledger.value(door,"open")&&std::get<bool>(*ledger.value(door,"unlocked"))&&!ledger.find(lever)&&!ledger.value(altar,"open")&&ledger.value(altar,"unlocked")&&ledger.find(party)&&ledger.revision()>before,"A rest reset a permanent property, kept a resettable one, or left an empty record");
        const auto settled=ledger.revision();check(ledger.reset(LedgerReset::Rest).empty()&&ledger.revision()==settled&&ledger.reset(LedgerReset::PartyWipe).empty(),"Running the same reset again changed the ledger");}
    // Serialization: versioned, sorted, save scope only by default, exact round trip.
    {WorldLedger ledger;Gate gate;gate.unlocked=gate.open=true;gate.uses=4;ledger.capture(door,gate);ledger.set(altar,"interactable",1,"place",LedgerTransform{1.5,-2,3.25,.5},{LedgerScope::Save});ledger.set(altar,"interactable",1,"ratio",.1,{LedgerScope::Save});ledger.set(altar,"interactable",1,"label",std::string("north \"gate\""),{LedgerScope::Save,LedgerReset::PartyWipe});ledger.set(party,"party",1,"count",std::int64_t(-3),{LedgerScope::Save});
        const auto saved=ledger.serialize();const auto document=Json::parse(saved);check(document.at("ledger")==1&&document.at("records").size()==3&&!document.at("records")[0].contains("scene")&&saved.find("\"uses\"")==std::string::npos,"The saved ledger is not versioned, lost a record, or carried a session property");
        const auto loaded=WorldLedger::deserialize(saved,known);check(loaded.serialize()==saved&&loaded.records().size()==3&&!loaded.value(door,"uses")&&std::get<LedgerTransform>(*loaded.value(altar,"place"))==LedgerTransform{1.5,-2,3.25,.5}&&std::get<double>(*loaded.value(altar,"ratio"))==.1&&std::get<std::string>(*loaded.value(altar,"label"))=="north \"gate\""&&std::get<std::int64_t>(*loaded.value(party,"count"))==-3&&loaded.find(altar)->properties.at("label").policy.on(LedgerReset::PartyWipe),"The ledger did not survive a round trip exactly");
        const auto whole=WorldLedger::deserialize(ledger.serialize(true),known);check(std::get<std::int64_t>(*whole.value(door,"uses"))==4&&whole.find(door)->properties.at("uses").policy==LedgerPolicy{LedgerScope::Session,LedgerReset::SceneEntry},"A session property did not survive the full form");
        // A newer container is refused whole; malformed records are refused.
        auto newer=document;newer["ledger"]=2;check(rejects([&]{WorldLedger::deserialize(newer.dump(),known);}),"A ledger from a newer version was accepted");
        for(auto change:{+[](Json& value){value["records"][1]["properties"]["open"]["type"]="colour";},+[](Json& value){value["records"][1]["properties"]["open"]["resets"]=Json::array({"rest","rest"});},+[](Json& value){value["records"][1]["properties"]["open"]["scope"]="cloud";},+[](Json& value){value["records"][1]["object"]="";},+[](Json& value){value["records"].push_back(value["records"][1]);},+[](Json& value){value["records"][1]["extra"]=1;},+[](Json& value){value["extra"]=1;},+[](Json& value){value["records"][1]["properties"]["Open"]=value["records"][1]["properties"]["open"];}}){auto broken=document;change(broken);check(rejects([&]{WorldLedger::deserialize(broken.dump(),known);}),"A malformed ledger was accepted");}
        // An unknown kind and a newer schema are kept verbatim, reported, never written and never dropped.
        auto future=document;future["records"][1]["schema"]=2;future["records"][1]["properties"]["hinge"]={{"type","quaternion"},{"value",7}};future["records"][2]["kind"]="weather";
        auto kept=WorldLedger::deserialize(future.dump(),known);const auto foreign=kept.foreign();const PersistentId first_scene=scene_a<scene_b?door:altar,second_scene=scene_a<scene_b?altar:door;
        check(foreign.size()==2&&foreign[0]==first_scene&&foreign[1]==second_scene&&Json::parse(kept.serialize())==future,"Records this build does not understand were dropped or rewritten");
        Gate unreadable;unreadable.unlocked=true;check(!kept.restore(door,unreadable)&&!unreadable.unlocked,"An object was restored from a record this build does not understand");
        Gate fresh;fresh.open=true;kept.capture(door,fresh);check(rejects([&]{kept.set(door,"gate",1,"open",true,{});})&&kept.reset(LedgerReset::Rest).empty()&&Json::parse(kept.serialize())==future,"A foreign record was overwritten by a capture, a write or a reset");}
    std::cout<<"World ledger: values under persistent identities, capture and restore through one interface, per-property scope and reset policies, ordered idempotent resets over unloaded scenes, versioned JSON round trip, newer container refused, unknown kinds and newer schemas kept verbatim\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
