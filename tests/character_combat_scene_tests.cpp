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
    CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();combat.target={9,2};combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    ObjectData player;player.id={9,1};ObjectData target;target.id=combat.target;target.transform.z=1.2;target.transform.yaw=3.141592653589793;std::array<ObjectData,2> objects{player,target};World authoring(WorldDomain::Authoring);for(auto object:objects)authoring.create(object);auto original=authoring.serialize();CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}}};SessionHandshake hello{3,sha256("character combat scene wire3"),sha256(cooked.definition->generation),703};
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
        auto attacked=combat;attacked.attacker=CharacterSceneAttacker{CombatSlot::Light,10,120,3,true};
        auto away=objects;away[1].transform.yaw=0;std::uint64_t hit_tick{};
        // The session bootstrap itself counts as one prediction baseline; nothing afterwards may add another.
        unsigned baseline{};
        {
            CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,attacked);
            while(scene.motor().tick<9)scene.step({});check(!scene.attacker_receipt()&&!scene.observer(target.id,9).frame.ability.active&&player_health(scene)==100,"Scripted attacker acted before its first tick");
            auto idle=*scene.pose(target.id);baseline=scene.resynchronizations();scene.step({});
            check(scene.attacker_receipt()&&scene.attacker_receipt()->failure==AbilityFailure::None&&scene.attacker_receipt()->committed,"Scripted attacker request was not committed by authority");
            auto seen=scene.observer(target.id,10);check(bool(seen.frame.ability.active),"Target attack is not public state");
            check(std::abs(std::remainder(seen.frame.motor.yaw-3.141592653589793,6.283185307179586))<.1,"Scripted attacker did not turn to the player on activation");
            while(scene.motor().tick<70){scene.step({});if(!hit_tick&&player_health(scene)<100)hit_tick=scene.motor().tick;if(scene.motor().tick==30)check(!same(*scene.pose(target.id),idle),"Target attack is not presented from its public action");}
            check(hit_tick>10&&hit_tick<45&&player_health(scene)==75&&attribute(scene,2)==75,"Target hit did not damage the player once through authority and the owner correction");
            check(scene.pending_prediction()==0&&scene.resynchronizations()==baseline&&!scene.ability()->active&&scene.pending_abilities()==0&&target_health(scene)==100,"Incoming damage disturbed owner prediction or the attacker");
            while(scene.motor().tick<200)scene.step({});check(player_health(scene)==50&&scene.attacker_receipt()->operation==2,"Second scheduled attack did not land exactly once");
        }
        {auto far=away;far[1].transform.z=6;CharacterSceneSession scene(hello,far,player.id,collision,plan,rig.definition,rig.archive,attacked);while(scene.motor().tick<80)scene.step({});check(!scene.attacker_receipt()&&player_health(scene)==100,"Scripted attacker ignored its range");}
        {auto blind=attacked;blind.attacker->face_player=false;CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,blind);while(scene.motor().tick<80)scene.step({});check(scene.attacker_receipt()&&scene.attacker_receipt()->failure==AbilityFailure::None&&player_health(scene)==100,"A swing facing away must miss: hits come from the authoritative query only");}
        {auto invalid=attacked;invalid.attacker->slot=CombatSlot::Parry;bool rejected=false;try{CharacterSceneSession scene(hello,away,player.id,collision,plan,rig.definition,rig.archive,invalid);}catch(const std::exception&){rejected=true;}check(rejected,"Scripted attacker accepted an unassigned slot");}

        // Royal game rules. The kit binds the owned Flinch to the Light hit by default: owner reaction, interruption, stagger, invulnerability and death.
        std::ifstream flinch_source(content/"royal_district/combat/flinch.daeffect");const auto flinch_id=AssetId::parse(nlohmann::json::parse(flinch_source).at("asset").get<std::string>());
        auto bound_flinch=std::find_if(cooked.effects.begin(),cooked.effects.end(),[&](const auto& effect){return effect->id==flinch_id;});check(bound_flinch!=cooked.effects.end(),"Royal kit no longer binds the owned Flinch");auto flinch=*bound_flinch;
        check(flinch->reaction&&flinch->interrupt_action&&flinch->lock_movement&&!flinch->reaction_loop,"Owned Royal flinch lost its reaction, interruption or movement lock");
        auto royal=combat;royal.effects=cooked.effects;for(const auto& effect:royal.effects)if(effect->reaction&&!royal.clips.contains(effect->reaction->motion->clip.id)){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),effect->reaction->motion->clip.id);royal.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
        royal.damage={};ashen_roots::configure_royal_combat(royal);
        royal.death=flinch->reaction;royal.attacker=CharacterSceneAttacker{CombatSlot::Light,10,60,3,true};
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
            const auto last=scene.attacker_receipt()->tick;CharacterSceneInput pressed_light;pressed_light.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};scene.step(pressed_light);const auto corpse=scene.motor().position;walk.jump=true;while(scene.motor().tick<400)scene.step(walk);
            check(apart(scene.motor().position,corpse)<1e-6&&std::abs(scene.motor().position.y-corpse.y)<1e-3&&apart(scene.predicted_motor().position,corpse)<1e-6,"Dead player walked or jumped");
            check(scene.attacker_receipt()->tick==last&&!scene.ability()->active&&!scene.predicted_ability()->active&&scene.pending_abilities()==0&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline&&target_health(scene)==100,"Dead player acted, or the attacker kept attacking a dead player");
            check(same(scene.pose(),oracle.sample(reaction_clip,*scene.death(player.id))),"Death pose is not held");
        }
        // The owned Stun (unbound by default) on the Heavy hit: stun loop on the owner pose and a movement lock for the whole status.
        {
            assets.cook("royal_district/combat/stun.daeffect");std::ifstream stun_source(content/"royal_district/combat/stun.daeffect");const auto stun_id=AssetId::parse(nlohmann::json::parse(stun_source).at("asset").get<std::string>());assets.package(stun_id,cache/"stun.json");
            auto stun=load_cooked_effect(cache/"stun.json",assets.cas_path(),stun_id).definition;check(stun->reaction&&stun->reaction_loop&&stun->lock_movement&&stun->duration_ticks==90,"Owned Royal stun lost its loop, lock or duration");
            auto stunned=royal;stunned.death={};stunned.effects.push_back(stun);auto clip=load_cooked_clip(cache/"stun.json",assets.cas_path(),stun->reaction->motion->clip.id);stunned.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));
            const auto heavy_ability=cooked.definition->slots[1].ability;auto bound=std::make_shared<CombatKitDefinition>(*cooked.definition);bound->effects.push_back(stun->id);bound->effect_bindings.push_back({heavy_ability,2,stun->id,1});stunned.kit=bound;stunned.damage={};stunned.combat_damage={};stunned.effect_evaluators.clear();ashen_roots::configure_royal_combat(stunned);
            stunned.attacker=CharacterSceneAttacker{CombatSlot::Heavy,10,600,3,true};CharacterSceneSession scene(hello,objects,player.id,collision,plan,rig.definition,rig.archive,stunned);
            while(scene.motor().tick<70&&player_health(scene)==100)scene.step({});const auto stun_tick=scene.motor().tick;check(stun_tick<70,"Target Heavy did not hit the player");
            CharacterSceneInput walk;walk.z=-1;walk.jump=true;const auto held=scene.motor().position;while(scene.motor().tick<stun_tick+60)scene.step(walk);
            auto shown=scene.reaction(player.id);check(shown&&shown->effect==stun->id&&shown->suppressed==ReactionSuppression::None&&shown->tick==double(scene.motor().tick-shown->start)&&same(scene.pose(),oracle.sample(*stunned.clips.at(stun->reaction->motion->clip.id),shown->tick)),"Owner pose is not the stun loop on the effect clock");
            check(std::hypot(scene.motor().position.x-held.x,scene.motor().position.z-held.z)<1e-6&&std::hypot(scene.predicted_motor().position.x-held.x,scene.predicted_motor().position.z-held.z)<1e-6&&scene.pending_prediction()==0&&scene.resynchronizations()==baseline,"Stunned player moved or prediction disagreed");
            while(scene.motor().tick<stun_tick+100)scene.step(walk);check(scene.motor().position.z<held.z-.2&&!scene.reaction(player.id),"Stun did not end with its status");
        }
        // Dodge invulnerability against a real incoming hit. A wall behind the player stops the dash, so only the tag window differs.
        auto walled=collision;walled.boxes.push_back({101,{0,1,-1.2},{4,1,.5}});royal.death={};royal.attacker=CharacterSceneAttacker{CombatSlot::Light,60,600,3,true};const auto dodge_hit=hit_tick+50;
        auto dodge_run=[&](std::uint64_t press){
            auto scene=std::make_unique<CharacterSceneSession>(hello,objects,player.id,walled,plan,rig.definition,rig.archive,royal);
            CharacterSceneInput dodge;dodge.z=-1;dodge.combat_events={{6,InputEdge::Pressed,1000,0,1,false}};
            while(scene->motor().tick<press-1)scene->step({});scene->step(dodge);while(scene->motor().tick<dodge_hit+20)scene->step({});
            check(attribute(*scene,3)==85&&scene->attacker_receipt()&&scene->attacker_receipt()->failure==AbilityFailure::None&&scene->pending_prediction()==0&&scene->resynchronizations()==baseline,"Dodge or the scripted attack did not run through authority");return scene;};
        auto late=dodge_run(dodge_hit-14),early=dodge_run(dodge_hit-45);
        check(std::abs(late->motor().position.z-early->motor().position.z)<.05&&std::abs(late->motor().position.x-early->motor().position.x)<.05,"Dodge control and invulnerable runs ended in different places");
        check(player_health(*early)==75&&early->reaction(player.id),"Control: a hit after the invulnerability window must damage and stagger the player");
        check(player_health(*late)==100&&!late->reaction(player.id),"A hit inside the dodge invulnerability window damaged or staggered the player");
        while(late->motor().tick<dodge_hit+60)late->step({});check(player_health(*late)==100,"The avoided hit window damaged the player later");
        check(authoring.serialize()==original,"Scripted attacker changed the authoring world");
    }
    auto tag_registry=std::make_shared<const TagDictionary>(std::vector<TagDefinition>{{2,0,"State.Blocked",AttributeVisibility::Public}},AssetId::random(),std::string(64,'e'));auto tagged=*combat.abilities[0];tagged.tag_registry=tag_registry->registry();tagged.tag_generation=tag_registry->generation();tagged.requirements.none={2};combat.abilities={std::make_shared<const AbilityDefinition>(std::move(tagged))};combat.tags=tag_registry;auto tagged_kit=std::make_shared<CombatKitDefinition>(*combat.kit);for(auto& slot:tagged_kit->slots)if(slot.ability!=combat.abilities[0]->id)slot.ability={};tagged_kit->effects.clear();tagged_kit->effect_bindings.clear();combat.kit=std::move(tagged_kit);CharacterSceneSession tagged_scene(hello,objects,player.id,collision,cooked.stance.plan,cooked.stance.rig.definition,cooked.stance.rig.archive,combat);tagged_scene.step(pressed);check(tagged_scene.predicted_ability()->active&&tagged_scene.predicted_ability()->tags.registry==tag_registry->registry()&&tagged_scene.observer(player.id,1).frame.ability.tags.registry==tag_registry->registry(),"Scene prepares the same frozen tag registry for native authority, owner prediction and observer frames");
    std::cout<<"Cooked Royal combat: serialized intent, action/root/pose, Jolt target damage/Health, exact owner correction, prepared tags, scripted target attacker (owner damage, flinch, interruption, dodge invulnerability, death) and authoring isolation passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
