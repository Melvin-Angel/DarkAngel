#include <darkangel/world_session.hpp>
#include <algorithm>
#include <iostream>
#include <limits>
using namespace darkangel;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"Expected atomic combat effect rejection");}
class SyntheticQuery:public MeleeQuery {std::uint64_t source_,target_;public:SyntheticQuery(std::uint64_t source,std::uint64_t target):source_(source),target_(target){}bool matches(std::uint64_t id,const MotorState&)const override{return id==source_||id==target_;}MeleeCandidates query(std::uint64_t id,const MotorState&,const AbilityMelee&)const override{return {{{id==source_?target_:source_,1}},false};}};
struct Fixture {
 WorldSession server{SessionRole::Server,{3,std::string(64,'a'),std::string(64,'b'),77}};AbilityOwnerHandle source,target;std::uint64_t tick_{};bool attacker_alive{true};
#ifdef DAE_COMBAT_EFFECT_PHYSICS
 PhysicsWorld physics;std::unique_ptr<CharacterMotor> attacker,victim;
#endif
 std::vector<AttributeDefinition> schema{{1,"MaxHealth",AttributeKind::Statistic,100,0,1000,0},{2,"Health",AttributeKind::Resource,100,0,1000,1},{3,"Stamina",AttributeKind::Resource,100,0,100,0}};
 std::shared_ptr<const TagDictionary> tags=std::make_shared<const TagDictionary>(std::vector<TagDefinition>{{1,0,"Status",AttributeVisibility::Public},{2,1,"Status.Burn",AttributeVisibility::Public},{3,1,"Status.Stagger",AttributeVisibility::Public}},AssetId::random(),std::string(64,'e'));
 std::shared_ptr<ActionDefinition> action=std::make_shared<ActionDefinition>();std::shared_ptr<AbilityDefinition> ability=std::make_shared<AbilityDefinition>();std::shared_ptr<CombatKitDefinition> kit=std::make_shared<CombatKitDefinition>();InputProfile input;std::shared_ptr<EffectDefinition> burn=std::make_shared<EffectDefinition>();CombatEvaluation evaluation;bool invalid_execution{};
 Fixture(){ObjectData object;object.id={51,1};source=server.configure_abilities(server.create(object),schema,2,1);object.id={51,2};object.transform.z=1.2;target=server.configure_abilities(server.create(object),schema,2,1);server.configure_ability_tags(source,tags);server.configure_ability_tags(target,tags);
#ifdef DAE_COMBAT_EFFECT_PHYSICS
 physics.add({100,{0,-.5,0},{10,.5,10}});attacker=std::make_unique<CharacterMotor>(physics,MotorVec{0,0,0},source.network);victim=std::make_unique<CharacterMotor>(physics,MotorVec{0,0,1.2},target.network);server.bind_melee_query(std::make_shared<PhysicsMeleeQuery>(physics));
#else
 server.bind_melee_query(std::make_shared<SyntheticQuery>(source.network,target.network));
#endif

  server.register_combat_evaluator(1,[this](const DamageContext& context){check(context.source.owner==source||context.source.owner==target,"Checked combat source credit");return evaluation;});server.register_effect_evaluator(2,[this](const EffectContext& context){return std::vector<ResourceDelta>{{2,invalid_execution?std::numeric_limits<double>::quiet_NaN():-context.credit.power}};});burn->id=AssetId::random();burn->generation=std::string(64,'f');burn->duration_ticks=5;burn->period_ticks=2;burn->evaluator=2;burn->stacking=EffectStack::RefreshPerSource;burn->tags={2};burn->visibility=AttributeVisibility::Public;evaluation={30,{{burn,10}}};
  action->id=AssetId::random();action->generation=std::string(64,'c');action->duration=4*action_tick_units;action->loops=1;action->blocks={{1,0,action_tick_units/2,3*action_tick_units,ActionBlockKind::HitWindow,"Weapon"}};ability->id=AssetId::random();ability->generation=std::string(64,'d');ability->action=action;ability->costs={{3,10}};ability->melee={{1,{0,1,.9},.7,30,1,1}};input.id=AssetId::random();kit->id=AssetId::random();kit->generation="combat-effects";kit->locomotion_stance=AssetId::random();for(unsigned i=0;i<combat_slot_count;++i){input.actions.push_back({i+1,"combat."+std::to_string(i),InputActionKind::Button});kit->slots[i]={static_cast<CombatSlot>(i),i+1,{}};}kit->slots[0].ability=ability->id;std::array<std::shared_ptr<const AbilityDefinition>,1> catalogue{ability};server.equip_combat_kit(source,kit,input,catalogue);server.equip_combat_kit(target,kit,input,catalogue);
 }
 AbilityReceipt activate(AbilityOwnerHandle owner,std::uint64_t operation=1){return server.request_ability({owner,operation,server.ability_snapshot(owner).grant_generation,CombatSlot::Light,{1,InputEdge::Pressed,operation*1000,0,1,false}});}
 void step(){auto tick=++tick_;server.advance_abilities(tick);server.drain_ability_actions();
#ifdef DAE_COMBAT_EFFECT_PHYSICS
 if(attacker)attacker->step({tick,tick,attacker->state().epoch});victim->step({tick,tick,victim->state().epoch,0,0,3.141592653589793});physics.step();if(attacker)attacker->post_physics();victim->post_physics();physics.finish_tick();physics.take_sensor_events();if(attacker)server.publish_motor(source.network,attacker->state());server.publish_motor(target.network,victim->state());
#else
 MotorState motor;motor.tick=tick;motor.sequence=tick;if(attacker_alive)server.publish_motor(source.network,motor);motor.position.z=1.2;motor.yaw=3.141592653589793;server.publish_motor(target.network,motor);
#endif
 }
 void retire_source(){attacker_alive=false;
#ifdef DAE_COMBAT_EFFECT_PHYSICS
 attacker.reset();
#endif
 server.destroy(source.network);}
 std::vector<DamageResult> resolve(){return server.resolve_ability_hits(tick_);}
 double health(){return server.objects().at(target.network).health.current;}
};
void damage_status_and_credit(){Fixture f;auto activation=f.activate(f.source).handle;f.step();auto result=f.resolve();auto effects=f.server.ability_effects(f.target);check(result.size()==1&&result[0].applied==30&&f.health()==70&&effects.size()==1&&effects[0].credit.activation==activation.activation&&effects[0].credit.source_network==f.source.network&&effects[0].credit.power==10&&f.server.ability_tags(f.target)==std::vector<TagId>{2},"Jolt-validated damage and attributed Burn publish together");check(f.resolve().empty()&&f.server.ability_effects(f.target).size()==1,"Duplicate resolution cannot apply another status");f.retire_source();for(unsigned tick=2;tick<=6;++tick){f.step();f.resolve();f.server.drain_effect_outcomes();}check(f.health()==50&&f.server.ability_effects(f.target).empty(),"On-hit Burn retains captured credit after source destruction and excludes execution at expiry");}
void all_or_nothing(){Fixture f;f.activate(f.source);f.step();auto invalid=std::make_shared<EffectDefinition>(*f.burn);invalid->id=AssetId::random();invalid->tags={99};f.evaluation.effects.push_back({invalid,5});rejects([&]{f.resolve();});check(f.health()==100&&f.server.ability_effects(f.target).empty()&&f.server.drain_effect_outcomes().empty(),"Later invalid status rolls back earlier prepared damage/status and ledger");f.evaluation.effects.resize(1);check(f.resolve().size()==1&&f.health()==70&&f.server.ability_effects(f.target).size()==1,"Corrected retry commits the retained hit once");
 Fixture execution;auto instant=std::make_shared<EffectDefinition>(*execution.burn);instant->lifetime=EffectLifetime::Instant;instant->stacking=EffectStack::Independent;instant->duration_ticks=instant->period_ticks=0;instant->tags.clear();instant->execute_on_apply=true;execution.evaluation.effects={{instant,5}};execution.activate(execution.source);execution.step();execution.invalid_execution=true;rejects([&]{execution.resolve();});check(execution.health()==100&&execution.server.drain_effect_outcomes().empty(),"Nonfinite instant evaluator rolls back direct damage");execution.invalid_execution=false;auto result=execution.resolve();auto outcomes=execution.server.drain_effect_outcomes();check(result.size()==1&&result[0].applied==30&&result[0].after==65&&outcomes.size()==1&&outcomes[0].execution.applied.front().delta==-5,"Direct damage and instant effect outcome publish coherently");
 Fixture lethal;lethal.evaluation.damage=100;lethal.activate(lethal.source);lethal.step();auto killed=lethal.resolve();check(killed.size()==1&&killed[0].killed&&lethal.server.ability_effects(lethal.target).empty(),"On-hit status requests do not resurrect or apply to a killed target");}
