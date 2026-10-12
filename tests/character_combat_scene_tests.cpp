#include <darkangel/character_scene.hpp>
#include <darkangel/assembly.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/character_presentation.hpp>
#include <ashen_roots/royal_combat.hpp>
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same(const std::vector<JointMatrix>& a,const std::vector<JointMatrix>& b){if(a.size()!=b.size())return false;for(unsigned joint=0;joint<a.size();++joint)for(unsigned i=0;i<16;++i)if(std::abs(a[joint].values[i]-b[joint].values[i])>2e-5)return false;return true;}}
int main(){try{
    auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("character-combat-"+AssetId::random().text());std::ifstream source(content/"royal_district/combat/player.dakit");auto data=nlohmann::json::parse(source);auto id=AssetId::parse(data.at("asset").get<std::string>());AssetService assets(content,cache);assets.cook("royal_district/combat/player.dakit");assets.package(id,cache/"registry.json");auto cooked=load_cooked_combat_kit(cache/"registry.json",assets.cas_path(),id);
    CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    ObjectData player;player.id={9,1};ObjectData target;target.id={9,2};target.transform.z=1.2;target.transform.yaw=3.141592653589793;std::array<ObjectData,2> objects{player,target};World authoring(WorldDomain::Authoring);for(auto object:objects)authoring.create(object);auto original=authoring.serialize();CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}}};SessionHandshake hello{3,sha256("character combat scene wire3"),sha256(cooked.definition->generation),703};
    // The training target is one server-driven actor: unequipped, uncontrolled, with the scene's own starting attributes.
    {CharacterSceneActor dummy;dummy.id=target.id;dummy.attributes=combat.attributes;combat.actors={dummy};}
    // The scripted training attacker on the first actor. A plain dummy borrows the scene kit so it has something to press; a named NPC keeps its own.
    auto arm=[](CharacterSceneCombat& scene,ScriptedAttack script){auto& actor=scene.actors.front();if(actor.name.empty()){actor.kit=scene.kit;actor.input=scene.input;actor.abilities=scene.abilities;}actor.controller=make_scripted_attacker(script);actor.senses.everything=true;};
    {CharacterSceneSession bounded(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);CharacterSceneInput excessive;excessive.combat_events.assign(33,{8,InputEdge::Pressed,1000,0,1,false});bool rejected=false;try{bounded.advance(0,excessive);}catch(const std::exception&){rejected=true;}check(rejected,"Combat queue overflow rejects explicitly");bounded.advance(1./60,{});check(!bounded.ability()->active,"Rejected input batch cannot leave a queued partial activation");}
    CharacterSceneSession session(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);
    CharacterSceneInput pressed;pressed.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};check(session.advance(1./144,pressed)==0,"Render edge retained before fixed tick");while(session.motor().tick<1)session.advance(1./144,{});check(session.ability()&&session.ability()->active,"Serialized owned intent starts existing native action");check(session.predicted_ability()&&session.predicted_ability()->active==session.ability()->active&&session.pending_abilities()==0,"Atomic owner prediction is published only after prepared motor correction");
    CharacterSceneInput released;released.combat_events={{8,InputEdge::Released,2000,0,0,false},{8,InputEdge::Tapped,2000,0,1,false}};session.step(released);while(session.motor().tick<30)session.step({});auto remote=session.observer(player.id,28);check(remote.frame.ability.active&&remote.action_clock==27*action_tick_units,"Royal observer buffers current action phase on the motor render clock");auto public_target=session.observer(target.id,30);check(public_target.frame.ability.attributes.size()==2,"Royal public schema preserves owner-only Stamina/Essence privacy");while(session.motor().tick<60)session.step({});auto health=[&]{return session.presentation().read(session.presentation().find(target.id)).health.current;};check(health()==75,"Authoritative hit replicates target Health through existing baseline");check(session.pending_prediction()==0&&!session.ability()->active,"Motor correction and action completion retire execution");
    auto stamina=[&]{for(const auto& attribute:session.ability()->attributes)if(attribute.id==3)return attribute.value;return -1.;};check(stamina()==90,"Tap/release cannot duplicate the Pressed cost");
    for(unsigned attack=2;attack<=4;++attack){session.step(pressed);session.step(released);while(session.motor().tick<60*attack)session.step({});check(stamina()==100-10*attack,"Repeated serialized operations charge once per activation");check(health()==100-25*attack,"One damage outcome per action/target despite persistent hit window");}
    check(health()==0&&authoring.serialize()==original,"Death baseline and gameplay isolation preserve authoring");session.step(pressed);session.step(released);while(session.motor().tick<300)session.step({});check(health()==0&&stamina()==50,"Dead target never receives repeated damage");
    // Scripted training attacker: the target presses a slot of the same kit through the server's checked request path.
    // Its facing is the direction to the player plus the attack clip's own root yaw.
    {
        const auto& plan=cooked.stance.plan;const auto& rig=cooked.stance.rig;
        auto player_health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(player.id)).health.current;};
        auto target_health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(target.id)).health.current;};
        auto attribute=[&](const CharacterSceneSession& scene,unsigned id){for(const auto& value:scene.ability()->attributes)if(value.id==id)return value.value;return -1.;};
        auto attacked=combat;arm(attacked,{CombatSlot::Light,10,120,3,true});
        auto away=objects;away[1].transform.yaw=0;std::uint64_t hit_tick{};
        // The session bootstrap itself counts as one prediction baseline; nothing afterwards may add another.
        unsigned baseline{};
        {
            CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,attacked);
            while(scene.motor().tick<9)scene.step({});check(!scene.controller_receipt(target.id)&&!scene.observer(target.id,9).frame.ability.active&&player_health(scene)==100,"Scripted attacker acted before its first tick");
            auto idle=*scene.pose(target.id);baseline=scene.resynchronizations();scene.step({});
            check(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->failure==AbilityFailure::None&&scene.controller_receipt(target.id)->committed,"Scripted attacker request was not committed by authority");
            auto seen=scene.observer(target.id,10);check(bool(seen.frame.ability.active),"Target attack is not public state");
            check(std::abs(std::remainder(seen.frame.motor.yaw-3.141592653589793,6.283185307179586))<.1,"Scripted attacker did not turn to the player on activation");
            while(scene.motor().tick<70){scene.step({});if(!hit_tick&&player_health(scene)<100)hit_tick=scene.motor().tick;if(scene.motor().tick==30)check(!same(*scene.pose(target.id),idle),"Target attack is not presented from its public action");}
            check(hit_tick>10&&hit_tick<45&&player_health(scene)==75&&attribute(scene,2)==75,"Target hit did not damage the player once through authority and the owner correction");
            check(scene.pending_prediction()==0&&scene.resynchronizations()==baseline&&!scene.ability()->active&&scene.pending_abilities()==0&&target_health(scene)==100,"Incoming damage disturbed owner prediction or the attacker");
            while(scene.motor().tick<200)scene.step({});check(player_health(scene)==50&&scene.controller_receipt(target.id)->operation==2,"Second scheduled attack did not land exactly once");
        }
        {auto far=away;far[1].transform.z=6;CharacterSceneSession scene(hello,far,player.id,collision,plan,rig.definition,rig.archive,attacked);while(scene.motor().tick<80)scene.step({});check(!scene.controller_receipt(target.id)&&player_health(scene)==100,"Scripted attacker ignored its range");}
        {auto blind=attacked;arm(blind,{CombatSlot::Light,10,120,3,false});CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,blind);while(scene.motor().tick<80)scene.step({});check(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->failure==AbilityFailure::None&&player_health(scene)==100,"A swing facing away must miss: hits come from the authoritative query only");}
        {auto invalid=attacked;arm(invalid,{CombatSlot::Parry,10,120,3,true});CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,invalid);while(scene.motor().tick<40)scene.step({});check(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->failure==AbilityFailure::Unassigned&&player_health(scene)==100,"A press of a slot the kit does not assign was not refused by authority");}

        // Royal game rules. The kit binds the owned Flinch to the Light hit by default: owner reaction, interruption, stagger, invulnerability and death.
        std::ifstream flinch_source(content/"royal_district/combat/flinch.daeffect");const auto flinch_id=AssetId::parse(nlohmann::json::parse(flinch_source).at("asset").get<std::string>());
        auto bound_flinch=std::find_if(cooked.effects.begin(),cooked.effects.end(),[&](const auto& effect){return effect->id==flinch_id;});check(bound_flinch!=cooked.effects.end(),"Royal kit no longer binds the owned Flinch");auto flinch=*bound_flinch;
        check(flinch->reaction&&flinch->interrupt_action&&flinch->lock_movement&&!flinch->reaction_loop,"Owned Royal flinch lost its reaction, interruption or movement lock");
        auto royal=combat;royal.effects=cooked.effects;for(const auto& effect:royal.effects)if(effect->reaction&&!royal.clips.contains(effect->reaction->motion->clip.id)){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),effect->reaction->motion->clip.id);royal.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
        royal.damage={};ashen_roots::configure_royal_combat(royal);
        royal.death=flinch->reaction;arm(royal,{CombatSlot::Light,10,60,3,true});
        const auto& reaction_clip=*royal.clips.at(flinch->reaction->motion->clip.id);RigPose oracle(rig.definition,rig.archive);
        {
            CharacterSceneSession scene(hello,objects,player.id,collision,plan,rig.definition,rig.archive,royal);
            CharacterSceneInput heavy;heavy.combat_events={{9,InputEdge::Pressed,1000,0,1,false}};
            while(scene.motor().tick<hit_tick-4)scene.step({});scene.step(heavy);check(scene.ability()->active&&!scene.reaction(player.id),"Player Heavy did not start before the incoming hit");
            while(scene.motor().tick<hit_tick)scene.step({});
            check(player_health(scene)==75&&!scene.ability()->active&&!scene.predicted_ability()->active&&scene.pending_abilities()==0,"Incoming flinch did not interrupt the player's own action through authority and reconciliation");
            auto shown=scene.reaction(player.id);check(shown&&shown->suppressed==ReactionSuppression::None&&shown->effect==flinch->id&&shown->tick==double(scene.motor().tick-shown->start),"Owner flinch is not selected from the public effect clock");
            check(same(scene.pose(),oracle.sample(reaction_clip,shown->tick)),"Owner pose is not the flinch clip sample");
            // The status, not the animation, gates the next activation; a refused press settles without a resync.
            scene.step(heavy);scene.step({});scene.step({});check(!scene.ability()->active&&!scene.predicted_ability()->active&&scene.pending_abilities()==0&&target_health(scene)==100,"Staggered player started an attack or the interrupted Heavy still hit");
            // Stagger restricts movement on authority and in prediction alike; it ends with the status, not the clip.
            CharacterSceneInput walk;walk.x=1;walk.jump=true;const auto held=scene.motor().position;for(unsigned i=0;i<10;++i)scene.step(walk);
            auto apart=[](const MotorVec& a,const MotorVec& b){return std::hypot(a.x-b.x,a.z-b.z);};
            check(apart(scene.motor().position,held)<1e-6&&std::abs(scene.motor().position.y-held.y)<1e-3&&apart(scene.predicted_motor().position,scene.motor().position)<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Staggered player walked or jumped, or prediction disagreed with authority");
            while(scene.motor().tick<hit_tick+30)scene.step({});walk.jump=false;for(unsigned i=0;i<6;++i)scene.step(walk);
            check(scene.motor().position.x-held.x>.1&&apart(scene.predicted_motor().position,scene.motor().position)<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Player could not walk again when the stagger expired");
            while(scene.motor().tick<260)scene.step({});
            auto death=scene.death(player.id);check(player_health(scene)==0&&attribute(scene,2)==0&&death&&*death>0&&!scene.death(target.id),"Four target hits did not kill the player and start the owner death timeline");
            check(same(scene.pose(),oracle.sample(reaction_clip,*death)),"Dead player pose is not the death timeline sample");
            const auto last=scene.controller_receipt(target.id)->tick;CharacterSceneInput pressed_light;pressed_light.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};scene.step(pressed_light);const auto corpse=scene.motor().position;walk.jump=true;while(scene.motor().tick<400)scene.step(walk);
            check(apart(scene.motor().position,corpse)<1e-6&&std::abs(scene.motor().position.y-corpse.y)<1e-3&&apart(scene.predicted_motor().position,corpse)<1e-6,"Dead player walked or jumped");
            check(scene.controller_receipt(target.id)->tick==last&&!scene.ability()->active&&!scene.predicted_ability()->active&&scene.pending_abilities()==0&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline&&target_health(scene)==100,"Dead player acted, or the attacker kept attacking a dead player");
            check(same(scene.pose(),oracle.sample(reaction_clip,*scene.death(player.id))),"Death pose is not held");
        }
        // The owned Stun (unbound by default) on the Heavy hit: stun loop on the owner pose and a movement lock for the whole status.
        {
            assets.cook("royal_district/combat/stun.daeffect");std::ifstream stun_source(content/"royal_district/combat/stun.daeffect");const auto stun_id=AssetId::parse(nlohmann::json::parse(stun_source).at("asset").get<std::string>());assets.package(stun_id,cache/"stun.json");
            auto stun=load_cooked_effect(cache/"stun.json",assets.cas_path(),stun_id).definition;check(stun->reaction&&stun->reaction_loop&&stun->lock_movement&&stun->duration_ticks==90,"Owned Royal stun lost its loop, lock or duration");
            auto stunned=royal;stunned.death={};stunned.effects.push_back(stun);auto clip=load_cooked_clip(cache/"stun.json",assets.cas_path(),stun->reaction->motion->clip.id);stunned.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));
            const auto heavy_ability=cooked.definition->slots[1].ability;auto bound=std::make_shared<CombatKitDefinition>(*cooked.definition);bound->effects.push_back(stun->id);bound->effect_bindings.push_back({heavy_ability,2,stun->id,1});stunned.kit=bound;stunned.damage={};stunned.combat_damage={};stunned.effect_evaluators.clear();ashen_roots::configure_royal_combat(stunned);
            arm(stunned,{CombatSlot::Heavy,10,600,3,true});CharacterSceneSession scene(hello,objects,player.id,collision,plan,rig.definition,rig.archive,stunned);
            while(scene.motor().tick<70&&player_health(scene)==100)scene.step({});const auto stun_tick=scene.motor().tick;check(stun_tick<70,"Target Heavy did not hit the player");
            CharacterSceneInput walk;walk.z=-1;walk.jump=true;const auto held=scene.motor().position;while(scene.motor().tick<stun_tick+60)scene.step(walk);
            auto shown=scene.reaction(player.id);check(shown&&shown->effect==stun->id&&shown->suppressed==ReactionSuppression::None&&shown->tick==double(scene.motor().tick-shown->start)&&same(scene.pose(),oracle.sample(*stunned.clips.at(stun->reaction->motion->clip.id),shown->tick)),"Owner pose is not the stun loop on the effect clock");
            check(std::hypot(scene.motor().position.x-held.x,scene.motor().position.z-held.z)<1e-6&&std::hypot(scene.predicted_motor().position.x-held.x,scene.predicted_motor().position.z-held.z)<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Stunned player moved or prediction disagreed");
            while(scene.motor().tick<stun_tick+100)scene.step(walk);check(scene.motor().position.z<held.z-.2&&!scene.reaction(player.id),"Stun did not end with its status");
        }
        // Dodge invulnerability against a real incoming hit. A wall behind the player stops the dash, so only the tag window differs.
        auto walled=collision;walled.boxes.push_back({101,{0,1,-1.2},{4,1,.5}});royal.death={};arm(royal,{CombatSlot::Light,60,600,3,true});const auto dodge_hit=hit_tick+50;
        auto dodge_run=[&](std::uint64_t press){
            auto scene=std::make_unique<CharacterSceneSession>(hello,objects,player.id,walled,plan,rig.definition,rig.archive,royal);
            CharacterSceneInput dodge;dodge.z=-1;dodge.combat_events={{6,InputEdge::Pressed,1000,0,1,false}};
            while(scene->motor().tick<press-1)scene->step({});scene->step(dodge);while(scene->motor().tick<dodge_hit+20)scene->step({});
            check(attribute(*scene,3)==85&&scene->controller_receipt(target.id)&&scene->controller_receipt(target.id)->failure==AbilityFailure::None&&scene->pending_prediction()==0&&scene->resynchronizations()==baseline,"Dodge or the scripted attack did not run through authority");return scene;};
        auto late=dodge_run(dodge_hit-14),early=dodge_run(dodge_hit-45);
        check(std::abs(late->motor().position.z-early->motor().position.z)<.05&&std::abs(late->motor().position.x-early->motor().position.x)<.05,"Dodge control and invulnerable runs ended in different places");
        check(player_health(*early)==75&&early->reaction(player.id),"Control: a hit after the invulnerability window must damage and stagger the player");
        check(player_health(*late)==100&&!late->reaction(player.id),"A hit inside the dodge invulnerability window damaged or staggered the player");
        while(late->motor().tick<dodge_hit+60)late->step({});check(player_health(*late)==100,"The avoided hit window damaged the player later");
        check(authoring.serialize()==original,"Scripted attacker changed the authoring world");
    }
    // NPC on the combat target (M6): its own kit and starting attributes, driven only through the controller seam.
    {
        const auto light_id=cooked.definition->slots[0].ability,heavy_id=cooked.definition->slots[1].ability;
        auto guard_kit=std::make_shared<CombatKitDefinition>(*cooked.definition);guard_kit->slots[1].ability={};std::erase_if(guard_kit->effect_bindings,[&](const auto& binding){return binding.ability==heavy_id;});
        CharacterSceneActor guard;guard.id=target.id;guard.kit=guard_kit;guard.input=cooked.input;for(const auto& ability:cooked.abilities)if(ability->id!=heavy_id)guard.abilities.push_back(ability);guard.attributes=combat.attributes;for(auto& attribute:guard.attributes)if(attribute.id==1||attribute.id==2)attribute.base=60;guard.name="Test Guard";guard.faction="royal_guard";guard.senses.everything=true;guard.generation=std::string(64,'a');
        auto placed=combat;placed.actors={guard};auto guarded=objects;guarded[1].health={60,60};
        auto target_health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(target.id)).health.current;};
        auto player_health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(player.id)).health.current;};
        auto public_health=[&](const CharacterSceneSession& scene){for(const auto& value:scene.observer(target.id,double(scene.motor().tick)).frame.ability.attributes)if(value.id==2)return value.value;return -1.;};
        {
            // Same Character and ability execution as the player, different attributes: three Light hits defeat a 60 Health guard.
            CharacterSceneSession scene(hello,guarded,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,placed);
            scene.step({});check(target_health(scene)==60&&public_health(scene)==60&&player_health(scene)==100&&!scene.controller_receipt(target.id),"NPC target did not start from its own attributes, or acted without a controller");
            CharacterSceneInput press;press.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};CharacterSceneInput release;release.combat_events={{8,InputEdge::Released,2000,0,0,false}};
            for(unsigned attack=1;attack<=3;++attack){scene.step(press);scene.step(release);while(scene.motor().tick<70*attack)scene.step({});check(target_health(scene)==std::max(0.,60.-25*attack)&&public_health(scene)==target_health(scene),"Player hits did not reduce the NPC's own Health through authority");}
            check(target_health(scene)==0&&scene.pending_prediction()==0,"NPC was not defeated by three Light hits");
        }
        {
            // The scripted attacker is now one controller behind the seam and presses the NPC's own kit.
            auto attacking=placed;arm(attacking,{CombatSlot::Light,10,120,3,true});auto away=guarded;away[1].transform.yaw=0;
            CharacterSceneSession scene(hello,away,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,attacking);const auto baseline=scene.resynchronizations();
            while(scene.motor().tick<70)scene.step({});check(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->committed&&player_health(scene)==75&&target_health(scene)==60&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Scripted controller on the NPC kit did not hit the player once through authority");
            auto unarmed=placed;arm(unarmed,{CombatSlot::Heavy,10,120,3,true});CharacterSceneSession refused(hello,away,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,unarmed);while(refused.motor().tick<40)refused.step({});check(refused.controller_receipt(target.id)&&refused.controller_receipt(target.id)->failure==AbilityFailure::Unassigned&&player_health(refused)==100,"A press of a slot the NPC kit does not assign was not refused by authority");
        }
        {
            // A controller asks for move, face and a slot press, the same operations a player intent carries.
            struct Chaser final:ActorController {unsigned heavy_presses{};ControllerCommand decide(const ControllerView& view)override{
                ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);
                command.face=std::atan2(dx,dz);if(distance>1.5){command.move_x=dx/distance;command.move_z=dz/distance;}else if(!view.busy)command.press=view.tick%2?CombatSlot::Light:(++heavy_presses,CombatSlot::Heavy);return command;}};
            auto chaser=std::make_shared<Chaser>();auto hunted=placed;hunted.actors.front().controller=chaser;auto far=guarded;far[1].transform.z=6;far[1].transform.yaw=0;
            CharacterSceneSession scene(hello,far,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,hunted);const auto baseline=scene.resynchronizations();
            while(scene.motor().tick<40)scene.step({});const auto walking=scene.observer(target.id,40).frame.motor;check(walking.position.z<5.5&&std::abs(std::remainder(walking.yaw-3.141592653589793,6.283185307179586))<.05&&player_health(scene)==100,"Controller move/face requests did not walk the NPC toward the player");
            bool refused=false;while(scene.motor().tick<400&&player_health(scene)>0){scene.step({});if(scene.controller_receipt(target.id)&&scene.controller_receipt(target.id)->failure==AbilityFailure::Unassigned)refused=true;}
            const auto arrived=scene.observer(target.id,double(scene.motor().tick)).frame.motor.position;
            check(std::hypot(arrived.x-scene.motor().position.x,arrived.z-scene.motor().position.z)<2&&player_health(scene)<100&&target_health(scene)==60,"Controller did not close in and hit the player through the ordinary ability path");
            check(chaser->heavy_presses>0&&refused&&scene.controller_target(target.id)==player.id&&scene.actors().size()==1&&scene.actors()[0]==target.id,"A press of a slot the NPC kit does not assign was not refused by authority");
            check(scene.pending_prediction()==0&&scene.resynchronizations()==baseline&&scene.pending_abilities()==0,"A controlled NPC disturbed owner prediction");
            std::cout<<"NPC controller: walked 6 m -> "<<std::hypot(arrived.x-scene.motor().position.x,arrived.z-scene.motor().position.z)<<" m, player Health "<<player_health(scene)<<", refused unassigned presses\n";
        }
        {
            struct Runaway final:ActorController {ControllerCommand decide(const ControllerView&)override{ControllerCommand command;command.move_x=5;return command;}};
            auto broken=placed;broken.actors.front().controller=std::make_shared<Runaway>();CharacterSceneSession scene(hello,guarded,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,broken);bool rejected=false;try{scene.step({});}catch(const std::exception&){rejected=true;}check(rejected,"An out-of-bounds controller command was accepted");
        }
    }
    // Crosshair aim and the first ranged ability: the owned Bow Shot sweeps its hit along the actor's facing (the commanded yaw the
    // server already validates) up to its authored range and stops at the first actor; static collision blocks it.
    {
        const auto bow_id=cooked.definition->slots[2].ability;auto bow=std::find_if(cooked.abilities.begin(),cooked.abilities.end(),[&](const auto& ability){return ability->id==bow_id;});
        check(bow!=cooked.abilities.end()&&(*bow)->aim&&(*bow)->aim->range==20&&!(*bow)->soft_lock&&(*bow)->melee.size()==1&&(*bow)->melee[0].sweep==20,"Owned Royal Bow Shot lost its aim range or swept hit profile");
        {auto bad=**bow;bad.melee[0].sweep=0;bool rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"An aim ability with an unswept hit profile was frozen");bad=**bow;bad.aim->range=500;bad.melee[0].sweep=500;rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"An aim range beyond the bound was frozen");}
        auto health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(target.id)).health.current;};
        auto stamina_of=[&](const CharacterSceneSession& scene){for(const auto& attribute:scene.ability()->attributes)if(attribute.id==3)return attribute.value;return -1.;};
        auto shoot=[&](std::span<const ObjectData> placed,const CollisionDefinition& world,double yaw,double pitch=0){
            auto scene=std::make_unique<CharacterSceneSession>(hello,placed,player.id,world,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);CharacterSceneInput idle;idle.yaw=yaw;idle.pitch=pitch;
            while(scene->motor().tick<5)scene->step(idle);const auto baseline=scene->resynchronizations();auto press=idle;press.combat_events={{14,InputEdge::Pressed,1000,0,1,false}};scene->step(press);
            check(scene->ability()->active&&scene->ability()->ability==bow_id&&scene->predicted_ability()->active&&!scene->soft_lock(),"Bow Shot did not start through authority and prediction");
            while(scene->motor().tick<75)scene->step(idle);check(stamina_of(*scene)==90&&!scene->ability()->active&&scene->pending_prediction()==0&&scene->resynchronizations()==baseline,"Bow Shot did not charge once and settle");return scene;};
        auto distant=objects;distant[1].transform.z=8;
        check(health(*shoot(distant,collision,0))==80,"An aimed shot did not hit a target 8 m straight ahead exactly once");
        check(health(*shoot(distant,collision,.5))==100,"A shot aimed away from the target hit it");
        auto beyond=objects;beyond[0].transform.z=-9;beyond[1].transform.z=14;check(health(*shoot(beyond,collision,0))==100,"A shot hit a target beyond its range");
        auto walled=collision;walled.boxes.push_back({102,{0,1,4},{2,1,.2}});check(health(*shoot(distant,walled,0))==100,"A shot passed through static collision");
        check(health(*shoot(objects,collision,0))==80,"A shot did not hit a target at melee distance");
        // Travelling projectile: the owned Bow Shot launches an arrow from its hit profile's offset. Authority flies it one swept step
        // a tick; the client sees it in flight as replicated public state and the damage arrives when it lands, not when it is loosed.
        {
            check((*bow)->aim->projectile&&(*bow)->aim->projectile->speed==40&&(*bow)->aim->projectile->gravity==5&&(*bow)->aim->projectile->lifetime==120,"Owned Royal Bow Shot lost its projectile");
            {auto bad=**bow;bad.aim->projectile->speed=0;bool rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"A projectile without speed was frozen");bad=**bow;bad.aim->projectile->lifetime=0;rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"A projectile without a lifetime was frozen");}
            auto far=objects;far[1].transform.z=16;
            auto flight=[&](const CharacterSceneCombat& content,std::uint64_t& loosed,std::uint64_t& landed,double& lowest,unsigned& most){
                CharacterSceneSession scene(hello,far,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,content);while(scene.motor().tick<5)scene.step({});CharacterSceneInput press;press.combat_events={{14,InputEdge::Pressed,1000,0,1,false}};scene.step(press);
                loosed=landed=0;lowest=10;most=0;double last_z=-1;std::uint64_t identity=0;
                while(scene.motor().tick<120){scene.step({});const auto& seen=scene.projectiles();most=std::max(most,unsigned(seen.size()));
                    if(!seen.empty()){if(!loosed)loosed=scene.motor().tick;check(seen.size()==1&&(!identity||seen[0].id==identity)&&seen[0].position.z>last_z&&seen[0].velocity.z>30&&health(scene)==100,"A projectile in flight is not one replicated object moving forward before its hit");identity=seen[0].id;last_z=seen[0].position.z;lowest=std::min(lowest,seen[0].position.y);}
                    if(!landed&&health(scene)<100)landed=scene.motor().tick;}
                check(scene.projectiles().empty()&&scene.pending_prediction()==0&&stamina_of(scene)==90,"A landed projectile is still listed, or the shot disturbed the owner");return health(scene);};
            std::uint64_t loosed{},landed{};double lowest{};unsigned most{};
            check(flight(combat,loosed,landed,lowest,most)==80&&loosed&&landed>=loosed+15&&landed<=loosed+30&&most==1,"The arrow did not fly about 15 m at 40 m/s before its one hit");
            check(lowest<(*bow)->melee[0].offset[1]-.15&&lowest>.5,"Gravity did not bend the arrow's flight");
            std::cout<<"Projectile: loosed tick "<<loosed<<", landed tick "<<landed<<", lowest point "<<lowest<<" m\n";
            // Control: the same ability without a projectile is the instant sweep; nothing is ever in flight.
            auto instant=combat;for(auto& ability:instant.abilities)if(ability->id==bow_id){auto copy=*ability;copy.aim->projectile.reset();ability=std::make_shared<const AbilityDefinition>(std::move(copy));}
            std::uint64_t swept_loosed{},swept_landed{};check(flight(instant,swept_loosed,swept_landed,lowest,most)==80&&!swept_loosed&&most==0&&swept_landed&&swept_landed<landed-10,"Control: without a projectile the hit must be instant");
        }
        // Vertical aim: a target standing on a slab 3 m up. A level shot passes under it; the aim elevation carried in the motor
        // input and confirmed in the motor state sends the same shot from the muzzle up to it.
        {
            auto raised=objects;raised[1].transform.z=8;raised[1].transform.y=3;auto slab=collision;slab.boxes.push_back({103,{0,2.75,8},{1.5,.25,1.5}});
            const auto& muzzle=(*bow)->melee[0].offset;const double elevation=std::atan2(3+1.0-muzzle[1],8-muzzle[2]);
            check(health(*shoot(raised,slab,0))==100,"A level shot hit a target standing 3 m above it");
            auto up=shoot(raised,slab,0,elevation);check(health(*up)==80&&up->motor().pitch==elevation&&up->predicted_motor().pitch==elevation&&up->observer(player.id,double(up->motor().tick)).frame.motor.pitch==elevation,"A shot with the aim elevation did not hit the raised target, or the elevation is not confirmed state");
            check(health(*shoot(raised,slab,0,-elevation))==100&&health(*shoot(distant,collision,0,elevation))==100,"A shot aimed down hit the raised target, or a shot aimed up hit a target on the ground");
            {CharacterSceneSession scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);CharacterSceneInput steep;steep.pitch=2;bool rejected=false;try{scene.step(steep);}catch(const std::exception&){rejected=true;}check(rejected,"An aim elevation beyond the bound was accepted");}
            // Crosshair convergence: the owner turns a camera ray into the yaw and pitch that send a shot from its muzzle through the
            // point the ray meets. Authority never sees the ray, only the two angles.
            CharacterSceneSession scene(hello,raised,player.id,slab,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);while(scene.motor().tick<5)scene.step({});
            const MotorVec offset{muzzle[0],muzzle[1],muzzle[2]},camera{.6,1.9,-3};
            auto miss=[&](const AimSolution& solved){const auto& at=scene.predicted_motor().position;const double c=std::cos(solved.yaw),sn=std::sin(solved.yaw);const MotorVec origin{at.x+c*offset.x+sn*offset.z,at.y+offset.y,at.z-sn*offset.x+c*offset.z},along{sn*std::cos(solved.pitch),std::sin(solved.pitch),c*std::cos(solved.pitch)},to{solved.point.x-origin.x,solved.point.y-origin.y,solved.point.z-origin.z};const double t=to.x*along.x+to.y*along.y+to.z*along.z;return std::hypot(std::hypot(to.x-along.x*t,to.y-along.y*t),to.z-along.z*t);};
            auto actor=scene.aim(camera,{0-camera.x,4-camera.y,8-camera.z},20,offset);check(actor.hit&&std::abs(actor.point.z-8)<.5&&actor.point.y>3.5&&actor.point.y<4.5&&actor.pitch>.2&&miss(actor)<.02,"The crosshair on the raised actor did not converge on it from the muzzle");
            auto ground=scene.aim(camera,{0-camera.x,0-camera.y,6-camera.z},20,offset);check(ground.hit&&std::abs(ground.point.y)<.02&&std::abs(ground.point.z-6)<.1&&ground.pitch<-.1&&miss(ground)<.02,"The crosshair on the ground did not converge on the ground point");
            auto sky=scene.aim(camera,{0,1,1},20,offset);check(!sky.hit&&sky.point.y>10&&sky.pitch>.6&&miss(sky)<.02,"A crosshair on nothing did not aim at the far point");
            // Steeply down from above and behind: the ground between the camera and the owner is not the aim point.
            auto behind=scene.aim({0,6,-2},{0,-6,2.5},20,offset);check(std::isfinite(behind.yaw)&&std::abs(behind.yaw)<.2&&behind.pitch<0,"A ray that lands at the owner's feet turned the owner around");
            // The solved angles through the session: the shot lands on the raised target.
            CharacterSceneInput aimed;aimed.yaw=actor.yaw;aimed.pitch=actor.pitch;auto press=aimed;press.combat_events={{14,InputEdge::Pressed,1000,0,1,false}};scene.step(press);while(scene.motor().tick<75)scene.step(aimed);check(health(scene)==80&&scene.pending_prediction()==0,"A shot aimed by crosshair convergence missed the actor under the crosshair");
            std::cout<<"Vertical aim: elevation "<<elevation<<" rad, converged yaw "<<actor.yaw<<" pitch "<<actor.pitch<<", muzzle line error "<<miss(actor)<<" m\n";
        }
    }
    // Melee soft lock on the owned Light: the targeting module proposes the target, the motor turns the swing and closes distance,
    // and authority, owner prediction and the hit query agree. A copy without targeting is the control.
    {
        const double pi=3.141592653589793;const auto light_id=cooked.definition->slots[0].ability;auto light=std::find_if(cooked.abilities.begin(),cooked.abilities.end(),[&](const auto& ability){return ability->id==light_id;});
        check(light!=cooked.abilities.end()&&(*light)->soft_lock&&(*light)->soft_lock->range==4.5&&std::abs((*light)->soft_lock->cone-75*pi/180)<1e-12&&(*light)->soft_lock->stop==1.2&&(*light)->soft_lock->distance==2.0&&(*light)->soft_lock->begin==0&&(*light)->soft_lock->end==16*action_tick_units,"Owned Royal Light lost its authored soft lock");
        {auto bad=**light;bad.soft_lock->cone=0;bool rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"Zero soft-lock cone was frozen");bad=**light;bad.dash=AbilityDash{1,0,16*action_tick_units};rejected=false;try{freeze_ability_definition(bad,AttributeSet(combat.attributes),combat.tags.get());}catch(const std::exception&){rejected=true;}check(rejected,"Soft lock combined with a dash was frozen");}
        auto aside=objects;aside[1].transform.x=1.5;aside[1].transform.z=1.5;
        auto health=[&](const CharacterSceneSession& scene){return scene.presentation().read(scene.presentation().find(target.id)).health.current;};
        auto apart=[](const MotorVec& a,const MotorVec& b){return std::hypot(a.x-b.x,a.z-b.z);};
        CharacterSceneInput light_press;light_press.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};
        {
            CharacterSceneSession scene(hello,aside,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);
            while(scene.motor().tick<5)scene.step({});const auto baseline=scene.resynchronizations();const auto seen=scene.observer(target.id,5).frame.motor.position;const double before=apart(scene.motor().position,seen);check(!scene.soft_lock()&&std::abs(before-std::hypot(1.5,1.5))<.01,"Soft-lock fixture layout");
            scene.step(light_press);const double toward=std::atan2(seen.x-scene.motor().position.x,seen.z-scene.motor().position.z);
            check(scene.ability()->active&&scene.soft_lock()==target.id&&std::abs(std::remainder(scene.motor().yaw-toward,2*pi))<1e-3&&std::abs(std::remainder(scene.predicted_motor().yaw-scene.motor().yaw,2*pi))<1e-12,"Light did not turn to the soft-lock target on activation under authority and prediction");
            double worst{};while(scene.motor().tick<21){scene.step({});worst=std::max(worst,apart(scene.predicted_motor().position,scene.motor().position));if(scene.motor().tick<=20)check(scene.soft_lock()==target.id,"Soft lock dropped inside its window");}
            const double after=apart(scene.motor().position,seen);
            check(worst<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Soft-lock warp diverged between prediction and authority");
            check(after<before-.5&&after<1.3&&after>.6&&std::abs(std::remainder(scene.motor().yaw-std::atan2(seen.x-scene.motor().position.x,seen.z-scene.motor().position.z),2*pi))<.05,"Soft lock did not close on the target and stop near its stop distance");
            while(scene.motor().tick<24)scene.step({});check(!scene.soft_lock(),"Soft lock indicator outlived its window");
            while(scene.motor().tick<65)scene.step({});check(health(scene)==75&&!scene.ability()->active&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Soft-locked Light did not hit the off-axis target exactly once");
            std::cout<<"Soft lock closed "<<before<<" -> "<<after<<" m, predicted_error="<<worst<<"\n";
        }
        {
            // Shared soft lock for NPC attacks: a guard 2.5 m away presses Light at its controller's target. Its swing turns and
            // closes exactly as the player's does, toward that target only; without targeting on the ability it stays put and misses.
            auto guard=[&](const CharacterSceneCombat& scene){CharacterSceneActor made;made.id=target.id;made.kit=scene.kit;made.input=scene.input;made.abilities=scene.abilities;made.attributes=scene.attributes;made.name="Guard";made.faction="royal_guard";made.controller=make_scripted_attacker({CombatSlot::Light,10,600,3,true});made.senses.everything=true;return made;};
            auto apart_now=[&](const CharacterSceneSession& scene){return apart(scene.observer(target.id,double(scene.motor().tick)).frame.motor.position,scene.motor().position);};
            auto reach=objects;reach[1].transform.z=2.5;reach[1].transform.yaw=0;
            auto locking=combat;locking.actors={guard(locking)};CharacterSceneSession scene(hello,reach,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,locking);const auto baseline=scene.resynchronizations();
            while(scene.motor().tick<70)scene.step({});const auto player_health=scene.presentation().read(scene.presentation().find(player.id)).health.current;
            check(apart_now(scene)<1.4&&apart_now(scene)>.9&&player_health==75&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"The guard's Light did not close on its chosen target and hit it through the shared soft lock");
            auto still=combat;for(auto& ability:still.abilities)if(ability->id==light_id){auto copy=*ability;copy.soft_lock.reset();ability=std::make_shared<const AbilityDefinition>(std::move(copy));}still.actors={guard(still)};
            CharacterSceneSession control(hello,reach,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,still);while(control.motor().tick<70)control.step({});
            check(apart_now(control)>2.2&&control.presentation().read(control.presentation().find(player.id)).health.current==100&&control.controller_receipt(target.id)&&control.controller_receipt(target.id)->committed,"Control: without targeting the guard's swing from 2.5 m must miss");
        }
        {
            // Heavy shares the same authored soft lock.
            const auto heavy_id=cooked.definition->slots[1].ability;auto heavy=std::find_if(cooked.abilities.begin(),cooked.abilities.end(),[&](const auto& ability){return ability->id==heavy_id;});
            check(heavy!=cooked.abilities.end()&&(*heavy)->soft_lock&&(*heavy)->soft_lock->range==4.5&&(*heavy)->soft_lock->stop==1.2,"Owned Royal Heavy lost its authored soft lock");
            CharacterSceneSession scene(hello,aside,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);while(scene.motor().tick<5)scene.step({});const auto baseline=scene.resynchronizations();
            CharacterSceneInput heavy_press;heavy_press.combat_events={{9,InputEdge::Pressed,1000,0,1,false}};scene.step(heavy_press);check(scene.ability()->active&&scene.ability()->ability==heavy_id&&scene.soft_lock()==target.id,"Heavy did not soft-lock the off-axis target");
            double worst{};while(scene.motor().tick<120){scene.step({});worst=std::max(worst,apart(scene.predicted_motor().position,scene.motor().position));}
            check(health(scene)<100&&worst<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Soft-locked Heavy did not hit the off-axis target, or prediction diverged");
        }
        {
            auto plain=combat;for(auto& ability:plain.abilities)if(ability->id==light_id){auto copy=*ability;copy.soft_lock.reset();ability=std::make_shared<const AbilityDefinition>(std::move(copy));}
            CharacterSceneSession scene(hello,aside,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,plain);
            while(scene.motor().tick<5)scene.step({});const auto start=scene.motor().position;scene.step(light_press);check(scene.ability()->active&&!scene.soft_lock(),"Control Light reported a soft lock");while(scene.motor().tick<65)scene.step({});
            check(health(scene)==100&&apart(scene.motor().position,start)<.6,"Control: without targeting the off-axis swing must miss and stay in place");
        }
        {
            // Movement input pointing away puts the target outside the cone: no turn, no hit.
            CharacterSceneSession scene(hello,aside,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);
            while(scene.motor().tick<5)scene.step({});auto averted=light_press;averted.x=-1;scene.step(averted);check(scene.ability()->active&&!scene.soft_lock(),"Soft lock ignored its facing cone");while(scene.motor().tick<65)scene.step({});check(health(scene)==100&&scene.pending_prediction()==0,"A target outside the cone was hit");
        }
        {
            auto distant=objects;distant[1].transform.x=4;distant[1].transform.z=4;
            CharacterSceneSession scene(hello,distant,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);
            while(scene.motor().tick<5)scene.step({});const auto start=scene.motor().position;scene.step(light_press);check(scene.ability()->active&&!scene.soft_lock(),"Soft lock ignored its range");while(scene.motor().tick<65)scene.step({});check(health(scene)==100&&apart(scene.motor().position,start)<.6,"A target out of range pulled the player");
        }
    }
    // Multi-entity combat: several server-driven actors, each with its own Health, kit, faction and controller, simulated in
    // ascending network identity. The player stays the one predicted actor.
    {
        const auto& plan=cooked.stance.plan;const auto& rig=cooked.stance.rig;const StableId first{9,2},second{9,3},third{9,4};
        auto guard=[&](StableId id,double health,std::shared_ptr<ActorController> controller){CharacterSceneActor made;made.id=id;made.kit=cooked.definition;made.input=cooked.input;made.abilities=cooked.abilities;made.attributes=combat.attributes;for(auto& attribute:made.attributes)if(attribute.id==1||attribute.id==2)attribute.base=health;made.name="Guard";made.faction="royal_guard";made.generation=std::string(64,'b');made.controller=std::move(controller);made.senses.everything=true;return made;};
        auto dummy=[&](StableId id){CharacterSceneActor made;made.id=id;made.attributes=combat.attributes;return made;};
        auto placed=[](StableId id,double x,double z,double yaw,double health=100){ObjectData made;made.id=id;made.transform.x=x;made.transform.z=z;made.transform.yaw=yaw;made.health={health,health};return made;};
        auto health_of=[](const CharacterSceneSession& scene,StableId id){return scene.presentation().read(scene.presentation().find(id)).health.current;};
        CharacterSceneInput press;press.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};CharacterSceneInput release;release.combat_events={{8,InputEdge::Released,2000,0,0,false}};
        {
            // Two guards attack on their own schedules. The player defeats the one in front; it stops acting and the one behind keeps attacking.
            auto fight=combat;fight.actors={guard(first,50,make_scripted_attacker({CombatSlot::Light,10,120,3,true})),guard(second,100,make_scripted_attacker({CombatSlot::Light,40,120,3,true}))};
            std::array<ObjectData,3> crowd{player,placed(first,0,1.2,0,50),placed(second,0,-1.3,0)};
            CharacterSceneSession scene(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,fight);const auto baseline=scene.resynchronizations();
            check(scene.actors().size()==2&&scene.actors()[0]==first&&scene.actors()[1]==second&&health_of(scene,first)==50&&health_of(scene,second)==100,"Two actors did not start from their own attributes in creation order");
            for(unsigned attack=1;attack<=2;++attack){scene.step(press);scene.step(release);while(scene.motor().tick<70*attack)scene.step({});}
            check(health_of(scene,first)==0&&health_of(scene,second)==100,"The player did not defeat the guard in front without touching the one behind");
            // A chosen target is the latest decision's: named on the tick the guard presses, absent when it does not act.
            while(scene.motor().tick<160)scene.step({});check(scene.controller_target(second)==player.id,"The attacking guard did not name the player as its target");
            while(scene.motor().tick<200)scene.step({});
            const auto* front=scene.controller_receipt(first);const auto* behind=scene.controller_receipt(second);
            check(front&&front->operation==1&&front->committed&&!scene.controller_target(first),"The defeated guard kept acting, or never attacked on its own schedule");
            check(behind&&behind->operation==2&&behind->committed&&!scene.controller_target(second),"The surviving guard did not keep attacking on its own schedule");
            check(health_of(scene,player.id)==25&&scene.pending_prediction()==0&&scene.pending_abilities()==0&&scene.resynchronizations()==baseline,"Three hits from two guards did not reach the player exactly once each, or owner prediction was disturbed");
            check(scene.pose(first)&&scene.pose(second)&&scene.pose(first)!=scene.pose(second)&&!scene.pose(third),"Each actor does not have its own observed pose");
        }
        {
            // Soft lock among several hostiles picks the nearer one, whatever its place in the actor list, and only that one is hit.
            // The list order given by the caller does not change the simulation.
            const auto wide=placed(second,1.5,1.5,3.141592653589793),near=placed(third,-1.0,1.5,3.141592653589793);std::array<ObjectData,3> pair{player,wide,near};
            auto run=[&](std::vector<CharacterSceneActor> order){auto two=combat;two.actors=std::move(order);auto scene=std::make_unique<CharacterSceneSession>(hello,pair,player.id,collision,plan,rig.definition,rig.archive,two);while(scene->motor().tick<5)scene->step({});scene->step(press);check(scene->ability()->active&&scene->soft_lock()==third,"Soft lock did not choose the nearer of two hostile actors");while(scene->motor().tick<65)scene->step({});return scene;};
            auto forward=run({dummy(second),dummy(third)}),reversed=run({dummy(third),dummy(second)});
            check(health_of(*forward,third)==75&&health_of(*forward,second)==100&&forward->pending_prediction()==0,"The soft-locked swing did not hit the nearer actor alone");
            check(forward->actors()[0]==second&&reversed->actors()[0]==second&&forward->motor().position.x==reversed->motor().position.x&&forward->motor().position.z==reversed->motor().position.z&&forward->motor().yaw==reversed->motor().yaw&&health_of(*reversed,third)==75,"Actor list order changed the simulation");
            // A friendly actor is never a soft-lock candidate.
            auto allied=combat;allied.actors={dummy(second),dummy(third)};allied.actors[1].faction="player";CharacterSceneSession scene(hello,pair,player.id,collision,plan,rig.definition,rig.archive,allied);while(scene.motor().tick<5)scene.step({});scene.step(press);check(scene.ability()->active&&scene.soft_lock()==second,"Soft lock considered an actor of the player's own faction");
        }
        {
            // Three guards with their own controllers close in from three sides and attack through the ordinary ability path.
            struct Closer final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);command.face=std::atan2(dx,dz);if(distance>1.4){command.move_x=dx/distance;command.move_z=dz/distance;}else if(!view.busy)command.press=CombatSlot::Light;return command;}};
            auto siege=combat;siege.actors={guard(first,60,std::make_shared<Closer>()),guard(second,60,std::make_shared<Closer>()),guard(third,60,std::make_shared<Closer>())};
            std::array<ObjectData,4> crowd{player,placed(first,0,6,0,60),placed(second,5.2,-3,0,60),placed(third,-5.2,-3,0,60)};
            CharacterSceneSession scene(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,siege);const auto baseline=scene.resynchronizations();
            while(scene.motor().tick<300&&health_of(scene,player.id)>0)scene.step({});
            for(auto id:{first,second,third}){const auto at=scene.observer(id,double(scene.motor().tick)).frame.motor.position;check(std::hypot(at.x-scene.motor().position.x,at.z-scene.motor().position.z)<2.5&&scene.controller_receipt(id)&&scene.controller_target(id)==player.id,"One of three guards did not close in and attack the player");}
            check(health_of(scene,player.id)<100&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Three controlled guards did not damage the player through authority, or disturbed owner prediction");
            std::cout<<"Three guards: player Health "<<health_of(scene,player.id)<<" at tick "<<scene.motor().tick<<"\n";
            // A full session: sixteen actors beside the player, four of them controlled, all replicated every tick.
            std::vector<ObjectData> many{player};auto full=combat;full.actors.clear();
            for(unsigned index=0;index<16;++index){const StableId id{9,100+index};const double angle=6.283185307179586*index/16,radius=index%4?8.:6.;many.push_back(placed(id,std::sin(angle)*radius,std::cos(angle)*radius,0,index%4?100:60));full.actors.push_back(index%4?dummy(id):guard(id,60,std::make_shared<Closer>()));if(index%4)full.actors.back().faction="royal_guard";}
            CharacterSceneSession crowd_scene(hello,many,player.id,collision,plan,rig.definition,rig.archive,full);const auto crowd_baseline=crowd_scene.resynchronizations();
            const auto packets_before=crowd_scene.server_packets();const auto kinds_before=crowd_scene.server_packet_kinds();const auto ticks_before=crowd_scene.motor().tick;
            while(crowd_scene.motor().tick<220&&health_of(crowd_scene,player.id)>0)crowd_scene.step({});
            const double packets_per_tick=double(crowd_scene.server_packets()-packets_before)/double(crowd_scene.motor().tick-ticks_before);check(packets_per_tick<16,"A full session sends more server packets per tick than the measured budget");
            check(crowd_scene.actors().size()==16&&health_of(crowd_scene,player.id)<100&&crowd_scene.pending_prediction()==0&&crowd_scene.resynchronizations()==crowd_baseline,"A full session of sixteen actors did not run, or disturbed owner prediction");
            for(auto id:crowd_scene.actors()){check(crowd_scene.pose(id)&&crowd_scene.observer(id,double(crowd_scene.motor().tick)).frame.motor.tick==crowd_scene.motor().tick,"An actor of a full session is not replicated on the current tick");if(crowd_scene.controller_status(id).empty()&&!crowd_scene.controller_receipt(id))check(health_of(crowd_scene,id)==100,"A guard's swing damaged a bystander of its own faction");}
            std::cout<<"Sixteen actors: player Health "<<health_of(crowd_scene,player.id)<<" at tick "<<crowd_scene.motor().tick<<", server packets per tick "<<packets_per_tick<<"\n";
            {const char* names[]={"","hello","content-ready","baseline","ack","ready","resync","motor-command","motor","collision","collision-ack","correction","correction-ack","ability-command","receipt","public-ability","effect-owner","effect-public","projectiles","motor-batch","public-index","public-batch"};const auto kinds=crowd_scene.server_packet_kinds();std::cout<<"Sixteen actors, server packets per tick by kind:";for(std::size_t kind=1;kind<kinds.size()&&kind<std::size(names);++kind)if(kinds[kind]>kinds_before[kind])std::cout<<" "<<names[kind]<<" "<<double(kinds[kind]-kinds_before[kind])/double(crowd_scene.motor().tick-ticks_before);std::cout<<"\n";}
        }
        {
            // Thirty-two actors: a full session. Four guards near the player close in and attack; twenty-eight stand far away. Far characters'
            // motor states travel every third tick, near ones every tick, and the whole scene stays inside the packet budget.
            struct Near final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);command.face=std::atan2(dx,dz);if(distance>1.4){command.move_x=dx/distance;command.move_z=dz/distance;}else if(!view.busy)command.press=CombatSlot::Light;return command;}};
            std::vector<ObjectData> many{player};auto full=combat;full.actors.clear();
            for(unsigned index=0;index<character_scene_actor_limit;++index){const StableId id{9,400+index};const bool near=index<4;const double angle=6.283185307179586*index/(near?4.:28.),radius=near?6.:14.5;many.push_back(placed(id,std::sin(angle)*radius,std::cos(angle)*radius,0,near?60:100));full.actors.push_back(near?guard(id,60,std::make_shared<Near>()):dummy(id));if(!near)full.actors.back().faction="royal_guard";}
            CharacterSceneSession crowd_scene(hello,many,player.id,collision,plan,rig.definition,rig.archive,full);const auto packets_before=crowd_scene.server_packets();const auto kinds_before=crowd_scene.server_packet_kinds();const auto ticks_before=crowd_scene.motor().tick;std::uint64_t stalest=0;
            while(crowd_scene.motor().tick<180&&health_of(crowd_scene,player.id)>0){crowd_scene.step({});const auto now=crowd_scene.motor().tick;for(unsigned index=0;index<character_scene_actor_limit;++index){const auto frame=crowd_scene.observer({9,400+index},double(now)).frame;if(index<4)check(frame.motor.tick==now,"A character near the player is not replicated on the current tick");else{stalest=std::max(stalest,now-frame.motor.tick);check(std::abs(std::hypot(frame.motor.position.x,frame.motor.position.z)-14.5)<.05,"A far character is not where it stands");}}}
            const double packets_per_tick=double(crowd_scene.server_packets()-packets_before)/double(crowd_scene.motor().tick-ticks_before);
            check(crowd_scene.actors().size()==32&&stalest>=1&&stalest<=2&&health_of(crowd_scene,player.id)<100&&crowd_scene.pending_prediction()==0,"A full session of thirty-two actors did not run, or far characters are not sent at the lower rate");
            {const char* names[]={"","hello","content-ready","baseline","ack","ready","resync","motor-command","motor","collision","collision-ack","correction","correction-ack","ability-command","receipt","public-ability","effect-owner","effect-public","projectiles","motor-batch","public-index","public-batch"};const auto kinds=crowd_scene.server_packet_kinds();std::cout<<"Thirty-two actors: server packets per tick "<<packets_per_tick<<", far motor states at most "<<stalest<<" ticks old, by kind:";for(std::size_t kind=1;kind<kinds.size()&&kind<std::size(names);++kind)if(kinds[kind]>kinds_before[kind])std::cout<<" "<<names[kind]<<" "<<double(kinds[kind]-kinds_before[kind])/double(crowd_scene.motor().tick-ticks_before);std::cout<<"\n";}
            check(packets_per_tick<16,"A full session of thirty-two actors sends more server packets per tick than the budget");
        }
        {
            // NPC ranged attacks: the controller only names its target and presses the ranged slot. The session aims the shot from the
            // archer's muzzle at that target, with the elevation and the projectile's drop, through the same aim the player's shot uses.
            struct Archer final:ActorController {bool name_target{true};ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;if(name_target)command.target=foe->id;command.face=std::atan2(foe->motor.position.x-view.self.position.x,foe->motor.position.z-view.self.position.z);if(!view.busy&&view.tick>=10)command.press=CombatSlot::RangedLight;return command;}};
            auto volley=[&](std::span<const ObjectData> crowd,const CollisionDefinition& ground,bool name_target,unsigned& in_flight){auto archer=std::make_shared<Archer>();archer->name_target=name_target;auto scene=combat;scene.actors={guard(first,60,archer)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,ground,plan,rig.definition,rig.archive,scene);in_flight=0;while(session->motor().tick<90){session->step({});in_flight=std::max(in_flight,unsigned(session->projectiles().size()));}check(session->controller_receipt(first)&&session->controller_receipt(first)->committed&&session->pending_prediction()==0,"The archer's shot was not an authoritative commitment");return health_of(*session,player.id);};
            unsigned seen{};std::array<ObjectData,2> level_ground{player,placed(first,0,10,3.141592653589793,60)};
            check(volley(level_ground,collision,true,seen)==80&&seen==1,"An archer 10 m away did not loose one arrow that hit the player");
            auto high=player;high.transform.y=3;auto slab=collision;slab.boxes.push_back({104,{0,2.75,0},{1.5,.25,1.5}});std::array<ObjectData,2> raised{high,placed(first,0,9,3.141592653589793,60)};
            check(volley(raised,slab,true,seen)==80,"The archer did not aim up at a target standing 3 m above it");
            check(volley(raised,slab,false,seen)==100&&seen==1,"Control: without a chosen target the shot is level and must pass under the raised player");
        }
        {
            // No friendly fire: a guard's swing at the player passes over a guard of its own faction standing in it and lands on
            // a bystander of another faction; the player's swing never lands on an ally.
            auto swing=[&](const char* faction){auto scene=combat;scene.actors={guard(first,60,make_scripted_attacker({CombatSlot::Light,10,600,3,true})),dummy(second)};scene.actors[1].faction=faction;std::array<ObjectData,3> crowd{player,placed(first,0,1.2,3.141592653589793,60),placed(second,.7,.3,0)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);while(session->motor().tick<70)session->step({});check(health_of(*session,player.id)==75,"The guard's swing did not hit the player");return health_of(*session,second);};
            check(swing("royal_guard")==100,"A guard's swing damaged a guard of its own faction");check(swing("bandit")==75,"Control: the same swing must hit a bystander of another faction");
            auto allied=combat;allied.actors={dummy(first)};allied.actors[0].faction="player";std::array<ObjectData,2> pair{player,placed(first,0,1.2,3.141592653589793)};CharacterSceneSession scene(hello,pair,player.id,collision,plan,rig.definition,rig.archive,allied);scene.step(press);scene.step(release);while(scene.motor().tick<70)scene.step({});check(health_of(scene,first)==100&&!scene.ability()->active,"The player's swing damaged an ally");
        }
        {
            // NPC sensing: an actor's controller only learns of the actors its senses detect. A watcher records what it is given and names the nearest hostile.
            static constexpr double pi=3.141592653589793;
            struct Watcher final:ActorController {std::vector<PerceivedActor> last;std::uint64_t turn_from{},turn_until{};ControllerCommand decide(const ControllerView& view)override{last.assign(view.perceived.begin(),view.perceived.end());ControllerCommand command;if(const auto* foe=nearest_hostile(view))command.target=foe->id;if(view.tick>=turn_from&&view.tick<turn_until)command.face=0;else if(turn_until&&view.tick>=turn_until)command.face=pi;return command;}};
            auto sensing=[](std::initializer_list<SensorDefinition> sensors,double forget=1){SensesDefinition made;made.forget_seconds=forget;made.sensors=sensors;return made;};
            auto notices=[&](const SensesDefinition& given,const ObjectData& self,const ObjectData& subject,const CollisionDefinition& ground){auto scene=combat;scene.actors={guard(first,60,std::make_shared<Watcher>())};scene.actors[0].senses=given;std::array<ObjectData,2> crowd{subject,self};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,ground,plan,rig.definition,rig.archive,scene);for(unsigned tick=0;tick<3;++tick)session->step({});return session->controller_target(first)==player.id;};
            const SensorDefinition near{SensorKind::Distance,4},cone{SensorKind::Vision,12,45,30},sight{SensorKind::LineOfSight,10};
            auto walled=collision;walled.boxes.push_back({105,{0,1.5,4},{3,1.5,.25}});auto close_wall=collision;close_wall.boxes.push_back({105,{0,1.5,2},{3,1.5,.25}});
            // Distance: a radius around the actor, in any direction and through walls.
            check(notices(sensing({near}),placed(first,0,3.5,0,60),player,collision)&&!notices(sensing({near}),placed(first,0,4.5,0,60),player,collision),"A distance sensor did not detect inside its radius only");
            check(notices(sensing({near}),placed(first,0,3.5,0,60),player,close_wall),"A distance sensor was blocked by a wall");
            // Vision: a cone along the actor's facing, bounded by range, both half angles and line of sight.
            check(notices(sensing({cone}),placed(first,0,8,pi,60),player,collision)&&!notices(sensing({cone}),placed(first,0,8,0,60),player,collision),"A vision cone did not see ahead only");
            check(notices(sensing({cone}),placed(first,0,8,.6-pi,60),player,collision)&&!notices(sensing({cone}),placed(first,0,8,1.-pi,60),player,collision),"A vision cone ignored its horizontal half angle");
            check(!notices(sensing({cone}),placed(first,0,13,pi,60),player,collision),"A vision cone saw beyond its range");
            {auto high=player;high.transform.y=3;auto slab=collision;slab.boxes.push_back({104,{0,2.75,0},{1.5,.25,1.5}});
                check(notices(sensing({cone}),placed(first,0,6,pi,60),high,slab)&&!notices(sensing({{SensorKind::Vision,12,45,15}}),placed(first,0,6,pi,60),high,slab),"A vision cone ignored its vertical half angle for a target standing 3 m higher");}
            check(!notices(sensing({cone}),placed(first,0,8,pi,60),player,walled),"A vision cone saw through a wall");
            // Line of sight: any direction within range, blocked by static collision.
            check(notices(sensing({sight}),placed(first,0,8,0,60),player,collision)&&!notices(sensing({sight}),placed(first,0,8,0,60),player,walled)&&!notices(sensing({sight}),placed(first,0,11,0,60),player,collision),"A line of sight sensor ignored a wall, its range, or needed the actor to face its target");
            {
                // Losing and forgetting: the watcher turns its back from tick 10, remembers the last known position for a second, forgets, and detects anew when it turns back.
                auto watcher=std::make_shared<Watcher>();watcher->turn_from=10;watcher->turn_until=100;auto scene=combat;scene.actors={guard(first,60,watcher)};scene.actors[0].senses=sensing({cone},1);std::array<ObjectData,2> crowd{player,placed(first,0,8,pi,60)};CharacterSceneSession session(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);
                while(session.motor().tick<9)session.step({});check(watcher->last.size()==1&&watcher->last[0].id==player.id&&watcher->last[0].detected&&watcher->last[0].visible&&watcher->last[0].since<=2&&session.controller_target(first)==player.id,"A watcher facing the player did not detect it from the first tick");
                while(session.motor().tick<30)session.step({});check(watcher->last.size()==1&&!watcher->last[0].detected&&!watcher->last[0].visible&&watcher->last[0].since<=2&&watcher->last[0].seen>=9&&watcher->last[0].seen<25&&std::hypot(watcher->last[0].motor.position.x,watcher->last[0].motor.position.z)<1e-6&&!session.controller_target(first),"A watcher that turned away did not keep the last known position of the player, or kept it as a target");
                const auto lost=watcher->last[0].seen;while(session.motor().tick<95)session.step({});check(watcher->last.empty()&&session.perception(first)&&session.perception(first)->actors().empty(),"A lost actor was not forgotten after the authored time");
                while(session.motor().tick<115)session.step({});check(watcher->last.size()==1&&watcher->last[0].detected&&watcher->last[0].since>=100&&session.controller_target(first)==player.id,"A watcher that turned back did not detect the player anew");
                std::cout<<"NPC sensing: player lost at tick "<<lost<<", detected again at tick "<<watcher->last[0].since<<"\n";
            }
            {
                // The awareness meter: with half a second to fill, a watcher facing the player senses it at once but detects it only after 30 ticks;
                // one that turns away after 15 ticks never detects it, and the meter drains to nothing.
                auto aware=sensing({cone});aware.awareness_seconds=.5;
                auto run=[&](std::uint64_t turn){auto watcher=std::make_shared<Watcher>();if(turn){watcher->turn_from=turn;watcher->turn_until=100000;}auto scene=combat;scene.actors={guard(first,60,watcher)};scene.actors[0].senses=aware;std::array<ObjectData,2> crowd{player,placed(first,0,8,pi,60)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);return std::pair{std::move(session),watcher};};
                {auto [session,watcher]=run(0);while(session->motor().tick<20)session->step({});check(watcher->last.size()==1&&watcher->last[0].sensed&&!watcher->last[0].detected&&watcher->last[0].awareness>.5&&watcher->last[0].awareness<.8&&!session->controller_target(first),"A filling awareness meter detected the player early, or did not fill");
                    while(session->motor().tick<34)session->step({});check(watcher->last.size()==1&&watcher->last[0].detected&&watcher->last[0].awareness==1&&watcher->last[0].since>=29&&watcher->last[0].since<=32&&session->controller_target(first)==player.id,"A full awareness meter did not detect the player after the authored time");}
                {auto [session,watcher]=run(15);bool detected=false;double peak=0;while(session->motor().tick<70){session->step({});for(const auto& other:watcher->last){detected|=other.detected;peak=std::max(peak,other.awareness);}}check(!detected&&peak>.3&&peak<1&&watcher->last.empty(),"A watcher that looked away before its meter filled detected the player, or the meter did not drain");}
            }
            {
                // An actor without senses perceives nothing: a controller that would walk up and attack stays where it is.
                struct Hunter final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);command.face=std::atan2(dx,dz);if(distance>1.4){command.move_x=dx/distance;command.move_z=dz/distance;}else if(!view.busy)command.press=CombatSlot::Light;return command;}};
                auto hunt=[&](bool sees){auto scene=combat;scene.actors={guard(first,60,std::make_shared<Hunter>())};scene.actors[0].senses={};scene.actors[0].senses.everything=sees;std::array<ObjectData,2> crowd{player,placed(first,0,5,pi,60)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);while(session->motor().tick<240)session->step({});
                    const auto at=session->observer(first,double(session->motor().tick)).frame.motor.position;const bool idle=std::abs(at.z-5)<.01&&std::abs(at.x)<.01&&!session->controller_receipt(first)&&!session->controller_target(first)&&session->perception(first)->actors().empty()&&health_of(*session,player.id)==100;return std::pair{idle,health_of(*session,player.id)};};
                check(hunt(false).first,"An actor without senses moved, chose a target or attacked");check(hunt(true).second<100,"Control: the same actor with senses must walk up and hit the player");
            }
            {
                // Sensing order is the session's actor order, not the order the actors were listed in.
                auto run=[&](bool reversed){auto scene=combat;auto ally=dummy(third);ally.faction="royal_guard";scene.actors={guard(first,60,std::make_shared<Watcher>()),guard(second,60,std::make_shared<Watcher>()),ally};if(reversed)std::reverse(scene.actors.begin(),scene.actors.end());
                    std::array<ObjectData,4> crowd{player,placed(first,0,6,pi,60),placed(second,3,6,pi,60),placed(third,-3,5,0)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);while(session->motor().tick<20)session->step({});
                    std::string record;for(auto id:session->actors()){record+=id.text()+":";for(const auto& other:session->perception(id)->actors())record+=other.id.text()+(other.detected?"+":"-")+(other.visible?"v":"h")+std::to_string(other.since)+"/"+std::to_string(other.seen)+",";record+=";";}
                    const auto seen=session->perception(first)->actors();check(seen.size()==3&&seen[0].id==player.id&&seen[1].id==second&&seen[2].id==third&&session->controller_target(first)==player.id&&session->controller_target(second)==player.id,"An actor that sees everything did not perceive the others in ascending network identity, or did not choose the hostile player");return record;};
                check(run(false)==run(true),"Perception depended on the order the actors were listed in");
            }
            {
                // NPC ranged aim needs line of sight: the same archer looses an arrow across open ground and never presses with a wall in the way.
                struct Bowman final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;command.face=std::atan2(foe->motor.position.x-view.self.position.x,foe->motor.position.z-view.self.position.z);if(!view.busy&&view.tick>=10)command.press=CombatSlot::RangedLight;return command;}};
                struct Volley {double health{};unsigned flown{};bool pressed{};};
                auto shoot=[&](const CollisionDefinition& ground){auto scene=combat;scene.actors={guard(first,60,std::make_shared<Bowman>())};std::array<ObjectData,2> crowd{player,placed(first,0,8,pi,60)};auto session=std::make_unique<CharacterSceneSession>(hello,crowd,player.id,ground,plan,rig.definition,rig.archive,scene);Volley result;while(session->motor().tick<90){session->step({});result.flown=std::max(result.flown,unsigned(session->projectiles().size()));}result.health=health_of(*session,player.id);result.pressed=session->controller_receipt(first)!=nullptr;return result;};
                const auto open=shoot(collision),blocked=shoot(walled);check(open.health==80&&open.flown==1&&open.pressed,"An archer with a clear line of sight did not shoot the player");check(blocked.health==100&&blocked.flown==0&&!blocked.pressed,"An archer shot at a target behind a wall");
            }
            std::cout<<"NPC sensing: distance radius, vision cone (facing, half angles, range, height), line of sight, a wall, losing and forgetting, the awareness meter, no senses, actor order, archer line of sight\n";
        }
        {
            // NPC ranged aim leads a moving target: the player runs across the archer's line, the arrow is loosed ahead of it and lands.
            struct Leader final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;command.face=std::atan2(foe->motor.position.x-view.self.position.x,foe->motor.position.z-view.self.position.z);if(!view.busy&&view.tick>=10)command.press=CombatSlot::RangedLight;return command;}};
            auto scene=combat;scene.actors={guard(first,60,std::make_shared<Leader>())};std::array<ObjectData,2> crowd{player,placed(first,0,10,3.141592653589793,60)};CharacterSceneSession session(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);
            CharacterSceneInput run;run.x=1;double lead=0,crossed=0;bool flown=false;
            while(session.motor().tick<120&&health_of(session,player.id)==100){session.step(run);
                if(!flown&&!session.projectiles().empty()){flown=true;const auto archer=session.observer(first,double(session.motor().tick)).frame.motor;const auto& target=session.motor().position;lead=std::remainder(archer.yaw-std::atan2(target.x-archer.position.x,target.z-archer.position.z),6.283185307179586);crossed=target.x;}}
            const auto struck=session.motor().position.x;
            // Facing -z, a target moving toward +x is led by turning to a smaller yaw.
            check(flown&&lead<-.03,"The archer did not aim ahead of a target running across its line");
            check(health_of(session,player.id)==80&&struck-crossed>.5,"The led arrow did not hit the running player, or the player had not moved during its flight");
            std::cout<<"NPC aim lead: "<<-lead<<" rad ahead at launch, player ran "<<struck-crossed<<" m during the flight and was hit\n";
        }
        {
            // Encounters: enemies enter a running session. The scene starts with the player alone; a world event starts a wave encounter
            // whose enemies appear at its spawn points, are replicated, and die to the player's ordinary attacks; the next wave follows.
            const AssetId weak=AssetId::random();CharacterSceneInput swing;swing.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};CharacterSceneInput let_go;let_go.combat_events={{8,InputEdge::Released,2000,0,0,false}};
            auto enemy=[&](double health,const char* faction){auto made=guard({},health,nullptr);made.name="Raider";made.faction=faction;made.senses={};made.senses.everything=true;return made;};
            auto staged=[&](const char* json,double health){CharacterSceneEncounter made;made.id={9,50};std::string text(json);for(auto at=text.find("NPC");at!=text.npos;at=text.find("NPC"))text.replace(at,3,weak.text());made.definition=decode_encounter(text);made.npcs.emplace(weak,enemy(health,"raiders"));return made;};
            std::array<ObjectData,1> alone{player};
            {
                auto scene=combat;scene.actors.clear();auto encounter=staged(R"({"trigger":{"event":"test.go"},"mode":"waves","waves":[{"npc":"NPC","count":2},{"npc":"NPC","count":1}],"placement":"points","emits":{"started":"test.started","wave_cleared":"test.wave_cleared","completed":"test.done"},"tag":"test.active"})",25);
                encounter.points={{{-.45,0,1.2},3.141592653589793},{{.45,0,1.2},3.141592653589793}};scene.encounters={encounter};
                CharacterSceneSession session(hello,alone,player.id,collision,plan,rig.definition,rig.archive,scene);const auto baseline=session.resynchronizations();
                while(session.motor().tick<20)session.step({});check(session.actors().empty()&&session.encounter({9,50})->phase==EncounterPhase::Waiting&&session.world_events().log().size()==1&&session.world_events().log()[0].name=="play.started","An encounter spawned before its event, or the session did not emit play.started");
                session.emit_world_event("test.go");session.step({});session.step({});
                check(session.actors().size()==2&&session.encounter({9,50})->phase==EncounterPhase::Running&&session.encounter({9,50})->alive==2&&session.world_events().tag("test.active"),"A world event did not start the encounter's first wave");
                for(auto id:session.actors()){const auto frame=session.observer(id,double(session.motor().tick)).frame;check(session.pose(id)&&frame.motor.tick==session.motor().tick&&std::abs(frame.motor.position.z-1.2)<.05&&std::abs(std::abs(frame.motor.position.x)-.45)<.05&&health_of(session,id)==25&&session.actor_encounter(id)==StableId{9,50}&&session.actor(id)&&session.actor(id)->name=="Raider","An enemy that entered during play is not replicated at its spawn point on the current tick");}
                check(session.actors()[0]==encounter_actor({9,50},0)&&session.actors()[1]==encounter_actor({9,50},1),"Encounter enemies do not have identities derived from the encounter");
                // The player's ordinary attacks kill the wave; the second wave enters only then.
                auto fight=[&](std::size_t until_actors,EncounterPhase until_phase){for(unsigned round=0;round<8&&!(session.actors().size()>=until_actors&&session.encounter({9,50})->phase==until_phase&&(until_phase==EncounterPhase::Complete||session.encounter({9,50})->wave==2));++round){session.step(swing);session.step(let_go);for(unsigned tick=0;tick<58;++tick)session.step({});}};
                fight(3,EncounterPhase::Running);check(session.actors().size()==3&&health_of(session,session.actors()[0])==0&&health_of(session,session.actors()[1])==0&&health_of(session,session.actors()[2])==25&&session.encounter({9,50})->wave==2,"The second wave did not enter when the player had killed the first");
                fight(3,EncounterPhase::Complete);check(session.encounter({9,50})->phase==EncounterPhase::Complete&&health_of(session,session.actors()[2])==0&&!session.world_events().tag("test.active"),"The encounter did not complete when the player killed its last wave");
                session.step({});std::string heard;for(const auto& event:session.world_events().log())heard+=event.name+" ";check(heard=="play.started test.go test.started test.wave_cleared test.done ","The encounter did not emit its world events in order");
                check(session.pending_prediction()==0&&health_of(session,player.id)==100,"Enemies entering during play disturbed owner prediction or hurt the player without a controller");
                std::cout<<"Encounter: two waves entered and were killed by tick "<<session.motor().tick<<", resynchronisations "<<session.resynchronizations()-baseline<<", events: "<<heard<<"\n";
            }
            {
                // A trigger volume the player walks into emits the event; enemies with a controller from the host's factory appear at random
                // points inside the encounter's volume, sense the player and attack through the ordinary paths. Two runs are identical.
                struct Raider final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;command.target=foe->id;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);command.face=std::atan2(dx,dz);if(distance>1.4){command.move_x=dx/distance;command.move_z=dz/distance;}else if(!view.busy)command.press=CombatSlot::Light;return command;}};
                auto run=[&]{auto scene=combat;scene.actors.clear();auto encounter=staged(R"({"trigger":{"event":"gate.entered"},"mode":"timed","timed":{"npc":"NPC","interval_seconds":0.5,"max_alive":2,"total":3},"placement":"random"})",60);encounter.volumes={{{0,1,9},0,{4,2,2}}};encounter.controller=[](AssetId){return std::make_shared<Raider>();};scene.encounters={encounter};
                    CharacterSceneTrigger gate;gate.id={9,60};gate.definition=decode_trigger(R"({"event":"gate.entered","half":[3,2,1]})");gate.center={0,1,3};scene.triggers={gate};
                    auto session=std::make_unique<CharacterSceneSession>(hello,alone,player.id,collision,plan,rig.definition,rig.archive,scene);CharacterSceneInput forward;forward.z=1;
                    while(session->motor().tick<30)session->step({});check(session->actors().empty()&&session->trigger_fired({9,60})==0u,"A trigger volume fired before the player reached it");
                    while(session->motor().tick<200&&session->actors().empty())session->step(forward);check(session->trigger_fired({9,60})==1u&&session->actors().size()==1&&session->motor().position.z>1.9&&session->motor().position.z<3.2,"Walking into the trigger volume did not start the encounter");
                    const auto first=session->observer(session->actors()[0],double(session->motor().tick)).frame.motor.position;check(encounter_contains(encounter.volumes,{first.x,1,first.z})&&std::abs(first.y)<.05,"A random spawn is outside the encounter's volume or off the floor");
                    std::string record;std::size_t most=0;while(session->motor().tick<420&&health_of(*session,player.id)>0){session->step({});most=std::max(most,std::size_t(session->encounter({9,50})->alive));}
                    check(session->actors().size()==2&&most==2&&session->encounter({9,50})->spawned==2,"A timed encounter did not hold to its cap on enemies alive");check(health_of(*session,player.id)<100&&session->controller_target(session->actors()[0])==player.id,"Encounter enemies with controllers did not sense and attack the player");
                    for(auto id:session->actors()){const auto at=session->observer(id,double(session->motor().tick)).frame.motor.position;record+=id.text()+":"+std::to_string(at.x)+","+std::to_string(at.z)+";";}return record+std::to_string(health_of(*session,player.id))+"@"+std::to_string(session->motor().tick);};
                const auto once=run();check(once==run(),"Two runs of the same encounter differ");std::cout<<"Encounter: trigger volume, timed random spawns: "<<once<<"\n";
            }
            {
                // A scene whose spawn points and encounters ask for more actors than a session holds is refused when the session is built.
                auto scene=combat;scene.actors.clear();std::vector<ObjectData> pair{player};for(unsigned index=0;index<17;++index){const StableId id{9,300+index};scene.actors.push_back(dummy(id));pair.push_back(placed(id,double(index)-8,5,0));}auto encounter=staged(R"({"trigger":{"event":"test.go"},"mode":"waves","waves":[{"npc":"NPC","count":16}],"placement":"points"})",25);encounter.points={{{0,0,3},0}};scene.encounters={encounter};
                bool refused=false;try{CharacterSceneSession session(hello,pair,player.id,collision,plan,rig.definition,rig.archive,scene);}catch(const std::exception&){refused=true;}check(refused,"A scene asking for thirty-three actors was accepted");
            }
            {
                // Dead enemies disappear: six waves of three is eighteen enemies, more than a session holds at once. Each wave's dead are removed
                // after half a second, so the scene never holds more than a few and is empty at the end.
                auto scene=combat;scene.actors.clear();auto encounter=staged(R"({"trigger":{"event":"test.go"},"mode":"waves","waves":[{"npc":"NPC","count":3},{"npc":"NPC","count":3},{"npc":"NPC","count":3},{"npc":"NPC","count":3},{"npc":"NPC","count":3},{"npc":"NPC","count":3}],"placement":"points","corpse_seconds":0.5})",25);
                encounter.points={{{-1.5,0,3},3.141592653589793},{{0,0,3.5},3.141592653589793},{{1.5,0,3},3.141592653589793}};struct Crowder final:ActorController {ControllerCommand decide(const ControllerView& view)override{ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);command.face=std::atan2(dx,dz);if(distance>1.1){command.move_x=dx/distance;command.move_z=dz/distance;}return command;}};encounter.controller=[](AssetId){return std::make_shared<Crowder>();};scene.encounters={encounter};
                CharacterSceneSession session(hello,alone,player.id,collision,plan,rig.definition,rig.archive,scene);session.emit_world_event("test.go");std::size_t most=0;
                for(unsigned round=0;round<120&&session.encounter({9,50})->phase!=EncounterPhase::Complete;++round){
                    // The player turns to the nearest living enemy before each swing.
                    double yaw=session.motor().yaw,nearest=1e9;for(auto id:session.actors())if(health_of(session,id)>0){const auto at=session.observer(id,double(session.motor().tick)).frame.motor.position;const double dx=at.x-session.motor().position.x,dz=at.z-session.motor().position.z,distance=std::hypot(dx,dz);if(distance<nearest){nearest=distance;yaw=std::atan2(dx,dz);}}
                    auto aimed=swing;aimed.yaw=yaw;auto released=let_go;released.yaw=yaw;CharacterSceneInput idle;idle.yaw=yaw;session.step(aimed);session.step(released);for(unsigned tick=0;tick<58;++tick){session.step(idle);most=std::max(most,session.actors().size());}}
                check(session.encounter({9,50})->phase==EncounterPhase::Complete&&session.encounter({9,50})->spawned==18&&most<=7,"An encounter of eighteen enemies did not run to its end with its dead removed");
                for(unsigned tick=0;tick<40;++tick)session.step({});check(session.actors().empty()&&!session.presentation().valid(session.presentation().find(encounter_actor({9,50},17)))&&session.pending_prediction()==0,"The last dead enemies were not removed from the session and the client's world");
                std::cout<<"Encounter: eighteen enemies in six waves, at most "<<most<<" in the session at once, done by tick "<<session.motor().tick<<"\n";
            }
            {
                // Time slicing. Spawning: a wave of six enters two a tick, and counts as alive from the start. Deciding: twelve controlled actors
                // with a budget of four decide four a tick, each every third tick, and keep walking between their turns.
                {auto scene=combat;scene.actors.clear();scene.spawn_budget=2;auto encounter=staged(R"({"trigger":{"event":"test.go"},"mode":"waves","waves":[{"npc":"NPC","count":6},{"npc":"NPC","count":1}],"placement":"random"})",25);encounter.volumes={{{0,1,8},0,{5,2,3}}};scene.encounters={encounter};
                    CharacterSceneSession session(hello,alone,player.id,collision,plan,rig.definition,rig.archive,scene);session.emit_world_event("test.go");session.step({});check(session.actors().size()==2,"The first two enemies of a wave did not enter on the tick its event was delivered");
                    std::vector<std::size_t> grown;for(unsigned tick=0;tick<5;++tick){session.step({});grown.push_back(session.actors().size());check(session.encounter({9,50})->alive==6&&session.encounter({9,50})->wave==1,"Enemies waiting to enter were not counted as alive, or the next wave came early");}
                    check(grown==std::vector<std::size_t>{4,6,6,6,6},"A wave did not enter two enemies a tick under a spawn budget of two");}
                struct Walker final:ActorController {std::map<std::uint64_t,unsigned>* per_tick{};std::vector<std::uint64_t> turns;ControllerCommand decide(const ControllerView& view)override{++(*per_tick)[view.tick];turns.push_back(view.tick);ControllerCommand command;const auto* foe=view.alive?nearest_hostile(view):nullptr;if(!foe)return command;const double dx=foe->motor.position.x-view.self.position.x,dz=foe->motor.position.z-view.self.position.z,distance=std::hypot(dx,dz);if(distance>2){command.move_x=dx/distance;command.move_z=dz/distance;command.face=std::atan2(dx,dz);}return command;}};
                auto march=[&](unsigned budget){std::map<std::uint64_t,unsigned> per_tick;std::vector<std::shared_ptr<Walker>> walkers;auto scene=combat;scene.actors.clear();scene.decision_budget=budget;scene.sense_budget=budget;std::vector<ObjectData> crowd{player};
                    for(unsigned index=0;index<12;++index){const StableId id{9,200+index};auto walker=std::make_shared<Walker>();walker->per_tick=&per_tick;walkers.push_back(walker);scene.actors.push_back(guard(id,60,walker));const double angle=6.283185307179586*index/12;crowd.push_back(placed(id,std::sin(angle)*12,std::cos(angle)*12,0,60));}
                    CharacterSceneSession session(hello,crowd,player.id,collision,plan,rig.definition,rig.archive,scene);while(session.motor().tick<60)session.step({});
                    unsigned most=0;for(const auto& [tick,count]:per_tick)most=std::max(most,count);std::uint64_t widest=0;for(const auto& walker:walkers)for(std::size_t turn=1;turn<walker->turns.size();++turn)widest=std::max(widest,walker->turns[turn]-walker->turns[turn-1]);
                    double walked=0;for(unsigned index=0;index<12;++index){const auto at=session.observer({9,200+index},60).frame.motor.position;walked+=12-std::hypot(at.x,at.z);}
                    return std::tuple{most,widest,walked/12};};
                const auto [all_most,all_widest,all_walked]=march(12);const auto [most,widest,walked]=march(4);
                check(all_most==12&&all_widest==1,"With budget for everyone, not every controller decided every tick");
                check(most==4&&widest==3,"A decision budget of four did not give twelve controllers four turns a tick, each every third tick");
                check(all_walked>2&&walked>all_walked*.85,"Actors did not keep walking between their turns to decide");
                std::cout<<"Time slicing: twelve controllers, budget 4: "<<most<<" decisions a tick, a turn every "<<widest<<" ticks, walked "<<walked<<" m against "<<all_walked<<" m with a turn every tick\n";
            }
        }
        {
            // Bounds: an actor needs a scene object and resolved attributes, identities are unique, and the list is bounded.
            auto reject=[&](CharacterSceneCombat bad,const char* message){bool rejected=false;try{CharacterSceneSession scene(hello,objects,player.id,collision,plan,rig.definition,rig.archive,bad);}catch(const std::exception&){rejected=true;}check(rejected,message);};
            auto missing=combat;missing.actors={dummy(third)};reject(missing,"An actor without a scene object was accepted");
            auto twice=combat;twice.actors={dummy(first),dummy(first)};reject(twice,"A duplicate actor identity was accepted");
            auto bare=combat;bare.actors.front().attributes.clear();reject(bare,"An actor without attributes was accepted");
            auto self=combat;self.actors={dummy(player.id)};reject(self,"The player was accepted as a server-driven actor");
            auto many=combat;many.actors.assign(character_scene_actor_limit+1,dummy(first));reject(many,"More actors than the limit were accepted");
            // A combat scene may have no other actor at all.
            auto alone=combat;alone.actors.clear();std::array<ObjectData,1> solo{player};CharacterSceneSession scene(hello,solo,player.id,collision,plan,rig.definition,rig.archive,alone);scene.step(press);check(scene.ability()->active&&!scene.soft_lock()&&scene.actors().empty(),"A combat scene without other actors did not run");
        }
    }
    auto tag_registry=std::make_shared<const TagDictionary>(std::vector<TagDefinition>{{2,0,"State.Blocked",AttributeVisibility::Public}},AssetId::random(),std::string(64,'e'));auto tagged=*combat.abilities[0];tagged.tag_registry=tag_registry->registry();tagged.tag_generation=tag_registry->generation();tagged.requirements.none={2};combat.abilities={std::make_shared<const AbilityDefinition>(std::move(tagged))};combat.tags=tag_registry;auto tagged_kit=std::make_shared<CombatKitDefinition>(*combat.kit);for(auto& slot:tagged_kit->slots)if(slot.ability!=combat.abilities[0]->id)slot.ability={};tagged_kit->effects.clear();tagged_kit->effect_bindings.clear();combat.kit=std::move(tagged_kit);CharacterSceneSession tagged_scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);tagged_scene.step(pressed);check(tagged_scene.predicted_ability()->active&&tagged_scene.predicted_ability()->tags.registry==tag_registry->registry()&&tagged_scene.observer(player.id,1).frame.ability.tags.registry==tag_registry->registry(),"Scene prepares the same frozen tag registry for native authority, owner prediction and observer frames");
    std::cout<<"Cooked Royal combat: serialized intent, action/root/pose, Jolt target damage/Health, exact owner correction, prepared tags, NPC target with its own kit/attributes and the controller seam (move, face, press, refusal), multi-entity combat (two and three controlled actors, own schedules, defeat of one, nearest soft lock, list-order independence, no friendly fire, NPC ranged attack aimed at its chosen target, a full session of sixteen actors, bounds), aimed ranged Bow Shot (range, aim direction, static blocking), vertical aim (elevation in motor input and state, crosshair convergence from the muzzle), travelling projectile (replicated flight, gravity, delayed single hit, instant control), melee soft lock (turn, closing warp, cone/range gates, hit) shared by Heavy and by NPC attacks on their chosen target, scripted target attacker (owner damage, flinch, interruption, dodge invulnerability, death) and authoring isolation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
