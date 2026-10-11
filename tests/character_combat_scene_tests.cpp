#include <darkangel/character_scene.hpp>
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
    auto arm=[](CharacterSceneCombat& scene,ScriptedAttack script){auto& actor=scene.actors.front();if(actor.name.empty()){actor.kit=scene.kit;actor.input=scene.input;actor.abilities=scene.abilities;}actor.controller=make_scripted_attacker(script);};
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
        CharacterSceneActor guard;guard.id=target.id;guard.kit=guard_kit;guard.input=cooked.input;for(const auto& ability:cooked.abilities)if(ability->id!=heavy_id)guard.abilities.push_back(ability);guard.attributes=combat.attributes;for(auto& attribute:guard.attributes)if(attribute.id==1||attribute.id==2)attribute.base=60;guard.name="Test Guard";guard.faction="royal_guard";guard.generation=std::string(64,'a');
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
        auto shoot=[&](std::span<const ObjectData> placed,const CollisionDefinition& world,double yaw){
            auto scene=std::make_unique<CharacterSceneSession>(hello,placed,player.id,world,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);CharacterSceneInput idle;idle.yaw=yaw;
            while(scene->motor().tick<5)scene->step(idle);const auto baseline=scene->resynchronizations();auto press=idle;press.combat_events={{14,InputEdge::Pressed,1000,0,1,false}};scene->step(press);
            check(scene->ability()->active&&scene->ability()->ability==bow_id&&scene->predicted_ability()->active&&!scene->soft_lock(),"Bow Shot did not start through authority and prediction");
            while(scene->motor().tick<75)scene->step(idle);check(stamina_of(*scene)==90&&!scene->ability()->active&&scene->pending_prediction()==0&&scene->resynchronizations()==baseline,"Bow Shot did not charge once and settle");return scene;};
        auto distant=objects;distant[1].transform.z=8;
        check(health(*shoot(distant,collision,0))==80,"An aimed shot did not hit a target 8 m straight ahead exactly once");
        check(health(*shoot(distant,collision,.5))==100,"A shot aimed away from the target hit it");
        auto beyond=objects;beyond[0].transform.z=-9;beyond[1].transform.z=14;check(health(*shoot(beyond,collision,0))==100,"A shot hit a target beyond its range");
        auto walled=collision;walled.boxes.push_back({102,{0,1,4},{2,1,.2}});check(health(*shoot(distant,walled,0))==100,"A shot passed through static collision");
        check(health(*shoot(objects,collision,0))==80,"A shot did not hit a target at melee distance");
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
        auto guard=[&](StableId id,double health,std::shared_ptr<ActorController> controller){CharacterSceneActor made;made.id=id;made.kit=cooked.definition;made.input=cooked.input;made.abilities=cooked.abilities;made.attributes=combat.attributes;for(auto& attribute:made.attributes)if(attribute.id==1||attribute.id==2)attribute.base=health;made.name="Guard";made.faction="royal_guard";made.generation=std::string(64,'b');made.controller=std::move(controller);return made;};
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
    std::cout<<"Cooked Royal combat: serialized intent, action/root/pose, Jolt target damage/Health, exact owner correction, prepared tags, NPC target with its own kit/attributes and the controller seam (move, face, press, refusal), multi-entity combat (two and three controlled actors, own schedules, defeat of one, nearest soft lock, list-order independence, bounds), aimed ranged Bow Shot (range, aim direction, static blocking), melee soft lock (turn, closing warp, cone/range gates, hit), scripted target attacker (owner damage, flinch, interruption, dodge invulnerability, death) and authoring isolation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
