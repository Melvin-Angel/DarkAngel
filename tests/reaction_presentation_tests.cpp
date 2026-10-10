#include <darkangel/character_presentation.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/animation_assets.hpp>
#include <darkangel/hash.hpp>
#include <ashen_roots/royal_combat.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed,"Expected reaction rejection");}
bool same(const std::vector<JointMatrix>& a,const std::vector<JointMatrix>& b){if(a.size()!=b.size())return false;for(unsigned joint=0;joint<a.size();++joint)for(unsigned i=0;i<16;++i)if(std::abs(a[joint].values[i]-b[joint].values[i])>2e-5)return false;return true;}
// No melee contact: this fixture applies statuses through the server API only.
class NoContact:public MeleeQuery {public:bool matches(std::uint64_t,const MotorState&)const override{return true;}MeleeCandidates query(std::uint64_t,const MotorState&,const AbilityMelee&)const override{return {};}};
// The public replica shape the server publishes, built from authoritative state.
EffectFrame published(const WorldSession& server,AbilityOwnerHandle owner,std::uint64_t tick,const std::vector<std::shared_ptr<const EffectDefinition>>& definitions){
 EffectFrame frame{owner,1,1,tick,tick+1,AttributeVisibility::Public,{}};
 for(const auto& effect:server.ability_effects(owner))for(const auto& definition:definitions)if(definition->id==effect.definition&&definition->visibility==AttributeVisibility::Public)frame.effects.push_back({effect.handle.value,effect.definition,effect.generation,{effect.credit.session_epoch,effect.credit.source_network,effect.credit.activation},effect.start,effect.end,effect.next_period,effect.suppressed});
 return frame;
}
}
int main(){try{
 std::ifstream input(std::filesystem::path(DAE_BINARY_DIR)/"authoring-fixture.json");auto fixture=nlohmann::json::parse(input);auto registry=fixture.at("reaction_registry").get<std::string>();auto cas=std::filesystem::path(fixture.at("cache").get<std::string>())/"cas";
 auto cooked=load_cooked_combat_kit(registry,cas,AssetId::parse(fixture.at("kit").get<std::string>()));auto flinch_id=AssetId::parse(fixture.at("reaction_effect").get<std::string>()),burn_id=AssetId::parse(fixture.at("burn_effect").get<std::string>());
 auto effect=[&](AssetId id){auto found=std::find_if(cooked.effects.begin(),cooked.effects.end(),[&](const auto& value){return value->id==id;});check(found!=cooked.effects.end(),"Fixture effect missing from frozen kit");return *found;};
 auto flinch=effect(flinch_id),burn=effect(burn_id);check(flinch->reaction&&flinch->reaction->id==AssetId::parse(fixture.at("reaction_action").get<std::string>())&&flinch->interrupt_action&&!burn->reaction,"Frozen kit lost the authored reaction binding");
 const auto& rig=cooked.stance.rig;auto tags=cooked.tags->dictionary();auto schema=cooked.attributes.definitions();const auto length=flinch->reaction->duration/action_tick_units;
 std::map<AssetId,std::shared_ptr<const AnimationClip>> clips;std::vector<std::shared_ptr<const ActionDefinition>> actions;
 auto prepare_clip=[&](const ActionDefinition& action){if(clips.contains(action.motion->clip.id))return;auto clip=load_cooked_clip(registry,cas,action.motion->clip.id);clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));};
 for(const auto& ability:cooked.abilities){actions.push_back(ability->action);prepare_clip(*ability->action);}prepare_clip(*flinch->reaction);
 RigPose oracle(rig.definition,rig.archive);const auto& reaction_clip=*clips.at(flinch->reaction->motion->clip.id);

 // Frozen definition policy: presentation-only, public, one-shot, no gameplay blocks or root.
 {
  AttributeSet attributes(schema);auto variant=[&](auto change){auto copy=*flinch;auto action=*flinch->reaction;change(copy,action);copy.reaction=std::make_shared<const ActionDefinition>(action);return copy;};
  freeze_effect_definition(*flinch,attributes,*tags);
  rejects([&]{freeze_effect_definition(variant([](auto& e,auto&){e.visibility=AttributeVisibility::Owner;}),attributes,*tags);});
  rejects([&]{freeze_effect_definition(variant([](auto&,auto& a){a.motion->motor_root=true;}),attributes,*tags);});
  rejects([&]{freeze_effect_definition(variant([](auto&,auto& a){a.loops=2;}),attributes,*tags);});
  rejects([&]{freeze_effect_definition(variant([](auto&,auto& a){a.upper_body=true;}),attributes,*tags);});
  rejects([&]{freeze_effect_definition(variant([](auto&,auto& a){a.motion.reset();}),attributes,*tags);});
  rejects([&]{freeze_effect_definition(variant([](auto&,auto& a){a.blocks.push_back({90,7,0,action_tick_units,ActionBlockKind::HitWindow,"Weapon.Right"});}),attributes,*tags);});
  std::array<std::shared_ptr<const EffectDefinition>,1> one{flinch};
  rejects([&]{ReactionPresentation missing(rig.definition,one,{});});
  auto foreign=rig.definition;foreign.signature=sha256("foreign rig");rejects([&]{ReactionPresentation mismatch(foreign,one,clips);});
  auto stale=std::make_shared<EffectDefinition>(*flinch);auto stale_action=*flinch->reaction;stale_action.motion->archive_generation=sha256("stale clip archive");stale->reaction=std::make_shared<const ActionDefinition>(stale_action);std::array<std::shared_ptr<const EffectDefinition>,1> stale_one{stale};rejects([&]{ReactionPresentation mismatch(rig.definition,stale_one,clips);});
  std::array<std::shared_ptr<const EffectDefinition>,1> none{burn};check(ReactionPresentation(rig.definition,none,clips).empty(),"Effect without a binding prepared a reaction");
 }

 // Authority: the effect, never its presentation, interrupts through the existing WorldSession path.
 {
  WorldSession server{SessionRole::Server,{3,sha256("Reaction authority protocol"),sha256(cooked.definition->generation),77}};
  ObjectData object;object.id={61,1};auto attacker=server.configure_abilities(server.create(object),schema,2,1);object.id={61,2};object.transform.z=1.2;auto victim=server.configure_abilities(server.create(object),schema,2,1);
  server.bind_melee_query(std::make_shared<NoContact>());server.register_combat_evaluator(1,[](const DamageContext&){return CombatEvaluation{};});
  for(auto owner:{attacker,victim}){server.configure_ability_tags(owner,tags);server.equip_combat_kit(owner,cooked.definition,cooked.input,cooked.abilities);}
  server.register_effect_evaluator(2,[](const EffectContext& context){return std::vector<ResourceDelta>{{2,-context.credit.power}};});
  std::uint64_t tick{},operation{1};auto step=[&]{++tick;server.advance_abilities(tick);server.drain_ability_actions();server.drain_effect_outcomes();MotorState motor;motor.tick=tick;motor.sequence=tick;server.publish_motor(attacker.network,motor);motor.position.z=1.2;server.publish_motor(victim.network,motor);server.resolve_ability_hits(tick);server.drain_ability_commitments();};
  const auto heavy_input=cooked.definition->slots[1].input_action;auto request=[&](AbilityOwnerHandle owner){return AbilityRequest{owner,operation++,server.ability_snapshot(owner).grant_generation,CombatSlot::Heavy,{heavy_input,InputEdge::Pressed,1000,0,1,false},false};};
  auto alive=[&](AbilityOwnerHandle owner){for(const auto& value:server.ability_snapshot(owner).attributes)if(value.id==2)return value.value>0;return false;};
  auto has=[&](AbilityOwnerHandle owner,TagId tag){auto values=server.ability_tags(owner,AttributeVisibility::Public);return std::find(values.begin(),values.end(),tag)!=values.end();};
  auto passive=std::make_shared<EffectDefinition>(*flinch);passive->id=AssetId::random();passive->generation=sha256("Non-interrupting reaction status");passive->interrupt_action=false;
  auto lingering=std::make_shared<EffectDefinition>(*flinch);lingering->id=AssetId::random();lingering->generation=sha256("Reaction status retained on death");lingering->remove_on_death=false;lingering->lifetime=EffectLifetime::UntilRemoved;lingering->duration_ticks=0;
  std::vector<std::shared_ptr<const EffectDefinition>> definitions{flinch,burn,passive,lingering};ReactionPresentation reactions(rig.definition,definitions,clips);
  auto select=[&](AbilityOwnerHandle owner){auto frame=published(server,owner,tick,definitions);return reactions.select(&frame,owner.network,77,1,tick,alive(owner),bool(server.ability_snapshot(owner).active));};
  check(server.request_ability(request(attacker)).failure==AbilityFailure::None&&server.request_ability(request(victim)).failure==AbilityFailure::None,"Fixture attacks did not start");server.drain_ability_actions();step();step();step();
  check(server.ability_snapshot(victim).active&&server.ability_snapshot(attacker).active&&!select(victim),"Fixture attacks are not active or a reaction preceded any effect");
  // A status without authored interruption: presentation waits; it cannot cancel the action.
  auto waiting=server.apply_effect(victim,attacker,*passive,0);auto candidate=select(victim);
  check(server.ability_snapshot(victim).active&&candidate&&candidate->effect==passive->id&&candidate->suppressed==ReactionSuppression::ActiveAction&&candidate->tick==0,"Non-interrupting status cancelled the action or was not reported as waiting");
  server.remove_effect(victim,waiting);check(!select(victim)&&server.ability_snapshot(victim).active,"Removing the status left a reaction or cancelled the action");
  auto before=server.ability_snapshot(attacker);auto applied=server.apply_effect(victim,attacker,*flinch,0);auto updates=server.drain_ability_actions();const auto onset=tick;
  check(!server.ability_snapshot(victim).active&&std::any_of(updates.begin(),updates.end(),[&](const auto& update){return update.handle.owner==victim&&update.phase==ActionPhase::Cancelled;}),"Authoritative effect did not interrupt the victim's attack");
  check(server.ability_snapshot(attacker).active==before.active&&server.ability_snapshot(attacker).action->clock==before.action->clock,"Effect on one actor disturbed another actor's action");
  auto shown=select(victim);check(shown&&shown->effect==flinch_id&&shown->key.handle==applied.value&&shown->suppressed==ReactionSuppression::None&&shown->tick==0&&shown->start==onset&&!select(attacker),"Interrupted actor does not present its reaction from the effect onset");
  check(server.can_activate(request(victim))==AbilityFailure::TagRequirements&&has(victim,6),"Status gameplay gate is not owned by the effect");
  // Burn from the same source coexists; the reaction instance stays selected.
  auto aura=server.apply_effect(victim,attacker,*burn,1);for(unsigned n=0;n<10;++n)step();shown=select(victim);
  check(shown&&shown->key.handle==applied.value&&shown->tick==10&&has(victim,3)&&server.ability_effects(victim).size()==2,"Coexisting Burn displaced or restarted the reaction");
  // An ordinary refresh keeps the instance handle and onset: no replay.
  auto refreshed=server.apply_effect(victim,attacker,*flinch,0);shown=select(victim);
  check(refreshed==applied&&shown&&shown->key.handle==applied.value&&shown->start==onset&&shown->tick==10,"Refresh restarted the one-shot reaction");
  auto frame=published(server,victim,tick,definitions);auto refreshed_end=std::find_if(frame.effects.begin(),frame.effects.end(),[&](const auto& value){return value.handle==applied.value;})->end;check(refreshed_end==tick+flinch->duration_ticks,"Refresh did not extend the authoritative status");
  while(tick<onset+length)step();
  check(!select(victim)&&has(victim,6)&&server.ability_effects(victim).size()==2&&server.can_activate(request(victim))==AbilityFailure::TagRequirements,"Finishing the animation removed the status or its gameplay gate");
  // A late refresh after the one-shot finished still does not replay it.
  server.apply_effect(victim,attacker,*flinch,0);check(!select(victim),"Late refresh replayed a finished one-shot");
  // Early removal ends presentation and keeps the unrelated Burn source.
  auto second=server.apply_effect(attacker,victim,*flinch,0);server.apply_effect(attacker,victim,*burn,1);for(unsigned n=0;n<5;++n)step();
  shown=select(attacker);check(shown&&shown->tick==5&&!server.ability_snapshot(attacker).active,"Second actor did not present its own reaction clock");
  server.remove_effect(attacker,second);check(!select(attacker)&&!has(attacker,6)&&has(attacker,3)&&server.ability_effects(attacker).size()==1,"Early removal kept the reaction or cleared the unrelated Burn");
  server.remove_effect(victim,aura);check(has(victim,6)&&!has(victim,3),"Removing Burn cleared the reaction status source");
  // Health 0 suppresses the flinch; no death animation is claimed.
  server.apply_effect(attacker,victim,*lingering,0);check(select(attacker)&&select(attacker)->suppressed==ReactionSuppression::None,"Retained status fixture did not present");
  server.apply_effect(attacker,victim,*burn,1000);for(unsigned n=0;n<burn->period_ticks&&alive(attacker);++n)step();
  auto dead=select(attacker);check(!alive(attacker)&&dead&&dead->effect==lingering->id&&dead->suppressed==ReactionSuppression::Dead,"Health 0 did not suppress the retained status reaction");
 }

 // Selection policy on checked public replicas.
 {
  auto strong=std::make_shared<EffectDefinition>(*flinch);strong->id=AssetId::random();strong->generation=sha256("Higher priority reaction status");auto strong_action=*flinch->reaction;strong_action.priority=flinch->reaction->priority+50;strong->reaction=std::make_shared<const ActionDefinition>(strong_action);
  std::vector<std::shared_ptr<const EffectDefinition>> definitions{flinch,strong,burn};ReactionPresentation reactions(rig.definition,definitions,clips);
  AbilityOwnerHandle owner;owner.network=9;owner.session_epoch=77;EffectAttribution credit{77,4,1};
  auto replica=[&](std::uint64_t handle,const EffectDefinition& definition,std::uint64_t start){return EffectReplica{handle,definition.id,definition.generation,credit,start,start+200,0,false};};
  EffectFrame frame{owner,1,3,100,5,AttributeVisibility::Public,{replica(1,*strong,80),replica(2,*flinch,95)}};
  auto pick=[&](std::uint64_t tick=100,bool alive=true,bool acting=false){return reactions.select(&frame,9,77,3,tick,alive,acting);};
  check(pick()&&pick()->key.handle==1&&pick()->tick==20,"Higher timeline priority did not win over a later onset");
  frame.effects={replica(1,*flinch,80),replica(2,*flinch,95)};check(pick()->key.handle==2&&pick()->tick==5,"Equal priority did not prefer the latest onset (retrigger by a new instance)");
  frame.effects={replica(4,*flinch,95),replica(7,*flinch,95),replica(5,*flinch,95)};check(pick()->key.handle==7,"Equal onset did not prefer the highest effect handle");
  auto first=pick();check(pick()->key==first->key&&pick()->tick==first->tick,"Repeated selection of one frame changed the reaction clock");
  frame.effects={replica(1,*flinch,80),replica(2,*flinch,95)};frame.effects[1].suppressed=true;check(pick()->key.handle==1,"Suppressed effect instance presented its reaction");
  frame.effects={replica(2,*flinch,95)};check(pick(99)==std::nullopt||pick(99)->tick==4,"Pose clock before frame tick is sampled from onset");check(!pick(94),"Reaction shown before its onset on the pose clock");
  check(!pick(95+length)&&pick(95+length-1)&&pick(95+length-1)->tick==double(length-1),"Finished one-shot kept presenting or ended early");
  frame.effects[0].end=110;check(pick(109)&&!pick(110),"Expired effect kept presenting while awaiting its removal frame");frame.effects[0].end=295;
  check(pick(100,false)->suppressed==ReactionSuppression::Dead&&pick(100,true,true)->suppressed==ReactionSuppression::ActiveAction&&pick(100,false,true)->suppressed==ReactionSuppression::Dead,"Terminal and active-action suppression precedence");
  check(!reactions.select(&frame,9,77,4,100,true,false)&&!reactions.select(&frame,9,77,2,100,true,false)&&!reactions.select(nullptr,9,77,3,100,true,false),"Effect frame from another avatar lifecycle presented a reaction");
  rejects([&]{reactions.select(&frame,10,77,3,100,true,false);});rejects([&]{reactions.select(&frame,9,78,3,100,true,false);});
  auto owner_only=frame;owner_only.audience=AttributeVisibility::Owner;rejects([&]{reactions.select(&owner_only,9,77,3,100,true,false);});
  auto stale=frame;stale.effects[0].generation=sha256("stale effect generation");rejects([&]{reactions.select(&stale,9,77,3,100,true,false);});
  frame.effects={replica(3,*burn,95)};check(!pick(),"Effect without a reaction binding presented one");
 }

 // Replicated public frames through the shared session: late join, duplicates and fences.
 {
  CharacterSceneCombat combat;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=schema;combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;combat.tags=tags;combat.effects=cooked.effects;combat.target={31,2};combat.evaluator=1;combat.clips=clips;ashen_roots::configure_royal_combat(combat);
  ObjectData player;player.id={31,1};ObjectData target;target.id=combat.target;target.transform.z=1.2;target.transform.yaw=3.141592653589793;std::array<ObjectData,2> objects{player,target};CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{16,.5,16}}};SessionHandshake hello{3,sha256("Reaction native pose fixture"),sha256(cooked.definition->generation),1711};
  CharacterSceneSession session(hello,objects,player.id,collision,cooked.stance.plan,rig.definition,rig.archive,combat);
  CharacterSceneInput press;press.combat_events={{cooked.definition->slots[1].input_action,InputEdge::Pressed,1000,0,1,false}};session.step(press);
  while(!session.reaction(target.id)&&session.motor().tick<40)session.step({});check(session.reaction(target.id)!=nullptr,"Replicated hit did not present the target reaction");
  for(unsigned n=0;n<12;++n)session.step({});const auto tick=session.motor().tick;auto live=*session.reaction(target.id);const auto onset=live.start;
  auto frame=session.observer(target.id,double(tick)).frame;auto effects=*session.effects(target.id)->current();check(live.tick==double(tick-onset)&&live.tick>=12&&same(*session.pose(target.id),oracle.sample(reaction_clip,live.tick)),"Live target reaction clock/pose mismatch");
  auto join=[&]{return std::make_unique<ObservedCharacterPose>(cooked.stance.plan,rig.definition,rig.archive,actions,clips,tags,cooked.effects);};
  // Late join inside the window samples the current instance clock, never tick 0.
  auto joined=join();joined->sample(frame,&effects);check(joined->reaction()&&joined->reaction()->key==live.key&&joined->reaction()->tick==live.tick&&same(joined->matrices(),oracle.sample(reaction_clip,live.tick)),"Late join replayed the onset instead of the current reaction clock");
  auto state=joined->graph_state();auto matrices=joined->matrices();joined->sample(frame,&effects);check(joined->graph_state().tick==state.tick&&joined->graph_state().phase==state.phase&&joined->reaction()->tick==live.tick&&same(joined->matrices(),matrices),"Duplicate frame advanced or restarted the reaction");
  auto locomotion=[&](const ObservedCharacterPose& pose){return same(pose.matrices(),oracle.blend(pose.inputs().span()));};
  // Stale/foreign/unknown effect state never presents and never corrupts the retained pose.
  auto foreign=effects;foreign.owner.network+=10;rejects([&]{joined->sample(frame,&foreign);});auto unknown=effects;unknown.effects[0].generation=sha256("stale replicated generation");for(auto& replica:unknown.effects)replica.generation=sha256("stale replicated generation");rejects([&]{joined->sample(frame,&unknown);});
  check(joined->graph_state().tick==state.tick&&joined->reaction()&&same(joined->matrices(),matrices),"Rejected effect frame changed the retained reaction pose");
  auto reused=effects;reused.avatar_epoch+=1;auto other=join();other->sample(frame,&reused);check(!other->reaction()&&locomotion(*other),"Effect frame from a previous avatar lifecycle presented a reaction");
  auto without=join();without->sample(frame);check(!without->reaction()&&locomotion(*without),"Aggregate public tags alone invented a reaction");
  // Late join after the window: status still current, no historical flinch.
  auto later=frame;later.motor.tick=later.ability.tick=onset+length+3;auto expired=join();expired->sample(later,&effects);check(!expired->reaction()&&locomotion(*expired)&&std::find(later.ability.tags.values.begin(),later.ability.tags.values.end(),6)!=later.ability.tags.values.end(),"Late join after the window replayed a finished reaction");
  auto corpse=frame;for(auto& attribute:corpse.ability.attributes)if(attribute.id==corpse.ability.health_attribute)attribute.value=0;auto fallen=join();fallen->sample(corpse,&effects);check(fallen->reaction()&&fallen->reaction()->suppressed==ReactionSuppression::Dead&&locomotion(*fallen),"Health 0 presented a flinch");
  // An actor's own remaining public action keeps precedence; presentation does not cancel it.
  CharacterSceneSession second(SessionHandshake{3,sha256("Reaction native pose fixture"),sha256(cooked.definition->generation),1712},objects,player.id,collision,cooked.stance.plan,rig.definition,rig.archive,combat);second.step(press);second.step({});
  auto acting=second.observer(player.id,2).frame;check(acting.ability.active&&acting.ability.action,"Missing active public action fixture");auto own=effects;own.owner=acting.ability.owner;own.avatar_epoch=acting.motor.epoch;own.tick=2;own.effects.resize(1);own.effects[0].definition=flinch->id;own.effects[0].generation=flinch->generation;own.effects[0].credit.session_epoch=acting.session_epoch;own.effects[0].start=1;own.effects[0].end=91;own.effects[0].next_period=0;own.effects[0].suppressed=false;
  auto busy=join();busy->sample(acting,&own);auto heavy=std::find_if(actions.begin(),actions.end(),[&](const auto& action){return action->id==acting.ability.action_definition;});
  check(busy->reaction()&&busy->reaction()->suppressed==ReactionSuppression::ActiveAction&&heavy!=actions.end()&&!same(busy->matrices(),oracle.sample(reaction_clip,1)),"Reaction replaced a remaining active public action");
  while(session.motor().tick<onset+length)session.step({});check(!session.reaction(target.id)&&same(*session.pose(target.id),oracle.blend(session.graph_inputs(target.id)->span()))&&session.pending_prediction()==0,"Completed reaction did not resolve current locomotion");
 }
 std::cout<<"Effect reactions: frozen policy/rig/clip rejection, authoritative interruption with unrelated actor retained, non-interrupting wait, Burn coexistence, refresh without replay, completion with status retained, early removal with unrelated source retained, Health0 suppression, priority/onset/handle order, expiry, avatar/actor/audience/generation fences, current-clock late join, expired late join, duplicate stability and active-action precedence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