void interruption_and_queue_bounds(){Fixture f;auto stagger=std::make_shared<EffectDefinition>(*f.burn);stagger->id=AssetId::random();stagger->period_ticks=0;stagger->evaluator=0;stagger->tags={3};stagger->interrupt_action=true;f.evaluation.effects={{stagger,0}};f.activate(f.source);f.activate(f.target);f.server.drain_ability_actions();f.step();auto result=f.resolve();auto updates=f.server.drain_ability_actions();check(result.size()==1&&!f.server.ability_snapshot(f.target).active&&std::any_of(updates.begin(),updates.end(),[&](const auto& update){return update.handle.owner==f.target&&update.phase==ActionPhase::Cancelled;}),"Atomic stagger interrupts the target and invalidates its later pending hit");
 Fixture full;auto instant=std::make_shared<EffectDefinition>(*full.burn);instant->lifetime=EffectLifetime::Instant;instant->stacking=EffectStack::Independent;instant->duration_ticks=instant->period_ticks=0;instant->tags.clear();instant->execute_on_apply=true;for(unsigned i=0;i<128;++i)full.server.apply_effect(full.target,full.source,*instant,0);full.evaluation.effects={{instant,5}};full.activate(full.source);full.step();rejects([&]{full.resolve();});check(full.health()==100&&full.server.drain_effect_outcomes().size()==128,"Effect outcome queue exhaustion rolls back hit damage and deduplication");check(full.resolve().size()==1&&full.health()==65,"Drained queue permits one exact hit retry");}
