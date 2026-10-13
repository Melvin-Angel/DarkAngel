#include <darkangel/scene_transition.hpp>
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
double apart(MotorVec a,MotorVec b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
// A provider that builds small scenes in code and takes a few polls to do it.
class Scenes final:public SceneProvider {
public:
    std::map<AssetId,std::function<PreparedScene()>> scenes;unsigned polls{2},begun{},cancelled{};
    void begin(AssetId scene)override{wanted_=scene;left_=polls;++begun;}
    SceneLoadStatus poll()override{if(!scenes.contains(wanted_))return {SceneLoad::Failed,0,"No such scene in the project"};if(left_){--left_;return {SceneLoad::Working,1.-double(left_)/double(polls),"Loading the test scene"};}return {SceneLoad::Ready,1,"Loaded"};}
    std::unique_ptr<PreparedScene> take()override{return std::make_unique<PreparedScene>(scenes.at(wanted_)());}
    void cancel()override{++cancelled;}
private:AssetId wanted_;unsigned left_{};
};
}
// Scene transitions with two players, in one process over loopback. The ledger carries what is persistent; the party
// is rebuilt in the destination. Nothing here shows a client in another process loading and acknowledging.
int main(){try{
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("scene-transition-"+AssetId::random().text());std::ifstream source(content/"royal_district/combat/player.dakit");auto data=nlohmann::json::parse(source);auto id=AssetId::parse(data.at("asset").get<std::string>());AssetService assets(content,cache);assets.cook("royal_district/combat/player.dakit");assets.package(id,cache/"registry.json");auto cooked=load_cooked_combat_kit(cache/"registry.json",assets.cas_path(),id);
    CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    const AssetId scene_a=AssetId::random(),scene_b=AssetId::random(),crowded=AssetId::random(),missing=AssetId::random(),raider=AssetId::random();
    const StableId one_a{9,1},one_b{8,1},two{7,2},dummy{9,2},lever{9,70},fight{9,50},gate{9,80},back_door{9,90},arrival_b{8,20},portal_b{8,30};
    auto weak=[&]{auto attributes=combat.attributes;for(auto& attribute:attributes)if(attribute.id==1||attribute.id==2)attribute.base=25;return attributes;};
    auto base=[&](AssetId scene,StableId player){PreparedScene made;made.scene=scene;made.hello={3,sha256("scene transition wire3"),sha256(cooked.definition->generation),901};made.player=player;ObjectData object;object.id=player;made.objects={object};made.collision.id=AssetId::random();made.collision.schema=2;made.collision.boxes={{100,{0,-.5,0},{24,.5,24}}};made.plan=cooked.stance.plan;made.rig=cooked.stance.rig.definition;made.rig_archive=cooked.stance.rig.archive;made.combat=combat;return made;};
    Scenes provider;
    // Scene A: a weak dummy in front of the start, a single-use lever that starts a one-enemy encounter, a walk-in transition to scene B and an entry point to come back by.
    provider.scenes[scene_a]=[&]{auto made=base(scene_a,one_a);ObjectData target;target.id=dummy;target.transform.z=1.2;target.transform.yaw=3.141592653589793;target.health={25,25};made.objects.push_back(target);
        auto& scene=*made.combat;{CharacterSceneActor actor;actor.id=dummy;actor.attributes=weak();actor.name="Dummy";actor.faction="dummies";scene.actors={actor};}
        scene.interactables={{lever,decode_interactable(R"({"prompt":"Pull the lever","event":"lever.pulled"})"),{-1.5,0,0}}};
        CharacterSceneEncounter encounter;encounter.id=fight;encounter.definition=decode_encounter(R"({"trigger":{"event":"lever.pulled"},"mode":"waves","waves":[{"npc":")"+raider.text()+R"(","count":1}],"placement":"points","emits":{"completed":"fight.done"}})");encounter.points={{{1.2,0,0},-1.5707963267948966}};{CharacterSceneActor enemy;enemy.attributes=weak();enemy.name="Raider";enemy.faction="raiders";encounter.npcs.emplace(raider,enemy);}scene.encounters={encounter};
        CharacterSceneTransition door;door.id=gate;door.definition=decode_transition(R"({"scene":")"+scene_b.text()+R"(","entry":")"+arrival_b.text()+R"(","activation":"enter","half":[2,2,1]})");door.center={0,1,8};scene.transitions={door};
        made.entries={{back_door,"From the temple",{0,0,3},3.141592653589793}};return made;};
    // Scene B: an entry point away from its own start, and a way back that is used on purpose.
    provider.scenes[scene_b]=[&]{auto made=base(scene_b,one_b);auto& scene=*made.combat;
        CharacterSceneTransition door;door.id=portal_b;door.definition=decode_transition(R"({"scene":")"+scene_a.text()+R"(","entry":")"+back_door.text()+R"(","activation":"interact","prompt":"Return to the gate","radius":3.5,"half":[1,1,1]})");door.center={5,0,7};scene.transitions={door};
        made.entries={{arrival_b,"Temple door",{5,0,5},1.}};return made;};
    provider.scenes[crowded]=[&]{auto made=base(crowded,one_b);for(unsigned index=0;index<32;++index){ObjectData object;object.id={6,100+index};object.transform.x=-14+double(index%8)*2;object.transform.z=6+double(index/8)*2;made.objects.push_back(object);CharacterSceneActor actor;actor.id=object.id;actor.attributes=combat.attributes;made.combat->actors.push_back(actor);}return made;};
    WorldLedger ledger;SceneTransitionCoordinator travel(provider,ledger,{two});travel.minimum_seconds=0;unsigned unloaded=0,loaded=0;travel.unloading=[&]{check(travel.session()!=nullptr,"The owner was told of an unload after the session was gone");++unloaded;};travel.loaded=[&]{check(travel.session()!=nullptr,"The owner was told of a load before a session existed");++loaded;};
    CharacterSceneInput held;auto frame=[&]{if(auto* session=travel.session())session->step(travel.busy()?CharacterSceneInput{}:held);travel.update(1./60);};
    auto settle=[&]{for(unsigned guard=0;guard<600&&(travel.busy()||!travel.outcome());++guard)frame();check(!travel.busy()&&travel.outcome().has_value(),"A transition did not end");};
    auto where=[&](StableId player){return travel.session()->authority().motor(player)->position;};
    const CharacterSceneInput pressed{0,0,0,false,false,{{8,InputEdge::Pressed,1000,0,1,false}}},released{0,0,0,false,false,{{8,InputEdge::Released,2000,0,0,false},{8,InputEdge::Tapped,2000,0,1,false}}};
    auto swing=[&](double yaw){auto press=pressed;press.yaw=yaw;auto release=released;release.yaw=yaw;held.yaw=yaw;travel.session()->step(press);travel.session()->step(release);for(unsigned tick=0;tick<58;++tick)travel.session()->step(held);};
    // Play starts in scene A at its own start: the first player on the player object, the second behind it.
    travel.start(std::make_unique<PreparedScene>(provider.scenes.at(scene_a)()));
    check(travel.session()&&travel.scene()->scene==scene_a&&travel.session()->players().size()==2&&apart(where(one_a),{0,0,0})<.05&&apart(where(two),{0,0,-1.2})<.05,"Play did not start with the party in formation at the scene's own start");
    check(!travel.request({AssetId{},{},"nowhere"})&&!travel.busy(),"A transition to no scene was accepted");
    // Progress in scene A: the dummy is defeated, the lever pulled, the encounter it starts fought to the end.
    for(unsigned tick=0;tick<5;++tick)frame();swing(0);check(!travel.session()->presentation().read(travel.session()->presentation().find(dummy)).health.current,"The dummy was not defeated");
    {CharacterSceneInput use;use.interact=lever;travel.session()->step(use);for(unsigned tick=0;tick<4;++tick)frame();check(travel.session()->interactable_used(lever)==1u&&travel.session()->actors().size()==2,"The lever did not start the encounter");}
    swing(1.5707963267948966);for(unsigned tick=0;tick<10;++tick)frame();check(travel.session()->encounter(fight)->phase==EncounterPhase::Complete,"The encounter was not completed");held={};
    check(std::get<std::int64_t>(*ledger.value({scene_a,lever},"used"))==1&&std::get<bool>(*ledger.value({scene_a,fight},"completed"))&&!ledger.value({scene_a,fight},"started")&&std::get<bool>(*ledger.value({scene_a,dummy},"defeated"))&&ledger.records().size()==3,"Persistent facts were not written to the ledger in the tick they happened");
    // A transition to scene B, phase by phase. A second request while it runs is refused.
    const auto* before=travel.session();check(travel.request({scene_b,arrival_b,"test"})&&travel.busy()&&!travel.request({scene_a,{},"again"}),"A transition was not accepted, or one requested during another was not refused");
    travel.minimum_seconds=.3;unsigned waited=0;bool loading_text=false;for(unsigned guard=0;guard<600&&travel.busy();++guard){frame();if(travel.status().phase==TransitionPhase::AwaitingReady)++waited;if(travel.status().phase==TransitionPhase::Preparing){check(travel.session()==before,"The source scene did not keep running while the destination loaded");loading_text|=travel.status().text=="Loading the test scene"&&travel.status().progress>0;}}travel.minimum_seconds=0;
    const std::array expected{TransitionPhase::Requested,TransitionPhase::Preparing,TransitionPhase::Validating,TransitionPhase::Capturing,TransitionPhase::Swapping,TransitionPhase::Placing,TransitionPhase::AwaitingReady,TransitionPhase::Resumed};
    check(std::equal(travel.phases().begin(),travel.phases().end(),expected.begin(),expected.end())&&travel.outcome()&&travel.outcome()->arrived&&waited>=8&&loading_text&&unloaded==1&&loaded==1,"A transition did not go through its phases in order, or the loading screen's minimum time was not kept");
    const MotorVec ring{5+std::sin(1.+3.141592653589793)*1.2,0,5+std::cos(1.+3.141592653589793)*1.2};
    check(travel.scene()->scene==scene_b&&travel.session()->players()[0]==one_b&&travel.session()->players()[1]==two&&apart(where(one_b),{5,0,5})<.05&&apart(where(two),ring)<.05&&std::abs(travel.session()->authority().motor(two)->yaw-1.)<1e-9&&travel.session()->client(two)->pose(one_b)&&travel.session()->client(one_b)->pose(two),"Both players did not arrive in formation at the entry point, facing its way");
    // Failures before the swap leave the party in the scene it is in, still running.
    {const auto* running=travel.session();const auto tick=running->motor().tick;const auto ends=unloaded;
        check(travel.request({missing,{},"test"}),"A request was refused");settle();check(!travel.outcome()->arrived&&!travel.outcome()->rebuilt&&travel.outcome()->failure.find("No such scene")!=std::string::npos&&travel.phases().back()==TransitionPhase::Preparing&&travel.session()==running,"A missing scene did not cancel the transition before the swap");
        check(travel.request({scene_a,{9,99},"test"}),"A request was refused");settle();check(!travel.outcome()->arrived&&travel.outcome()->failure.find("no entry point")!=std::string::npos&&travel.phases().back()==TransitionPhase::Validating&&travel.session()==running,"A missing entry point did not cancel the transition before the swap");
        check(travel.request({crowded,{},"test"}),"A request was refused");settle();check(!travel.outcome()->arrived&&travel.outcome()->failure.find("capacity")!=std::string::npos&&travel.session()==running,"A scene the party does not fit was entered");
        frame();check(running->motor().tick>tick+3&&unloaded==ends&&travel.scene()->scene==scene_b&&!travel.fatal(),"The party's scene stopped or was unloaded by a cancelled transition");}
    // A failure during the swap, after the source is gone: the source scene is built again from the ledger, the party where it last came in.
    {travel.fail_next_swap();check(travel.request({scene_a,back_door,"test"}),"A request was refused");settle();
        check(!travel.outcome()->arrived&&travel.outcome()->rebuilt&&travel.phases().back()==TransitionPhase::Swapping&&travel.session()&&travel.scene()->scene==scene_b&&apart(where(one_b),{5,0,5})<.05&&apart(where(two),ring)<.05&&!travel.fatal()&&unloaded==loaded,"A failure during the swap did not rebuild the source scene with the party at its last entry");
        const auto tick=travel.session()->motor().tick;for(unsigned index=0;index<5;++index)frame();check(travel.session()->motor().tick==tick+5,"The rebuilt scene does not run");}
    // Back to scene A through scene B's own transition, used on purpose by the second player. What was done in A is restored.
    {check(travel.session()->client(two)->interaction_prompt()&&travel.session()->client(two)->interaction_prompt()->id==portal_b,"The way back was not offered to a player standing near it");
        CharacterSceneInput use;use.interact=portal_b;travel.session()->input(two,use);frame();frame();settle();
        check(travel.outcome()->arrived&&travel.scene()->scene==scene_a&&apart(where(one_a),{0,0,3})<.05&&apart(where(two),{0,0,4.2})<.05,"The second player's request did not move the whole party to scene A's entry point");
        for(unsigned tick=0;tick<4;++tick)frame();const auto& session=*travel.session();
        check(session.interactable_used(lever)==1u&&session.encounter(fight)->phase==EncounterPhase::Complete&&session.actors().empty()&&!session.presentation().valid(session.presentation().find(dummy)),"A used lever, a completed encounter or a defeated actor was not restored from the ledger");
        std::string heard;for(const auto& event:session.world_events().log())heard+=event.name+" ";check(heard=="play.started ","Restoring persistent state emitted its events again");
        // Neither client offers the spent lever, though neither pulled it in this session.
        held.x=-1;held.yaw=-1.5707963267948966;while(travel.session()->motor().position.x>-1.2||std::abs(travel.session()->motor().position.z)>1.9){held.z=travel.session()->motor().position.z>.2?-1:0;held.x=held.z?0:-1;frame();check(travel.session()->motor().tick<600,"The player did not reach the lever");}held={};frame();
        check(!travel.session()->client(one_a)->interaction_prompt(),"A lever used up before this session was offered again");}
    // A rest while the party is elsewhere resets what rests reset, in a scene that is not loaded.
    {held.z=1;held.yaw=0;for(unsigned guard=0;guard<900&&!travel.busy();++guard)frame();held={};settle();check(travel.outcome()->arrived&&travel.scene()->scene==scene_b&&travel.phases().size()==8,"Walking into the transition trigger did not take the party to scene B");
        const auto rested=ledger.reset(LedgerReset::Rest);check(rested.size()==2&&ledger.value({scene_a,lever},"used")&&!ledger.find({scene_a,fight})&&!ledger.find({scene_a,dummy}),"A rest did not reset the encounter and the defeated actor of the unloaded scene, or reset the lever");
        check(travel.request({scene_a,back_door,"test"}),"A request was refused");settle();for(unsigned tick=0;tick<4;++tick)frame();const auto& session=*travel.session();
        check(session.actors().size()==1&&session.actors()[0]==dummy&&session.encounter(fight)->phase==EncounterPhase::Waiting&&session.interactable_used(lever)==1u,"Scene A was not rebuilt from the ledger as the rest left it");}
    // Leaving a scene while its encounter runs: it is recorded as started and starts again on the way back.
    {const auto rests=ledger.reset(LedgerReset::Rest);check(rests.empty(),"A second rest changed the ledger");
        // The lever is made fresh again while its scene is not loaded: an unload would capture it as used.
        check(travel.request({scene_b,arrival_b,"test"}),"A request was refused");settle();check(ledger.erase({scene_a,lever}),"The lever had no record");check(travel.request({scene_a,back_door,"test"}),"A request was refused");settle();
        held.x=-1;held.yaw=-1.5707963267948966;while(travel.session()->motor().position.x>-1.2||std::abs(travel.session()->motor().position.z)>1.9){held.z=travel.session()->motor().position.z>.2?-1:0;held.x=held.z?0:-1;frame();check(travel.session()->motor().tick<600,"The player did not reach the lever");}held={};
        CharacterSceneInput use;use.interact=lever;travel.session()->step(use);for(unsigned tick=0;tick<4;++tick)frame();check(travel.session()->encounter(fight)->phase==EncounterPhase::Running&&std::get<bool>(*ledger.value({scene_a,fight},"started")),"A running encounter was not recorded as started");
        check(travel.request({scene_b,arrival_b,"test"}),"A request was refused");settle();check(travel.request({scene_a,back_door,"test"}),"A request was refused");settle();for(unsigned tick=0;tick<4;++tick)frame();
        check(travel.session()->encounter(fight)->phase==EncounterPhase::Running&&travel.session()->actors().size()==2&&travel.session()->interactable_used(lever)==1u,"An encounter that was running when its scene was left did not start again");}
    travel.stop();check(!travel.session()&&!travel.scene()&&unloaded==loaded+1,"Stopping Play did not unload the scene");
    // Network construction and readiness can each span frames, including a rebuild after the source is gone.
    {WorldLedger separate;SceneTransitionCoordinator async(provider,separate,{two});async.minimum_seconds=0;unsigned waiting=3,builds{},acknowledgements{},loads{},resumes{},cancels{};
        async.build_session=[&](PreparedScene& scene,StableId,const SceneStart& start,std::span<const ScenePlayer> companions)->std::unique_ptr<CharacterSceneSession>{++builds;if(waiting){--waiting;return {};}
            return std::make_unique<CharacterSceneSession>(scene.hello,scene.objects,scene.player,scene.collision,scene.plan,scene.rig,scene.rig_archive,scene.combat,companions,start);};
        async.construction_text=[] {return std::string("Waiting for the test party");};async.ready=[&](CharacterSceneSession& session){check(session.hosting()&&session.bootstrapped(),"Readiness received an unprepared host");return ++acknowledgements>=4;};
        async.loaded=[&]{check(async.session()!=nullptr,"Async load callback has no session");++loads;};async.resumed=[&]{++resumes;};async.cancelled=[&](std::string_view){++cancels;};
        auto tick=[&]{if(async.session())async.session()->step({});async.update(1./60);};auto end=[&]{for(unsigned n=0;n<600&&async.busy();++n)tick();check(!async.busy(),"Async transition did not finish");};
        async.start(std::make_unique<PreparedScene>(provider.scenes.at(scene_a)()));check(async.busy()&&!async.session()&&async.scene()->scene==scene_a,"Initial construction did not retain prepared scene metadata");
        for(unsigned n=0;n<10&&!async.session();++n)tick();check(async.session()&&loads==1&&builds==4&&resumes==0,"Initial construction blocked or bypassed readiness");end();check(resumes==1&&async.outcome()->arrived,"Initial party readiness did not resume");
        waiting=2;acknowledgements=0;check(async.request({scene_b,arrival_b,"async test"}),"Async travel was refused");end();check(async.scene()->scene==scene_b&&resumes==2&&loads==2,"Async destination did not wait and resume");
        waiting=2;acknowledgements=0;async.fail_next_swap();check(async.request({scene_a,back_door,"async rollback"}),"Async rollback request was refused");bool held{};
        for(unsigned n=0;n<600&&async.busy();++n){tick();held|=async.status().phase==TransitionPhase::Swapping&&async.session()&&acknowledgements>0&&acknowledgements<4;}
        check(!async.busy()&&held&&async.outcome()->rebuilt&&!async.outcome()->arrived&&async.scene()->scene==scene_b&&resumes==3&&loads==3&&async.phases().back()==TransitionPhase::Swapping,"Source rebuild resumed before the external party was ready");
        bool reject_ready=true;async.ready=[&](CharacterSceneSession&){if(reject_ready&&async.scene()->scene==scene_a){reject_ready=false;throw std::runtime_error("Injected readiness failure");}return true;};
        check(async.request({scene_a,back_door,"async readiness failure"}),"Readiness failure request was refused");end();
        check(async.outcome()->rebuilt&&!async.outcome()->arrived&&!async.fatal()&&async.scene()->scene==scene_b&&async.outcome()->failure.find("Injected readiness failure")!=std::string::npos&&resumes==4,"A readiness failure discarded the source scene or bypassed rollback");
        const auto* before=async.session();check(async.request({missing,{},"async cancel"}),"Async cancel request was refused");end();check(cancels==1&&async.session()==before&&!async.outcome()->rebuilt,"Pre-swap cancellation did not retain the source or notify control");async.stop();}
    {WorldLedger separate;SceneTransitionCoordinator pending(provider,separate);unsigned calls{};pending.build_session=[&](PreparedScene&,StableId,const SceneStart&,std::span<const ScenePlayer>)->std::unique_ptr<CharacterSceneSession>{++calls;return {};};pending.start(std::make_unique<PreparedScene>(provider.scenes.at(scene_a)()));pending.stop();pending.update(1./60);check(calls==1&&!pending.busy()&&!pending.session()&&!pending.scene(),"Stopped initial construction retained pending scene work");}
    // The authored components behind entry points and transition triggers.
    {const auto rejects=[](const std::string& json,bool entry){try{if(entry)decode_entry(json);else decode_transition(json);}catch(const std::exception&){return true;}return false;};const std::string to=scene_b.text();
        check(decode_entry(R"({"name":"Temple door"})").name=="Temple door"&&rejects(R"({"name":""})",true)&&rejects(R"({"name":"x","facing":1})",true)&&rejects(R"({})",true),"A scene entry point component was decoded wrongly or an invalid one accepted");
        const auto walk=decode_transition(R"({"scene":")"+to+R"(","activation":"enter","half":[2,1,0.5]})");const auto use=decode_transition(R"({"scene":")"+to+R"(","entry":")"+arrival_b.text()+R"(","activation":"interact","prompt":"Enter","radius":3,"half":[1,1,1]})");
        check(walk.scene==scene_b&&!walk.entry&&walk.activation==TransitionActivation::Enter&&walk.half.z==.5&&use.entry==arrival_b&&use.activation==TransitionActivation::Interact&&use.prompt=="Enter"&&use.radius==3,"A scene transition component was decoded wrongly");
        for(const auto& bad:{std::string(R"({"activation":"enter","half":[1,1,1]})"),R"({"scene":")"+to+R"(","activation":"fly","half":[1,1,1]})",R"({"scene":")"+to+R"(","activation":"interact","half":[1,1,1]})",R"({"scene":")"+to+R"(","activation":"enter","prompt":"Go","half":[1,1,1]})",R"({"scene":")"+to+R"(","activation":"enter","half":[1,0,1]})",R"({"scene":")"+to+R"(","activation":"interact","prompt":"Go","radius":0.1,"half":[1,1,1]})",R"({"scene":")"+to+R"(","entry":"","activation":"enter","half":[1,1,1]})",R"({"scene":")"+to+R"(","activation":"enter","half":[1,1,1],"party":"one"})"})check(rejects(bad,false),"An invalid scene transition component was accepted");}
    // The ledger that crossed every transition is what a save will store.
    {const std::array<LedgerKind,4> known{LedgerKind{"interactable",1},LedgerKind{"trigger",1},LedgerKind{"encounter",1},LedgerKind{"actor",1}};const auto saved=ledger.serialize();check(WorldLedger::deserialize(saved,known).serialize()==saved&&WorldLedger::deserialize(saved,known).foreign().empty(),"The session's ledger does not survive serialization");}
    {std::error_code ignored;std::filesystem::remove_all(cache,ignored);}
    std::cout<<"Scene transitions (one process, loopback, lockstep): phase order, loading-screen minimum, whole-party arrival in formation at an entry point, walk-in and used-on-purpose triggers from either player, a used lever, a completed encounter and a defeated actor restored from the ledger, a rest resetting an unloaded scene, a running encounter starting again, a missing scene, a missing entry and a full scene cancelled before the swap, a failed swap rebuilding the source, a request during another refused\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
