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
    std::array<ControllerActor,1> foes{ControllerActor{StableId{9,1},1,MotorState{},true}};std::array<ControllerActor,1> fallen=foes;fallen[0].alive=false;
    auto controller=make_behavior_controller(definition);ControllerView view;view.alive=true;view.faction=2;view.others=foes;view.self.position={0,0,6};
    view.tick=1;auto command=controller->decide(view);check(!command.press&&command.target==foes[0].id&&std::abs(command.move_z+1)<1e-9&&std::abs(command.move_x)<1e-9&&command.face&&std::abs(std::abs(*command.face)-3.141592653589793)<1e-9,"Approach did not ask to walk toward the opponent");check(controller->status()=="Approach","Decision readout does not name the approach");
    view.self.position={0,0,1.2};view.tick=2;command=controller->decide(view);check(!command.press&&command.move_x==0&&command.move_z==0,"Approach kept moving once in range");
    view.tick=3;command=controller->decide(view);check(command.press&&*command.press==CombatSlot::Light&&command.face&&command.move_x==0&&command.move_z==0,"In reach and idle did not face and press the attack slot");check(controller->status()=="Attack","Decision readout does not name the attack");
    unsigned presses{};for(view.tick=4;view.tick<92;++view.tick){view.busy=view.tick<40;if(controller->decide(view).press)++presses;}check(presses==0&&controller->status().starts_with("Recover"),"The tree attacked again while recovering, or the readout does not show recovery");
    view.busy=false;bool again=false;for(view.tick=92;view.tick<100&&!again;++view.tick)again=controller->decide(view).press.has_value();check(again,"The tree did not attack again after recovering");
    {auto fresh=make_behavior_controller(definition);ControllerView idle=view;idle.tick=1;idle.busy=true;check(!fresh->decide(idle).press,"The tree pressed while its own action was running");idle.busy=false;idle.others=fallen;auto none=fresh->decide(idle);check(!none.press&&!none.face&&!none.target&&none.move_x==0&&none.move_z==0,"The tree acted against a dead opponent");}
    check(rejects([&]{make_behavior_controller(BehaviorDefinition{});}),"An undecoded behaviour was given a controller");
    {
        // NPC tuning: nodes set to "preferred" read the NPC's range, and recover time scales, without touching the tree.
        auto shared=source;shared["tree"]["children"][0]["children"][1]["range"]="preferred";shared["tree"]["children"][1]["children"][1]["range"]="preferred";auto tree=decode_behavior(shared.dump());check(tree.root.children[0].children[1].preferred&&tree.root.children[1].children[1].preferred,"Preferred range did not decode");
        {auto bad=shared;bad["tree"]["children"][0]["children"][1]["range"]="far";check(rejects([&]{decode_behavior(bad.dump());}),"An unknown range keyword was decoded");}
        ControllerView at;at.alive=true;at.faction=2;at.others=foes;at.self.position={0,0,2.5};at.tick=1;
        auto close=make_behavior_controller(tree),reach=make_behavior_controller(tree,{},BehaviorTuning{3.0,1.0});
        const auto near=close->decide(at),far=reach->decide(at);
        check(near.move_z<-.9&&!near.press&&far.move_z==0&&far.press.has_value(),"The NPC's preferred range did not decide where the approach stops and the attack starts");
        auto eager=make_behavior_controller(tree,{},BehaviorTuning{3.0,.5});check(eager->decide(at).press.has_value(),"Tuned NPC did not attack");
        std::uint64_t next{};for(at.tick=2;at.tick<200&&!next;++at.tick)if(eager->decide(at).press)next=at.tick;check(next>=45&&next<=49,"Recovery scale 0.5 did not halve the 90 tick recovery");
        check(rejects([&]{make_behavior_controller(tree,{},BehaviorTuning{0,1});})&&rejects([&]{make_behavior_controller(tree,{},BehaviorTuning{1.4,9});}),"Out-of-bounds NPC tuning was accepted");
    }

    // Owned content: the Royal Guard references the Guard Melee behaviour; both are frozen with the NPC.
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("behavior-"+AssetId::random().text());AssetService assets(content,cache);
    std::ifstream npc_source(content/"npcs/royal_guard.danpc");auto npc_id=AssetId::parse(Json::parse(npc_source).at("asset").get<std::string>());assets.cook("npcs/royal_guard.danpc");assets.package(npc_id,cache/"registry.json");
    auto guard=load_cooked_npc(cache/"registry.json",assets.cas_path(),npc_id);check(guard.behavior&&guard.behavior->name=="Guard Melee"&&guard.definition.behavior==guard.behavior->id&&guard.behavior->generation.size()==64&&guard.behavior->root.children.size()==2&&guard.behavior->root.children[1].children[1].preferred&&guard.definition.tuning.preferred_range==1.4&&guard.definition.tuning.recovery_scale==1,"Owned Royal Guard lost its frozen behaviour tree or tuning");
    check(load_cooked_behavior(cache/"registry.json",assets.cas_path(),guard.behavior->id).generation==guard.behavior->generation&&rejects([&]{load_cooked_behavior(cache/"registry.json",assets.cas_path(),npc_id);}),"Behaviour products are not kind-checked");

    // In a session: the NPC's own kit and attributes, its authored tree as the controller, and the navigation mesh for the approach leaf.
    CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}},{101,{0,1,3},{4,1,.25}}};auto mesh=std::make_shared<const NavigationMesh>(collision);
    const auto& cooked=guard.kit;CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    combat.actors={CharacterSceneActor{StableId{9,2},cooked.definition,cooked.input,cooked.abilities,guard.attributes,guard.definition.name,guard.definition.faction,guard.definition.generation,make_behavior_controller(*guard.behavior,mesh,guard.definition.tuning)}};
    ObjectData player;player.id={9,1};ObjectData target;target.id={9,2};target.transform.z=6;target.health={60,60};std::array<ObjectData,2> objects{player,target};SessionHandshake hello{3,sha256("behavior scene wire3"),sha256(cooked.definition->generation),705};
    CharacterSceneSession scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);const auto baseline=scene.resynchronizations();
    auto health=[&]{return scene.presentation().read(scene.presentation().find(player.id)).health.current;};
    double widest{};std::uint64_t first{},second{};while(scene.motor().tick<600&&!second){scene.step({});widest=std::max(widest,std::abs(scene.observer(target.id,double(scene.motor().tick)).frame.motor.position.x));if(!first&&health()<100)first=scene.motor().tick;else if(first&&health()<75)second=scene.motor().tick;}
    check(widest>4&&first&&second&&second-first>=90&&health()==50,"The behaviour tree did not walk the NPC around the wall, attack, recover and attack again through authority");
    check(!scene.controller_status(target.id).empty()&&scene.presentation().read(scene.presentation().find(target.id)).health.current==60&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"A tree-driven NPC disturbed its own state or owner prediction");
    std::cout<<"Behaviour tree: around the wall (widest x "<<widest<<" m), first hit tick "<<first<<", second hit tick "<<second<<"\n";
    std::cout<<"Behaviour trees: JSON topology decoding and rejection, BT.CPP executor behind the controller seam (approach, face, attack, recover), frozen behaviour on the owned Royal Guard, and a tree-driven NPC navigating and attacking in a session passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