void global_periodic_reservation(){Fixture f;ObjectData object;object.id={51,3};auto third=f.server.configure_abilities(f.server.create(object),f.schema,2,1);auto periodic=std::make_shared<EffectDefinition>(*f.burn);periodic->id=AssetId::random();periodic->tags.clear();periodic->lifetime=EffectLifetime::UntilRemoved;periodic->duration_ticks=0;periodic->period_ticks=60;periodic->stacking=EffectStack::Independent;for(unsigned i=0;i<63;++i){f.server.apply_effect(f.source,f.source,*periodic,0);f.server.apply_effect(f.target,f.source,*periodic,0);}f.server.apply_effect(third,f.source,*periodic,0);auto released=f.server.apply_effect(third,f.source,*periodic,0);f.activate(f.source);f.step();rejects([&]{f.resolve();});check(f.health()==100&&f.server.ability_effects(f.target).size()==63,"On-hit status cannot bypass the global 128-periodic reservation");f.server.remove_effect(third,released);check(f.resolve().size()==1&&f.health()==70&&f.server.ability_effects(f.target).size()==64,"Releasing global capacity allows atomic damage/status retry");}
}
int main(){try{damage_status_and_credit();all_or_nothing();interruption_and_queue_bounds();global_periodic_reservation();
#ifdef DAE_COMBAT_EFFECT_PHYSICS
 std::cout<<"Jolt achieved-pose combat: ";
#else
 std::cout<<"Synthetic Headless query combat: ";
#endif
 std::cout<<"Atomic damage/status requests, durable credit, interruption/death, evaluator rollback and global queue/periodic capacity passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
