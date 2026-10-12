#include <darkangel/behavior_tree.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/actor_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace darkangel;
using Json=nlohmann::json;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F call){try{call();}catch(const std::exception&){return true;}return false;}}
int main(){try{
    // Topology decoding: a closed native node set with bounded shape and parameters.
    Json source={{"schema",1},{"kind","behavior"},{"asset",AssetId::random().text()},{"name","Test Tree"},{"tree",Json::parse(default_behavior_tree_json())}};
    auto definition=decode_behavior(source.dump());check(definition.name=="Test Tree"&&definition.root.type=="selector"&&definition.root.children.size()==2&&definition.root.children[0].children.size()==6&&definition.root.children[0].children[4].slot==CombatSlot::Light&&definition.root.children[0].children[5].ticks==90&&definition.root.children[1].children[1].range==1.4,"Default behaviour tree did not decode");
    for(auto change:{+[](Json& tree){tree["children"][0]["type"]="teleport";},+[](Json& tree){tree["children"]=Json::array();},+[](Json& tree){tree["children"][1]["children"][1]["range"]=0.0;},+[](Json& tree){tree["children"][0]["children"][4]["slot"]="dodge";},+[](Json& tree){tree["children"][0]["children"][5]["ticks"]=0u;},+[](Json& tree){tree["children"][0]["children"][0]["extra"]=1;},+[](Json& tree){Json deep=tree;for(int i=0;i<9;++i)deep=Json{{"type","sequence"},{"children",Json::array({deep})}};tree=deep;}}){auto bad=source;change(bad["tree"]);check(rejects([&]{decode_behavior(bad.dump());}),"An invalid behaviour tree was decoded");}

    // The executor behind the controller seam, without a navigation mesh: approach, face and press, then recover.
    // The view lists one hostile actor at the origin: faction 1 against the controlled actor's faction 2.
    std::array<PerceivedActor,1> foes{PerceivedActor{StableId{9,1},1,MotorState{},true,true,true}};std::array<PerceivedActor,1> fallen=foes;fallen[0].alive=false;
    auto controller=make_behavior_controller(definition);ControllerView view;view.alive=true;view.faction=2;view.perceived=foes;view.self.position={0,0,6};
    view.tick=1;auto command=controller->decide(view);check(!command.press&&command.target==foes[0].id&&std::abs(command.move_z+1)<1e-9&&std::abs(command.move_x)<1e-9&&command.face&&std::abs(std::abs(*command.face)-3.141592653589793)<1e-9,"Approach did not ask to walk toward the opponent");check(controller->status()=="Approach","Decision readout does not name the approach");
    view.self.position={0,0,1.2};view.tick=2;command=controller->decide(view);check(!command.press&&command.move_x==0&&command.move_z==0,"Approach kept moving once in range");
    view.tick=3;command=controller->decide(view);check(command.press&&*command.press==CombatSlot::Light&&command.face&&command.move_x==0&&command.move_z==0,"In reach and idle did not face and press the attack slot");check(controller->status()=="Attack","Decision readout does not name the attack");
    unsigned presses{};for(view.tick=4;view.tick<92;++view.tick){view.busy=view.tick<40;if(controller->decide(view).press)++presses;}check(presses==0&&controller->status().starts_with("Recover"),"The tree attacked again while recovering, or the readout does not show recovery");
    view.busy=false;bool again=false;for(view.tick=92;view.tick<100&&!again;++view.tick)again=controller->decide(view).press.has_value();check(again,"The tree did not attack again after recovering");
    {auto fresh=make_behavior_controller(definition);ControllerView idle=view;idle.tick=1;idle.busy=true;check(!fresh->decide(idle).press,"The tree pressed while its own action was running");idle.busy=false;idle.perceived=fallen;auto none=fresh->decide(idle);check(!none.press&&!none.face&&!none.target&&none.move_x==0&&none.move_z==0,"The tree acted against a dead opponent");}
    // Perception: an opponent the NPC remembers but no longer detects is not an opponent, and opponent_visible needs line of sight.
    {auto lost=foes;lost[0].detected=false;lost[0].visible=false;auto fresh=make_behavior_controller(definition);ControllerView blind=view;blind.tick=1;blind.busy=false;blind.self.position={0,0,1.2};blind.perceived=lost;auto none=fresh->decide(blind);check(!none.press&&!none.target&&none.move_x==0&&none.move_z==0,"The tree acted against an opponent it no longer detects");
        auto sighted=source;sighted["tree"]=Json::parse(R"({"type":"sequence","children":[{"type":"opponent_visible"},{"type":"attack","slot":"light"}]})");auto tree=decode_behavior(sighted.dump());blind.perceived=foes;check(make_behavior_controller(tree)->decide(blind).press.has_value(),"opponent_visible failed with a clear line of sight");auto hidden=foes;hidden[0].visible=false;blind.perceived=hidden;check(!make_behavior_controller(tree)->decide(blind).press,"opponent_visible passed without line of sight");
        // Approach keeps walking while the opponent is in range but out of sight.
        auto chase=source;chase["tree"]=Json::parse(R"({"type":"sequence","children":[{"type":"approach","range":1.4}]})");blind.self.position={0,0,1.2};auto walking=make_behavior_controller(decode_behavior(chase.dump()))->decide(blind);check(!walking.press&&std::abs(walking.move_z+1)<1e-9,"Approach stopped in range of an opponent it cannot see");}
    // Go and look: investigate walks to the last known position of a lost hostile, stops there, and fails when none is remembered.
    {auto search=source;search["tree"]=Json::parse(R"({"type":"sequence","children":[{"type":"investigate"}]})");auto tree=decode_behavior(search.dump());auto lost=foes;lost[0].detected=false;lost[0].visible=false;lost[0].seen=5;
        ControllerView looking=view;looking.tick=10;looking.busy=false;looking.self.position={0,0,6};looking.perceived=lost;auto seeker=make_behavior_controller(tree);auto walk=seeker->decide(looking);check(!walk.press&&!walk.target&&std::abs(walk.move_z+1)<1e-9&&seeker->status().starts_with("Investigate"),"investigate did not walk toward the last known position");
        looking.self.position={0,0,.8};looking.tick=11;auto arrived=seeker->decide(looking);check(arrived.move_x==0&&arrived.move_z==0,"investigate kept walking at the last known position");
        auto never=foes;never[0].detected=false;never[0].seen=0;looking.self.position={0,0,6};looking.perceived=never;auto idle=make_behavior_controller(tree)->decide(looking);check(idle.move_x==0&&idle.move_z==0,"investigate walked toward an actor the NPC never detected");}
    check(rejects([&]{make_behavior_controller(BehaviorDefinition{});}),"An undecoded behaviour was given a controller");
    {
        // NPC tuning: nodes set to "preferred" read the NPC's range, and recover time scales, without touching the tree.
        auto shared=source;shared["tree"]["children"][0]["children"][1]["range"]="preferred";shared["tree"]["children"][1]["children"][1]["range"]="preferred";auto tree=decode_behavior(shared.dump());check(tree.root.children[0].children[1].preferred&&tree.root.children[1].children[1].preferred,"Preferred range did not decode");
        {auto bad=shared;bad["tree"]["children"][0]["children"][1]["range"]="far";check(rejects([&]{decode_behavior(bad.dump());}),"An unknown range keyword was decoded");}
        ControllerView at;at.alive=true;at.faction=2;at.perceived=foes;at.self.position={0,0,2.5};at.tick=1;
        auto close=make_behavior_controller(tree),reach=make_behavior_controller(tree,{},BehaviorTuning{3.0,1.0});
        const auto near=close->decide(at),far=reach->decide(at);
        check(near.move_z<-.9&&!near.press&&far.move_z==0&&far.press.has_value(),"The NPC's preferred range did not decide where the approach stops and the attack starts");
        auto eager=make_behavior_controller(tree,{},BehaviorTuning{3.0,.5});check(eager->decide(at).press.has_value(),"Tuned NPC did not attack");
        std::uint64_t next{};for(at.tick=2;at.tick<200&&!next;++at.tick)if(eager->decide(at).press)next=at.tick;check(next>=45&&next<=49,"Recovery scale 0.5 did not halve the 90 tick recovery");
        check(rejects([&]{make_behavior_controller(tree,{},BehaviorTuning{0,1});})&&rejects([&]{make_behavior_controller(tree,{},BehaviorTuning{1.4,9});}),"Out-of-bounds NPC tuning was accepted");
    }

    // Script leaves: a behaviour node written in Luau inside the tree, deciding through the same seam, sandboxed, and never able to fail the tree's owner.
    const std::string keep_distance=R"lua(return function(view, state)
  state.calls = (state.calls or 0) + 1
  local foe = view.opponent
  if not foe then return "failure" end
  if foe.distance < 4 then
    local dx, dz = view.self.x - foe.x, view.self.z - foe.z
    return "running", {move_x = dx / foe.distance, move_z = dz / foe.distance, face = math.atan2(-dx, -dz), say = "Backing off, call " .. state.calls}
  end
  return "success", {press = "light", say = "Far enough"}
end)lua";
    auto scripted=[&](const std::string& lua,const char* name="Keep distance"){Json made=source;made["tree"]={{"type","selector"},{"children",Json::array({Json{{"type","script"},{"name",name},{"source",lua}},Json{{"type","face"}}})}};return made;};
    {
        auto tree=decode_behavior(scripted(keep_distance).dump());check(tree.root.children[0].type=="script"&&tree.root.children[0].name=="Keep distance"&&tree.root.children[0].source==keep_distance,"A script leaf did not decode");
        check(rejects([&]{decode_behavior(scripted("return function(view state) end").dump());})&&rejects([&]{decode_behavior(scripted("--"+std::string(4096,'a')).dump());})&&rejects([&]{decode_behavior(scripted("").dump());})&&rejects([&]{decode_behavior(scripted(keep_distance,"").dump());})&&rejects([&]{auto extra=scripted(keep_distance);extra["tree"]["children"][0]["range"]=1.0;decode_behavior(extra.dump());}),"A script leaf that does not compile, is out of bounds or has an unknown field was decoded");
        check(rejects([&]{auto many=scripted(keep_distance);Json group={{"type","sequence"},{"children",Json::array()}};for(unsigned n=0;n<8;++n)group["children"].push_back(many["tree"]["children"][0]);many["tree"]["children"].push_back(group);decode_behavior(many.dump());}),"A tree with nine script leaves was decoded");
        // The script backs the NPC away from an opponent 2.5 m off, keeps its own state between ticks, and presses once it is far enough.
        auto runner=make_behavior_controller(tree,{},BehaviorTuning{3.0,1.0});ControllerView near;near.alive=true;near.faction=2;near.perceived=foes;near.self.position={0,0,2.5};near.tick=1;
        auto away=runner->decide(near);check(!away.press&&away.target==foes[0].id&&std::abs(away.move_z-1)<1e-9&&std::abs(away.move_x)<1e-9&&away.face&&std::abs(std::abs(*away.face)-3.141592653589793)<1e-9&&runner->status()=="Backing off, call 1","A script leaf did not decide move and face through the controller seam");
        near.tick=2;runner->decide(near);check(runner->status()=="Backing off, call 2","A script leaf's state did not persist between ticks");
        near.self.position={0,0,5};near.tick=3;auto press=runner->decide(near);check(press.press&&*press.press==CombatSlot::Light&&press.move_x==0&&press.move_z==0&&runner->status()=="Far enough","A script leaf did not press a slot by name");
        // Each NPC has its own sandbox and state.
        auto other=make_behavior_controller(tree);near.self.position={0,0,2.5};near.tick=4;other->decide(near);check(other->status()=="Backing off, call 1","Two controllers share script state");
        // The view: the tuning's preferred range, no clock library, and globals that cannot be written.
        auto told=[&](const std::string& lua){auto made=make_behavior_controller(decode_behavior(scripted(lua).dump()),{},BehaviorTuning{3.0,1.0});ControllerView at=near;at.tick=1;const auto command=made->decide(at);return std::pair{made->status(),command};};
        check(told(R"lua(return function(view) return "success", {say = "range " .. view.preferred_range .. (os == nil and " no os" or " os")} end)lua").first=="range 3 no os","A script leaf does not see the NPC's tuning, or can reach the clock library");
        // Faults: the leaf fails, the readout names the script and the reason, the tree's next branch still decides, and the fault repeats without harm.
        for(const auto& [lua,reason]:{std::pair<const char*,const char*>{R"lua(return function() error("boom") end)lua","boom"},{R"lua(return function() while true do end end)lua","time budget"},{R"lua(return function() return 42 end)lua","must return"},{R"lua(return function() return "maybe" end)lua","unknown status"},{R"lua(return function() return "running", {move_x = 2} end)lua","longer than 1"},{R"lua(return function() return "success", {press = "dodge"} end)lua","unknown slot"},{R"lua(return 5)lua","must return a function"},{R"lua(counter = 1 return function() return "success" end)lua","readonly"},{R"lua(return function(view) view.tick = 0 return "success" end)lua","readonly"},{R"lua(return function() local big = {} for n = 1, 10000000 do big[n] = {n} end return "success" end)lua",""}}){
            auto made=make_behavior_controller(decode_behavior(scripted(lua,"Faulty").dump()));ControllerView at=near;at.tick=1;const auto first=made->decide(at);const auto readout=made->status();at.tick=2;const auto second=made->decide(at);
            check(!first.press&&first.move_x==0&&first.move_z==0&&first.face.has_value()&&second.face.has_value()&&readout.find("Script \"Faulty\" is disabled")==0&&readout.find(reason)!=std::string::npos&&made->status()==readout,"A faulty script leaf did not fail alone with its reason in the readout");}
    }

    // Owned content: the Royal Guard references the Guard Melee behaviour; both are frozen with the NPC.
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("behavior-"+AssetId::random().text());AssetService assets(content,cache);
    std::ifstream npc_source(content/"npcs/royal_guard.danpc");auto npc_id=AssetId::parse(Json::parse(npc_source).at("asset").get<std::string>());assets.cook("npcs/royal_guard.danpc");assets.package(npc_id,cache/"registry.json");
    auto guard=load_cooked_npc(cache/"registry.json",assets.cas_path(),npc_id);check(guard.behavior&&guard.behavior->name=="Guard Melee"&&guard.definition.behavior==guard.behavior->id&&guard.behavior->generation.size()==64&&guard.behavior->root.children.size()==3&&guard.behavior->root.children[1].children[1].preferred&&guard.definition.tuning.preferred_range==1.4&&guard.definition.tuning.recovery_scale==1,"Owned Royal Guard lost its frozen behaviour tree or tuning");
    check(load_cooked_behavior(cache/"registry.json",assets.cas_path(),guard.behavior->id).generation==guard.behavior->generation&&rejects([&]{load_cooked_behavior(cache/"registry.json",assets.cas_path(),npc_id);}),"Behaviour products are not kind-checked");

    // In a session: the NPC's own kit and attributes, its authored tree as the controller, and the navigation mesh for the approach leaf.
    CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}},{101,{0,1,3},{4,1,.25}}};auto mesh=std::make_shared<const NavigationMesh>(collision);
    const auto& cooked=guard.kit;CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    combat.actors={CharacterSceneActor{StableId{9,2},cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,make_behavior_controller(*guard.behavior,mesh,guard.definition.tuning),SensesDefinition{true}}};
    ObjectData player;player.id={9,1};ObjectData target;target.id={9,2};target.transform.z=6;target.health={60,60};std::array<ObjectData,2> objects{player,target};SessionHandshake hello{3,sha256("behavior scene wire3"),sha256(cooked.definition->generation),705};
    CharacterSceneSession scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);const auto baseline=scene.resynchronizations();
    auto health=[&]{return scene.presentation().read(scene.presentation().find(player.id)).health.current;};
    double widest{};std::uint64_t first{},second{};while(scene.motor().tick<600&&!second){scene.step({});widest=std::max(widest,std::abs(scene.observer(target.id,double(scene.motor().tick)).frame.motor.position.x));if(!first&&health()<100)first=scene.motor().tick;else if(first&&health()<75)second=scene.motor().tick;}
    check(widest>4&&first&&second&&second-first>=90&&health()==50,"The behaviour tree did not walk the NPC around the wall, attack, recover and attack again through authority");
    check(!scene.controller_status(target.id).empty()&&scene.presentation().read(scene.presentation().find(target.id)).health.current==60&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"A tree-driven NPC disturbed its own state or owner prediction");
    std::cout<<"Behaviour tree: around the wall (widest x "<<widest<<" m), first hit tick "<<first<<", second hit tick "<<second<<"\n";
    {
        // In a session: an NPC whose tree is one script leaf backs away from the player to four metres and holds there, through authority.
        // The same script without its press: a Light swing would soft-lock the NPC back toward the player.
        std::string holding=keep_distance;{const std::string press="press = \"light\", ";const auto at=holding.find(press);check(at!=std::string::npos,"Test script lost its press");holding.erase(at,press.size());}
        Json keeper=source;keeper["tree"]={{"type","sequence"},{"children",Json::array({Json{{"type","opponent_alive"}},Json{{"type","script"},{"name","Keep distance"},{"source",holding}}})}};const auto tree=decode_behavior(keeper.dump());
        auto wary=combat;wary.actors={CharacterSceneActor{StableId{9,2},cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,make_behavior_controller(tree),SensesDefinition{true}}};
        ObjectData close=target;close.transform.z=-2;std::array<ObjectData,2> pair{player,close};CharacterSceneSession scene(hello,pair,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,wary);const auto baseline=scene.resynchronizations();
        while(scene.motor().tick<240)scene.step({});const auto at=scene.observer(target.id,double(scene.motor().tick)).frame.motor.position;const double apart=std::hypot(at.x-scene.motor().position.x,at.z-scene.motor().position.z);
        check(apart>3.9&&apart<4.6&&scene.controller_status(target.id)=="Far enough"&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"A script-driven NPC did not keep its distance through authority");
        std::cout<<"Script leaf: NPC backed off from 2 m to "<<apart<<" m, readout \""<<scene.controller_status(target.id)<<"\"\n";
    }
    {
        // Go and look, in a session with real senses: the player walks round the wall and out of sight. A guard whose tree ends in
        // "investigate" walks to where it last saw the player, sees it again from there and takes it as its target again. The same
        // guard without that branch gives up where it stands. The owned Guard Melee tree has the branch.
        check(guard.behavior->root.children.size()==3&&guard.behavior->root.children[2].type=="investigate","The owned Guard Melee tree does not end in go-and-look");
        auto hunt=[&](bool looks){
            auto tree=*guard.behavior;if(!looks)tree.root.children.pop_back();SensesDefinition eyes;eyes.sensors={{SensorKind::LineOfSight,40,0,0}};
            auto chase=combat;chase.actors={CharacterSceneActor{StableId{9,2},cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,make_behavior_controller(tree,mesh,guard.definition.tuning),eyes}};
            ObjectData distant=target;distant.transform.z=-14;std::array<ObjectData,2> pair{player,distant};CharacterSceneSession scene(hello,pair,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,chase);
            std::uint64_t lost{},looked{},found{};const auto detects=[&]{for(const auto& other:scene.perception(target.id)->actors())if(other.id==player.id)return other.detected;return false;};
            while(scene.motor().tick<900&&!found){
                // Round the wall's end at x = 4, then in behind it.
                const auto here=scene.motor().position;CharacterSceneInput walk;if(here.z<5&&here.x<5.2)walk.x=1;else if(here.z<5)walk.z=1;else if(here.x>.5)walk.x=-1;
                scene.step(walk);const auto now=scene.motor().tick;const bool seen=detects();
                if(!lost&&now>30&&!seen)lost=now;if(lost&&!looked&&scene.controller_status(target.id).starts_with("Investigate"))looked=now;if(lost&&seen&&now>lost+10)found=now;}
            const auto at=scene.observer(target.id,double(scene.motor().tick)).frame.motor.position;
            return std::tuple{lost,looked,found,std::hypot(at.x-scene.motor().position.x,at.z-scene.motor().position.z)};};
        const auto [lost,looked,found,near]=hunt(true);const auto [gave_up,never_looked,never_found,far]=hunt(false);
        check(lost&&looked>=lost&&found>looked,"A guard with go-and-look did not walk to the last known position and find the player again");
        check(gave_up&&!never_looked&&!never_found&&far>near,"A guard without go-and-look did not give up where it lost the player");
        std::cout<<"Go and look: lost at tick "<<lost<<", looking from tick "<<looked<<", found again at tick "<<found<<" ("<<near<<" m away); without it the guard stays "<<far<<" m away\n";
    }
    {
        // Profile of a full scene: twenty-four tree-driven guards at spawn points and an encounter that keeps eight more alive, all with the
        // Royal Guard's authored senses (rays against static collision) and the navigation mesh. Prints where a tick's time goes for three
        // settings of the time-slicing budgets. A measurement, not a pass/fail timing.
        auto run=[&](unsigned decisions,unsigned senses,unsigned spawns){
            auto navigation=std::make_shared<const NavigationMesh>(collision);auto full=combat;full.actors.clear();full.decision_budget=decisions;full.sense_budget=senses;full.spawn_budget=spawns;
            for(auto& attribute:full.attributes)if(attribute.id==1||attribute.id==2){attribute.base=100000;attribute.maximum=std::max(attribute.maximum,100000.);}
            ObjectData sturdy;sturdy.id={9,1};sturdy.health={100000,100000};std::vector<ObjectData> many{sturdy};
            for(unsigned index=0;index<24;++index){const StableId id{9,700+index};const double angle=6.283185307179586*index/24,radius=index%2?12.:7.;ObjectData placed;placed.id=id;placed.transform.x=std::sin(angle)*radius;placed.transform.z=std::cos(angle)*radius;placed.health={60,60};many.push_back(placed);
                full.actors.push_back(CharacterSceneActor{id,cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,make_behavior_controller(*guard.behavior,navigation,guard.definition.tuning),guard.definition.senses});}
            CharacterSceneEncounter encounter;encounter.id={9,50};std::string text=R"({"trigger":{"event":"play.started"},"mode":"timed","timed":{"npc":"NPC","interval_seconds":0.25,"max_alive":8,"total":64},"placement":"random","corpse_seconds":1})";text.replace(text.find("NPC"),3,guard.definition.id.text());encounter.definition=decode_encounter(text);
            CharacterSceneActor made{StableId{},cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,nullptr,guard.definition.senses};encounter.npcs.emplace(guard.definition.id,made);encounter.volumes={{{0,1,-9},0,{6,2,3}}};encounter.controller=[&,navigation](AssetId){return make_behavior_controller(*guard.behavior,navigation,guard.definition.tuning);};full.encounters={encounter};
            CharacterSceneSession scene(hello,many,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,full);const auto baseline=scene.resynchronizations();std::size_t most=0;const auto packets=scene.server_packets();
            while(scene.motor().tick<600){scene.step({});most=std::max(most,scene.actors().size());}
            const auto& spent=scene.profile();const auto walked=navigation->usage();const double ticks=double(spent.ticks);const double total=spent.world+spent.sensing+spent.decisions+spent.abilities+spent.motors+spent.hits+spent.replication+spent.prediction+spent.presentation;
            check(most>=30&&scene.resynchronizations()==baseline&&scene.pending_prediction()==0&&scene.presentation().read(scene.presentation().find(player.id)).health.current<100000,"The profiled scene did not fill up, fight, or keep owner prediction undisturbed");
            std::cout<<"Profile, budgets "<<decisions<<"/"<<senses<<"/"<<spawns<<" (decisions/senses/spawns a tick), at most "<<most<<" actors, "<<double(scene.server_packets()-packets)/ticks<<" packets a tick, microseconds a tick: total "<<total/ticks<<" | world "<<spent.world/ticks<<" sensing "<<spent.sensing/ticks<<" decisions "<<spent.decisions/ticks<<" (navigation "<<walked.microseconds/ticks<<" in "<<double(walked.queries)/ticks<<" queries) abilities "<<spent.abilities/ticks<<" motors "<<spent.motors/ticks<<" hits "<<spent.hits/ticks<<" replication "<<spent.replication/ticks<<" prediction "<<spent.prediction/ticks<<" presentation "<<spent.presentation/ticks<<"\n";
        };
        run(8,8,2);run(32,32,8);run(4,4,1);
    }
    std::cout<<"Behaviour trees: JSON topology decoding and rejection, BT.CPP executor behind the controller seam (approach, face, attack, recover), frozen behaviour on the owned Royal Guard, a tree-driven NPC navigating and attacking in a session, and Luau script leaves (decode, seam, state, sandbox, fault isolation, session) passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
