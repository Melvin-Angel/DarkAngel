#include <darkangel/character_scene.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
double apart(MotorVec a,MotorVec b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}}
// Two players in one scene session: one authority, two clients on their own loopback pairs, in one process and in
// lockstep. This shows the gameplay rules for a party. It says nothing about separate processes, delay or loss.
int main(){try{
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("party-scene-"+AssetId::random().text());std::ifstream source(content/"royal_district/combat/player.dakit");auto data=nlohmann::json::parse(source);auto id=AssetId::parse(data.at("asset").get<std::string>());AssetService assets(content,cache);assets.cook("royal_district/combat/player.dakit");assets.package(id,cache/"registry.json");auto cooked=load_cooked_combat_kit(cache/"registry.json",assets.cas_path(),id);
    CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    const StableId one{9,1},two{9,3};ObjectData player;player.id=one;CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}}};SessionHandshake hello{3,sha256("party scene wire3"),sha256(cooked.definition->generation),801};
    const auto& plan=cooked.stance.plan;const auto& rig=cooked.stance.rig;
    auto build=[&](std::span<const ObjectData> objects,const CharacterSceneCombat& scene,ScenePlayer companion){const std::array<ScenePlayer,1> companions{companion};return std::make_unique<CharacterSceneSession>(hello,objects,one,collision,plan,rig.definition,rig.archive,scene,companions);};
    auto health=[](const SceneClient& view,StableId who){return view.presentation().read(view.presentation().find(who)).health.current;};
    const CharacterSceneInput pressed{0,0,0,false,false,{{8,InputEdge::Pressed,1000,0,1,false}}},released{0,0,0,false,false,{{8,InputEdge::Released,2000,0,0,false},{8,InputEdge::Tapped,2000,0,1,false}}};
    // Two players move. Each is predicted by its own client, corrected by authority, and seen by the other's client.
    {const std::array<ObjectData,1> objects{player};auto session=build(objects,combat,{two,{3,0,0},0});
        check(session->players().size()==2&&session->players()[0]==one&&session->players()[1]==two&&session->client(one)&&session->client(two)&&!session->client({9,99})&&session->authority().party().size()==2,"The session does not hold two players, each with its own client");
        const auto& first=*session->client(one);const auto& second=*session->client(two);const auto rebuilt_one=first.resynchronizations(),rebuilt_two=second.resynchronizations();
        check(first.presentation().valid(first.presentation().find(two))&&second.presentation().valid(second.presentation().find(one))&&apart(session->authority().motor(two)->position,{3,0,0})<.05,"The second player's character was not created at its place and replicated to both clients");
        CharacterSceneInput forward;forward.z=1;CharacterSceneInput right;right.x=1;right.yaw=1.5707963267948966;session->input(two,right);for(unsigned tick=0;tick<90;++tick)session->step(forward);
        const auto host_one=*session->authority().motor(one),host_two=*session->authority().motor(two);
        check(host_one.position.z>2&&std::abs(host_one.position.x)<.05&&host_two.position.x>5&&std::abs(host_two.position.z)<.05,"The two players did not each move by their own input");
        check(apart(first.predicted_motor().position,host_one.position)<1e-6&&apart(second.predicted_motor().position,host_two.position)<1e-6&&first.pending_prediction()==0&&second.pending_prediction()==0&&first.resynchronizations()==rebuilt_one&&second.resynchronizations()==rebuilt_two,"A player's prediction was not reconciled with authority, or its prediction world was rebuilt");
        const auto seen_two=first.observer(two,double(host_two.tick)).frame.motor,seen_one=second.observer(one,double(host_one.tick)).frame.motor;
        check(apart(seen_two.position,host_two.position)<1e-6&&apart(seen_one.position,host_one.position)<1e-6&&first.pose(two)&&second.pose(one)&&first.pose(one)&&second.pose(two)&&first.identity(second.network())==two&&second.identity(first.network())==one,"A client does not present the other player from its public state");
        check(session->motor().tick==90&&session->predicted_motor().tick==90&&session->pose(two)==first.pose(two),"The accessors that name no player are not the first player's");
        check(rejects([&]{session->input(one,{});})&&rejects([&]{session->input({9,99},{});}),"Input for the first player or an unknown player was accepted as a companion's");}
    // Interaction and trigger volumes for any member. Player 2 pulls a lever both stand near; authority answers player 2,
    // and player 1's client learns the lever is used up. Then player 2 walks into a trigger volume and into an encounter's bounds.
    {auto scene=combat;const StableId lever{9,70};scene.interactables={{lever,decode_interactable(R"({"prompt":"Pull the lever","event":"lever.pulled"})"),{1.5,0,0}}};
        CharacterSceneTrigger gate;gate.id={9,60};gate.definition=decode_trigger(R"({"event":"gate.entered","half":[1,2,3]})");gate.center={7,1,0};scene.triggers={gate};
        const AssetId raider=AssetId::random();CharacterSceneEncounter encounter;encounter.id={9,50};encounter.definition=decode_encounter(R"({"trigger":{"enter":true},"mode":"waves","waves":[{"npc":")"+raider.text()+R"(","count":1}],"placement":"points"})");encounter.volumes={{{11,1,0},0,{1,2,3}}};encounter.points={{{11,0,6},3.141592653589793}};
        {CharacterSceneActor made;made.attributes=combat.attributes;made.name="Raider";made.faction="raiders";made.senses.everything=true;encounter.npcs.emplace(raider,made);}scene.encounters={encounter};
        const std::array<ObjectData,1> objects{player};auto session=build(objects,scene,{two,{3,0,0},0});const auto& first=*session->client(one);const auto& second=*session->client(two);
        for(unsigned tick=0;tick<10;++tick)session->step({});
        check(first.interaction_prompt()&&first.interaction_prompt()->id==lever&&second.interaction_prompt()&&second.interaction_prompt()->id==lever,"The lever was not offered to both players standing in its reach");
        CharacterSceneInput use;use.interact=lever;session->input(two,use);session->step({});session->step({});session->step({});
        check(second.last_interaction()&&second.last_interaction()->object==lever&&second.last_interaction()->result==InteractionResult::Accepted&&session->interactable_used(lever)==1u,"Player 2's request was not resolved by authority for player 2");
        check(!first.last_interaction()&&!first.interaction_prompt()&&!second.interaction_prompt(),"Player 1's client did not learn that the lever is used up, or was answered for a request it never made");
        session->step(use);session->step({});session->step({});check(first.last_interaction()&&first.last_interaction()->result==InteractionResult::Unavailable&&session->interactable_used(lever)==1u,"A lever player 2 used up was used again by player 1");
        std::string heard;for(const auto& event:session->world_events().log())heard+=event.name+" ";check(heard=="play.started lever.pulled ","The lever's event was not emitted exactly once");
        CharacterSceneInput right;right.x=1;right.yaw=1.5707963267948966;session->input(two,right);
        while(session->motor().tick<400&&session->trigger_fired({9,60})==0u)session->step({});check(session->trigger_fired({9,60})==1u&&session->authority().motor(two)->position.x<7&&std::abs(session->motor().position.x)<.05,"A trigger volume did not fire for player 2 walking in while player 1 stood still");
        check(session->encounter({9,50})->phase==EncounterPhase::Waiting,"An encounter started before anyone entered its bounds");
        while(session->motor().tick<800&&session->actors().empty())session->step({});check(session->encounter({9,50})->phase==EncounterPhase::Running&&session->actors().size()==1&&session->authority().motor(two)->position.x<11,"An encounter's enter trigger did not fire for player 2");
        session->step({});check(first.pose(session->actors()[0])&&second.pose(session->actors()[0])&&first.pending_prediction()==0&&second.pending_prediction()==0,"An enemy that entered is not presented by both clients");}
    // An NPC senses and attacks the nearer player; players cannot hurt each other; player 2's own attack hits a hostile.
    {ObjectData lead=player;lead.transform.x=3;lead.transform.z=-1.2;ObjectData guard;guard.id={9,2};guard.transform.x=3;guard.transform.z=1.2;guard.transform.yaw=3.141592653589793;const std::array<ObjectData,2> objects{lead,guard};
        auto scene=combat;{CharacterSceneActor made;made.id=guard.id;made.kit=scene.kit;made.input=scene.input;made.abilities=scene.abilities;made.attributes=scene.attributes;made.name="Guard";made.faction="royal_guard";made.controller=make_scripted_attacker({CombatSlot::Light,10,600,3,true});made.senses.everything=true;scene.actors={made};}
        auto session=build(objects,scene,{two,{3,0,0},0});const auto& first=*session->client(one);const auto& second=*session->client(two);
        while(session->motor().tick<10)session->step({});const bool aimed=session->controller_target(guard.id)==two;while(session->motor().tick<60)session->step({});
        check(aimed&&session->controller_receipt(guard.id)&&session->controller_receipt(guard.id)->failure==AbilityFailure::None&&health(first,two)==75&&health(second,two)==75&&health(first,one)==100,"The guard did not sense and attack the player nearest to it, or both clients do not show the hit");
        check(session->perception(guard.id)&&std::count_if(session->perception(guard.id)->actors().begin(),session->perception(guard.id)->actors().end(),[&](const PerceivedActor& seen){return seen.detected&&(seen.id==one||seen.id==two);})==2,"The guard does not perceive both players");
        // Player 1 swings at player 2's back: same party, no damage. Player 2 swings at the guard in front of it: damage.
        session->step(pressed);session->step(released);while(session->motor().tick<120)session->step({});
        check(health(first,two)==75&&first.ability()&&!first.ability()->active,"A player's attack damaged another player");
        const auto guard_before=health(second,guard.id);session->input(two,pressed);session->step({});session->input(two,released);session->step({});while(session->motor().tick<180)session->step({});
        check(guard_before>=50&&health(second,guard.id)==guard_before-25&&health(first,guard.id)==guard_before-25&&second.pending_prediction()==0&&second.pending_abilities()==0,"Player 2's own attack did not hit the hostile in front of it");
        double stamina=-1;for(const auto& attribute:second.ability()->attributes)if(attribute.id==3)stamina=attribute.value;double untouched=-1;for(const auto& attribute:first.ability()->attributes)if(attribute.id==3)untouched=attribute.value;
        check(stamina<100&&untouched<100&&stamina>0,"Each player's attack was not paid from its own attributes");}
    // Capacity: every player takes one of the session's character places.
    {std::vector<ObjectData> crowd{player};auto scene=combat;for(unsigned index=0;index<32;++index){ObjectData dummy;dummy.id={9,100+index};dummy.transform.x=-14+double(index%8)*2;dummy.transform.z=6+double(index/8)*2;crowd.push_back(dummy);CharacterSceneActor made;made.id=dummy.id;made.attributes=combat.attributes;scene.actors.push_back(made);}
        check(rejects([&]{build(crowd,scene,{two,{3,0,0},0});}),"Thirty-four characters were accepted into one session");
        crowd.pop_back();scene.actors.pop_back();auto session=build(crowd,scene,{two,{3,0,0},0});session->step({});check(session->actors().size()==31&&session->client(two)->pose(crowd[1].id),"Two players and thirty-one actors did not fit one session");
        const std::array<ObjectData,1> alone{player};check(rejects([&]{build(alone,combat,{one,{3,0,0},0});})&&rejects([&]{const std::array<ScenePlayer,4> many{ScenePlayer{{9,3},{3,0,0},0},ScenePlayer{{9,4},{5,0,0},0},ScenePlayer{{9,5},{7,0,0},0},ScenePlayer{{9,6},{9,0,0},0}};CharacterSceneSession(hello,alone,one,collision,plan,rig.definition,rig.archive,combat,many);}),"A duplicate party identity or a fifth player was accepted");}
    {std::error_code ignored;std::filesystem::remove_all(cache,ignored);}
    std::cout<<"Party scene (one process, loopback, lockstep): two players each predicted and corrected and seen by the other's client, interaction resolved for the requesting player and its result seen by both, trigger volume and encounter entry for any member, an NPC sensing and attacking the nearer player, no damage between players, a player's own attack and costs, capacity 33 characters and 4 players\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
