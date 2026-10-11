#include <darkangel/encounter.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
}
int main(){try{
    // World events: emitted on one tick, delivered on the next in emission order; tags are state.
    {WorldEvents world;world.emit("gate.opened");world.emit("bell.rang");check(world.delivered().empty()&&!world.happened("gate.opened"),"A world event was delivered on the tick it was emitted");
        world.begin_tick(1);check(world.delivered().size()==2&&world.delivered()[0]=="gate.opened"&&world.delivered()[1]=="bell.rang"&&world.happened("bell.rang"),"World events were not delivered in emission order on the next tick");
        world.begin_tick(2);check(world.delivered().empty()&&world.log().size()==2&&world.log()[0].tick==1,"A world event was delivered twice, or the log lost it");
        world.set_tag("gate.fight_active",true);check(world.tag("gate.fight_active")&&world.tags().size()==1,"A world tag was not set");world.set_tag("gate.fight_active",false);check(!world.tag("gate.fight_active"),"A world tag was not cleared");
        check(rejects([&]{world.emit("Gate Opened");})&&rejects([&]{world.emit("");})&&rejects([&]{world.set_tag(".x",true);}),"An invalid world name was accepted");
        check(rejects([&]{WorldEvents flood;for(unsigned index=0;index<=world_event_limit;++index)flood.emit("x");}),"More world events than the per-tick bound were accepted");}
    const AssetId guard=AssetId::random(),archer=AssetId::random();
    Json waves={{"trigger",{{"event","gate.opened"}}},{"mode","waves"},{"waves",Json::array({Json{{"npc",guard.text()},{"count",2}},Json{{"npc",archer.text()},{"count",3}}})},{"placement","points"},{"emits",{{"started","gate.fight_started"},{"wave_cleared","gate.wave_cleared"},{"completed","gate.cleared"}}},{"tag","gate.fight_active"}};
    {const auto decoded=decode_encounter(waves.dump());check(decoded.trigger_event=="gate.opened"&&!decoded.trigger_enter&&decoded.mode==EncounterMode::Waves&&decoded.waves.size()==2&&decoded.waves[1].npc==archer&&decoded.waves[1].count==3&&decoded.placement==EncounterPlacement::Points&&decoded.completed=="gate.cleared"&&decoded.tag=="gate.fight_active"&&decoded.enemies()==5&&decoded.concurrent()==3&&decoded.npcs().size()==2,"A wave encounter did not decode");
        for(auto change:{+[](Json& value){value["waves"]=Json::array();},+[](Json& value){value["waves"][0]["count"]=0;},+[](Json& value){value["trigger"]={{"event","gate.opened"},{"enter",true}};},+[](Json& value){value["trigger"]={{"enter",false}};},+[](Json& value){value["mode"]="siege";},+[](Json& value){value["placement"]="anywhere";},+[](Json& value){value["emits"]["started"]="Bad Name";},+[](Json& value){value["extra"]=1;},+[](Json& value){value.erase("placement");},+[](Json& value){value["timed"]={{"npc","x"}};}}){auto bad=waves;change(bad);check(rejects([&]{decode_encounter(bad.dump());}),"An invalid encounter was decoded");}}
    const std::vector<EncounterPoint> points{{{4,0,4},1.},{{-4,0,4},2.}};const auto no_ground=[](double,double,double){return std::optional<double>{};};const auto empty=[](MotorVec){return false;};
    {
        // Waves at spawn points: starts on its event, spawns a wave at once, the next only when the last one is dead.
        EncounterDirector director(decode_encounter(waves.dump()),{},points,7);WorldEvents world;std::uint64_t tick=0;auto step=[&](unsigned alive){world.begin_tick(++tick);return director.step(tick,world,{},alive,no_ground,empty);};
        check(step(0).empty()&&director.status().phase==EncounterPhase::Waiting,"An encounter started without its trigger");
        world.emit("gate.opened");auto first=step(0);check(first.size()==2&&first[0].npc==guard&&first[0].index==0&&first[1].index==1&&first[0].position.x==4&&first[0].yaw==1.&&first[1].position.x==-4&&director.status().phase==EncounterPhase::Running&&director.status().wave==1&&director.status().waves==2&&world.tag("gate.fight_active"),"The first wave did not spawn at the encounter's spawn points when its event was delivered");
        check(step(2).empty()&&world.happened("gate.fight_started"),"The encounter did not emit its started event, or spawned again while its wave was alive");
        check(step(1).empty()&&step(1).empty(),"The next wave came while an enemy of the current one was alive");
        auto second=step(0);check(second.size()==3&&second[0].npc==archer&&second[0].index==2&&second[2].index==4,"The second wave did not come when the first was dead");
        // Three enemies over two points: the third steps aside from the first point.
        check(std::hypot(second[2].position.x-second[0].position.x,second[2].position.z-second[0].position.z)>.5,"Two enemies of one wave were given the same place");
        check(step(3).empty()&&world.happened("gate.wave_cleared")&&!world.happened("gate.cleared"),"The encounter did not emit its wave event");
        check(step(0).empty()&&director.status().phase==EncounterPhase::Complete&&!world.tag("gate.fight_active"),"The encounter did not complete when its last wave was dead, or kept its tag");
        check(step(0).empty()&&world.happened("gate.cleared"),"The encounter did not emit its completed event");
        world.emit("gate.opened");step(0);check(step(0).empty()&&director.status().phase==EncounterPhase::Complete&&director.status().spawned==5,"A completed encounter started again");
    }
    {
        // Timed: one enemy every interval while fewer than the cap are alive, until the total.
        Json timed={{"trigger",{{"event","play.started"}}},{"mode","timed"},{"timed",{{"npc",guard.text()},{"interval_seconds",0.5},{"max_alive",2},{"total",4}}},{"placement","points"}};
        EncounterDirector director(decode_encounter(timed.dump()),{},points,7);WorldEvents world;world.emit("play.started");std::uint64_t tick=0;unsigned alive=0,spawned=0;std::vector<std::uint64_t> at;
        auto run=[&](unsigned ticks){for(unsigned index=0;index<ticks;++index){world.begin_tick(++tick);const auto spawns=director.step(tick,world,{},alive,no_ground,empty);check(spawns.size()<=1,"A timed encounter spawned more than one enemy on a tick");if(!spawns.empty()){check(spawns[0].index==spawned,"Timed spawn index");++spawned;++alive;at.push_back(tick);}}};
        run(100);check(spawned==2&&at[0]==1&&at[1]==31&&alive==2,"A timed encounter ignored its interval or its cap on enemies alive");
        alive=1;run(1);check(spawned==3&&at[2]==101,"A timed encounter did not spawn when an enemy died and the interval had passed");
        alive=0;run(29);check(spawned==3,"A timed encounter spawned before its interval");run(1);check(spawned==4&&director.status().phase==EncounterPhase::Running,"A timed encounter did not reach its total");
        alive=1;run(60);check(spawned==4,"A timed encounter spawned beyond its total");alive=0;run(1);check(director.status().phase==EncounterPhase::Complete,"A timed encounter did not complete when all its enemies were dead");
    }
    {
        // Entering the bounds starts it; random placement lands inside the union of volumes, on the ground, apart from each other.
        Json entered={{"trigger",{{"enter",true}}},{"mode","waves"},{"waves",Json::array({Json{{"npc",guard.text()},{"count",8}}})},{"placement","random"}};
        const std::vector<EncounterVolume> volumes{{{10,1,0},0,{3,2,3}},{{16,1,6},.7853981633974483,{2,2,5}}};
        check(encounter_contains(volumes,{12,1,2})&&!encounter_contains(volumes,{14,1,0})&&!encounter_contains(volumes,{10,4,0})&&encounter_contains(volumes,{18,1,8})&&!encounter_contains(volumes,{19,1,3}),"Encounter bounds are not the union of its turned boxes");
        const auto flat=[](double,double,double){return std::optional<double>{.5};};
        auto spawn=[&](std::uint64_t seed){EncounterDirector director(decode_encounter(entered.dump()),volumes,{},seed);WorldEvents world;world.begin_tick(1);check(director.step(1,world,{0,1,0},0,flat,empty).empty(),"An enter encounter started with the player outside its bounds");world.begin_tick(2);return director.step(2,world,{11,1,1},0,flat,empty);};
        const auto first=spawn(99),again=spawn(99),other=spawn(100);check(first.size()==8,"An encounter did not start when the player entered its bounds");
        bool same=true,different=false;for(unsigned index=0;index<8;++index){const auto& at=first[index].position;check(encounter_contains(volumes,{at.x,1,at.z})&&at.y==.5,"A random spawn point is outside the encounter's volumes or off the ground");for(unsigned before=0;before<index;++before)check(std::hypot(first[before].position.x-at.x,first[before].position.z-at.z)>=1,"Two random spawn points are on top of each other");same&=at.x==again[index].position.x&&at.z==again[index].position.z;different|=at.x!=other[index].position.x;}
        check(same&&different,"Random spawn points are not repeatable for one seed, or do not depend on it");
        check(rejects([&]{EncounterDirector director(decode_encounter(entered.dump()),volumes,{},1);WorldEvents world;world.begin_tick(1);director.step(1,world,{11,1,1},0,no_ground,empty);}),"An encounter with no ground under its volumes spawned somewhere");
        check(rejects([&]{EncounterDirector director(decode_encounter(entered.dump()),{},points,1);})&&rejects([&]{EncounterDirector director(decode_encounter(waves.dump()),volumes,{},1);}),"An encounter without the volumes or spawn points it needs was accepted");
    }
    {
        // A trigger volume emits its world event when the player walks in: once, or on every entry. The event starts an encounter a tick later.
        const auto once=decode_trigger(R"({"event":"gate.opened","half":[2,2,2]})"),repeat=decode_trigger(R"({"event":"bell.rang","half":[2,2,2],"once":false})");check(once.once&&!repeat.once&&once.half.x==2,"A trigger volume did not decode");
        check(rejects([&]{decode_trigger(R"({"event":"Gate","half":[2,2,2]})");})&&rejects([&]{decode_trigger(R"({"event":"gate.opened","half":[2,0,2]})");})&&rejects([&]{decode_trigger(R"({"event":"gate.opened","half":[2,2,2],"extra":1})");}),"An invalid trigger volume was decoded");
        WorldTrigger gate(once,{10,1,0},0),bell(repeat,{10,1,0},0);WorldEvents world;EncounterDirector director(decode_encounter(waves.dump()),{},points,7);std::uint64_t tick=0;
        auto step=[&](MotorVec player,bool alive=true){world.begin_tick(++tick);gate.step(world,player,alive);bell.step(world,player,alive);return director.step(tick,world,player,0,no_ground,empty);};
        check(step({0,1,0}).empty()&&!gate.fired(),"A trigger volume fired with the player outside it");check(step({10,1,0},false).empty()&&!gate.fired(),"A trigger volume fired for a dead player");
        check(step({10,1,0}).empty()&&gate.fired()==1&&bell.fired()==1&&gate.inside(),"A trigger volume did not fire when the player walked in");
        check(step({10,1,1}).size()==2&&world.happened("gate.opened")&&world.happened("bell.rang"),"A trigger volume's event did not start the encounter on the next tick");
        step({10,1,1});check(gate.fired()==1&&bell.fired()==1,"A trigger volume fired again while the player stayed inside");
        step({0,1,0});step({10,1,0});check(gate.fired()==1&&bell.fired()==2,"A once trigger fired twice, or a repeating trigger did not fire on the second entry");
    }
    std::cout<<"Encounters: trigger volumes (entry, once and repeating, dead player), world events (next-tick delivery, order, bounds, tags), decoding and rejection, waves at spawn points (trigger, next wave on death, emitted events, tag, completion), timed spawning (interval, cap, total), enter trigger, random points inside turned volumes (ground, spacing, repeatable) passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
