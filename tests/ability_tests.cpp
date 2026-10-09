#include <darkangel/world_session.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace darkangel;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected checked API rejection");}
SessionHandshake handshake(){return {1,std::string(64,'a'),std::string(64,'b'),77};}
std::vector<AttributeDefinition> schema(double health=100){return {
    {1,"MaxHealth",AttributeKind::Statistic,100,0,10000,0},
    {2,"Health",AttributeKind::Resource,health,0,10000,1},
    {3,"Stamina",AttributeKind::Resource,60,0,60,0},
    {4,"Essence",AttributeKind::Resource,40,0,40,0}};}
double value(const AbilityOwnerSnapshot& snapshot,AttributeId id){for(const auto& attribute:snapshot.attributes)if(attribute.id==id)return attribute.value;throw std::runtime_error("Missing attribute snapshot");}
struct Fixture {
    WorldSession server{SessionRole::Server,handshake()};AbilityOwnerHandle owner;
    std::shared_ptr<ActionDefinition> action=std::make_shared<ActionDefinition>();
    std::shared_ptr<AbilityDefinition> light=std::make_shared<AbilityDefinition>(),heavy=std::make_shared<AbilityDefinition>();
    InputProfile input;std::shared_ptr<CombatKitDefinition> kit=std::make_shared<CombatKitDefinition>();
    std::vector<std::shared_ptr<const AbilityDefinition>> catalogue;
    Fixture(double heavy_stamina=40){
        ObjectData object;object.id={3,1};auto id=server.create(object);owner=server.configure_abilities(id,schema(),2,1);
        input.id=AssetId::random();for(unsigned i=0;i<combat_slot_count;++i)input.actions.push_back({i+1,"combat."+std::to_string(i),InputActionKind::Button});
        action->id=AssetId::random();action->generation=std::string(64,'c');action->duration=4*action_tick_units;action->loops=1;
        action->blocks={{1,0,0,0,ActionBlockKind::Cue,"swing"},{2,1,0,4*action_tick_units,ActionBlockKind::MovementLock,"attack"},{3,2,action_tick_units,2*action_tick_units,ActionBlockKind::HitWindow,"blade"}};
        light->id=AssetId::random();light->generation=std::string(64,'d');light->action=action;light->costs={{3,20},{4,10}};light->cooldown_group=1;light->cooldown_ticks=6;
        *heavy=*light;heavy->id=AssetId::random();heavy->costs={{3,heavy_stamina},{4,15}};heavy->cooldown_group=2;
        kit->id=AssetId::random();kit->generation="kit-v1";kit->locomotion_stance=AssetId::random();
        for(unsigned i=0;i<combat_slot_count;++i)kit->slots[i]={static_cast<CombatSlot>(i),i+1,{}};
        kit->slots[0].ability=light->id;kit->slots[1].ability=heavy->id;
        catalogue={light,heavy};server.equip_combat_kit(owner,kit,input,catalogue);
    }
    AbilityRequest request(std::uint64_t op,CombatSlot slot=CombatSlot::Light,InputEdge edge=InputEdge::Pressed,bool replace=false){
        return {owner,op,server.ability_snapshot(owner).grant_generation,slot,{static_cast<unsigned>(slot)+1,edge,op*1000,edge==InputEdge::Hold?600000u:0u,1,false},replace};
    }
    void step(unsigned count,unsigned rate=action_tick_units){for(unsigned i=0;i<count;++i){server.advance_abilities(server.ability_snapshot(owner).tick+1,rate);server.drain_ability_actions();}}
};
void commitment(){
    Fixture f;auto request=f.request(1);auto before=f.server.ability_snapshot(f.owner);
    check(f.server.can_activate(request)==AbilityFailure::None&&f.server.ability_snapshot(f.owner).revision==before.revision,"CanActivate is read-only");
    auto accepted=f.server.request_ability(request);auto snapshot=f.server.ability_snapshot(f.owner);
    check(accepted.committed&&accepted.handle.activation&&value(snapshot,3)==40&&value(snapshot,4)==30&&snapshot.cooldowns[0].second==6,"Cost cooldown and action commit together");
    auto entry=f.server.drain_ability_actions();check(entry.size()==1&&entry[0].phase==ActionPhase::Active&&entry[0].batch.events.size()==2,"Prepared time-zero action publishes once");
    auto duplicate=f.server.request_ability(request);check(duplicate.duplicate&&duplicate.handle==accepted.handle&&value(f.server.ability_snapshot(f.owner),3)==40&&f.server.drain_ability_actions().empty(),"Duplicate operation cannot spend or replay cues");
    auto conflict=request;conflict.replace_active=true;check(f.server.request_ability(conflict).failure==AbilityFailure::OperationConflict,"Duplicate payload conflict rejected");
    auto busy=f.server.request_ability(f.request(2,CombatSlot::Heavy));check(busy.failure==AbilityFailure::Busy&&f.server.ability_snapshot(f.owner).active==accepted.handle,"Busy failure preserves old action");
    // A replacement that cannot afford both resources must preserve the outgoing execution.
    auto invalid=std::make_shared<AbilityDefinition>(*f.heavy);invalid->action=std::make_shared<ActionDefinition>(*f.action);
    std::const_pointer_cast<ActionDefinition>(invalid->action)->duration=0;
    std::array<std::shared_ptr<const AbilityDefinition>,2> bad_catalogue{f.light,invalid};
    rejects([&]{f.server.equip_combat_kit(f.owner,f.kit,f.input,bad_catalogue);});
    check(f.server.ability_snapshot(f.owner).active==accepted.handle&&value(f.server.ability_snapshot(f.owner),3)==40,"Failed action preparation spends nothing and retains kit/action");
    // Existing installed heavy is affordable and replaces only after preparation.
    auto replaced=f.server.request_ability(f.request(3,CombatSlot::Heavy,InputEdge::Pressed,true));
    check(replaced.committed&&value(f.server.ability_snapshot(f.owner),3)==0,"Atomic successful replacement");
    auto updates=f.server.drain_ability_actions();check(updates.size()==2&&updates[0].reason==AbilityActionReason::Replaced&&updates[0].batch.events[0].edge==ActionEdge::End&&updates[1].reason==AbilityActionReason::Started,"Old ownership ends before new entry");
    check(f.server.cancel_ability(accepted.handle)==AbilityFailure::None&&f.server.ability_snapshot(f.owner).active==replaced.handle,"Old cancellation cannot stop replacement");
    check(f.server.cancel_ability(replaced.handle)==AbilityFailure::None&&f.server.cancel_ability(replaced.handle)==AbilityFailure::None,"Cancellation idempotent");
    check(value(f.server.ability_snapshot(f.owner),3)==0&&f.server.ability_snapshot(f.owner).cooldowns.size()==2&&f.server.drain_ability_actions().size()==1,"Cancellation does not refund or erase cooldown");
    auto insufficient=f.server.request_ability(f.request(4,CombatSlot::Light));check(insufficient.failure==AbilityFailure::Cooldown,"Cooldown checked before cost");
    f.step(6,0);check(f.server.ability_snapshot(f.owner).cooldowns.empty(),"Simulation cooldown advances through hitstop");
    auto resources=f.server.request_ability(f.request(5,CombatSlot::Light));check(resources.failure==AbilityFailure::Resources&&value(f.server.ability_snapshot(f.owner),4)==15,"Insufficient transaction leaves other resource unchanged");
    Fixture poor(50);auto previous=poor.server.request_ability(poor.request(1));poor.server.drain_ability_actions();
    auto failed=poor.server.request_ability(poor.request(2,CombatSlot::Heavy,InputEdge::Pressed,true));
    check(failed.failure==AbilityFailure::Resources&&poor.server.ability_snapshot(poor.owner).active==previous.handle&&value(poor.server.ability_snapshot(poor.owner),3)==40&&poor.server.drain_ability_actions().empty(),"Insufficient replacement preserves previous action and emits no cleanup");
}
void swap_and_input(){
    Fixture f;auto old=f.server.request_ability(f.request(1));f.server.drain_ability_actions();
    f.light->costs={{3,1}};f.action->duration=0; // live grant must retain its immutable source generation
    f.step(1);check(f.server.ability_snapshot(f.owner).action->clock==action_tick_units,"Frozen action survives mutable alias edits");
    auto new_action=std::make_shared<ActionDefinition>();new_action->id=AssetId::random();new_action->generation=std::string(64,'e');new_action->duration=2*action_tick_units;new_action->loops=1;
    auto charged=std::make_shared<AbilityDefinition>(*f.light);charged->action=new_action;charged->activate_on=InputEdge::Hold;charged->minimum_held_us=500000;charged->cancel_on_release=true;charged->cooldown_group=9;charged->cooldown_ticks=3;
    std::array<std::shared_ptr<const AbilityDefinition>,1> catalogue{charged};auto new_kit=std::make_shared<CombatKitDefinition>(*f.kit);new_kit->slots[1].ability={};
    f.server.equip_combat_kit(f.owner,new_kit,f.input,catalogue);auto cleanup=f.server.drain_ability_actions();
    check(cleanup.size()==1&&cleanup[0].reason==AbilityActionReason::GrantRemoved&&!f.server.ability_snapshot(f.owner).active&&f.server.ability_snapshot(f.owner).cooldowns.size()==1,"Kit removal forces cleanup and preserves committed cooldown");
    auto stale=f.request(2);stale.grant_generation=1;check(f.server.request_ability(stale).failure==AbilityFailure::StaleGrant,"Old grant request denied");
    check(f.server.request_ability(f.request(3,CombatSlot::Light,InputEdge::Hold)).failure==AbilityFailure::InputIgnored,"Held input cannot enter replacement kit");
    check(f.server.request_ability(f.request(4)).failure==AbilityFailure::InputIgnored,"Ability owns edge interpretation while fresh press rearms");
    auto short_hold=f.request(5,CombatSlot::Light,InputEdge::Hold);short_hold.input.held_us=1000;check(f.server.request_ability(short_hold).failure==AbilityFailure::InputIgnored,"Authored hold threshold");
    auto held=f.server.request_ability(f.request(6,CombatSlot::Light,InputEdge::Hold));check(held.committed&&value(f.server.ability_snapshot(f.owner),3)==39,"Ability-owned hold activation");f.server.drain_ability_actions();
    auto release=f.server.request_ability(f.request(7,CombatSlot::Light,InputEdge::Released));check(!release.committed&&release.failure==AbilityFailure::None&&release.handle==held.handle&&!f.server.ability_snapshot(f.owner).active,"Release cancels without second charge");
    check(f.server.cancel_ability(old.handle)==AbilityFailure::None,"Retired old activation cancellation is safe");
}
void authority_and_health(){
    Fixture f;WorldSession client(SessionRole::Client,handshake());
    rejects([&]{client.configure_abilities(f.owner.network,schema(),2,1);});rejects([&]{client.request_ability(f.request(1));});
    auto forged=f.owner;forged.entity.epoch++;rejects([&]{f.server.ability_snapshot(forged);});
    rejects([&]{f.server.configure_abilities(f.owner.network,schema(),2,1);});
    auto health_ability=std::make_shared<AbilityDefinition>(*f.light);health_ability->costs={{2,25}};health_ability->cooldown_ticks=0;
    std::array<std::shared_ptr<const AbilityDefinition>,2> catalogue{health_ability,f.heavy};f.server.equip_combat_kit(f.owner,f.kit,f.input,catalogue);
    auto pair=create_loopback(handshake());auto host=pair.host->open(handshake()),peer=pair.client->open(handshake());f.server.attach(*pair.host,host);client.attach(*pair.client,peer);
    for(unsigned n=0;n<20;++n){f.server.tick();client.tick();}
    auto revision=f.server.revision();auto hit=f.server.request_ability(f.request(1));check(hit.committed&&f.server.revision()==revision+1,"Health cost uses existing session revision");
    check(f.server.world().read(f.owner.entity).health.current==75&&f.server.objects().at(f.owner.network).health.current==75,"World and session Health publish coherently");
    for(unsigned n=0;n<20;++n){f.server.tick();client.tick();}
    check(client.objects().at(f.owner.network).health.current==75,"Existing baseline replicates committed Health");
    auto late_pair=create_loopback(handshake());auto lh=late_pair.host->open(handshake()),lc=late_pair.client->open(handshake());WorldSession late(SessionRole::Client,handshake());f.server.attach(*late_pair.host,lh);late.attach(*late_pair.client,lc);
    for(unsigned n=0;n<20;++n){f.server.tick();client.tick();late.tick();}check(late.objects().at(f.owner.network).health.current==75,"Late join includes Health outcome");
    f.server.drain_ability_actions();f.server.destroy(f.owner.network);auto despawn=f.server.drain_ability_actions();check(despawn.size()==1&&despawn[0].reason==AbilityActionReason::Despawned,"Despawn cleans owned action");
    rejects([&]{f.server.cancel_ability(hit.handle);});ObjectData replacement;replacement.id={3,1};auto id=f.server.create(replacement);check(id!=f.owner.network,"Despawn network identity cannot alias old owner");
    auto replacement_owner=f.server.configure_abilities(id,schema(),2,1);check(replacement_owner.entity!=f.owner.entity,"Entity generation changes after reuse");
}
void bounds_and_completion(){
    Fixture f;auto active=f.server.request_ability(f.request(1));f.server.drain_ability_actions();
    f.server.advance_abilities(1);auto crossed=f.server.drain_ability_actions();check(crossed.size()==1&&crossed[0].tick==1&&crossed[0].batch.events[0].kind==ActionBlockKind::HitWindow,"Existing action hit boundaries retain tick attribution, never damage");
    rejects([&]{f.server.advance_abilities(3);});check(f.server.ability_snapshot(f.owner).tick==1,"Skipped tick leaves all state unchanged");
    f.server.advance_abilities(2);f.server.drain_ability_actions();f.server.advance_abilities(3);f.server.drain_ability_actions();f.server.advance_abilities(4);auto completion=f.server.drain_ability_actions();
    check(completion.size()==1&&completion[0].phase==ActionPhase::Completed&&!f.server.ability_snapshot(f.owner).active&&f.server.cancel_ability(active.handle)==AbilityFailure::None,"Completion releases execution; cancel remains idempotent");
    for(unsigned i=2;i<=128;++i)check(f.server.request_ability(f.request(i,CombatSlot::Spell1)).failure==AbilityFailure::Unassigned,"Optional slots diagnosed");
    check(f.server.request_ability(f.request(129,CombatSlot::Spell1)).failure==AbilityFailure::HistoryFull,"Bounded operation history fails closed");
    f.server.retire_ability_operations(f.owner,100);auto stale=f.server.request_ability(f.request(1));check(stale.failure==AbilityFailure::StaleOperation&&value(f.server.ability_snapshot(f.owner),3)==40,"Retired operation cannot spend again");
    rejects([&]{f.server.retire_ability_operations(f.owner,99);});check(f.server.request_ability(f.request(129,CombatSlot::Spell1)).failure==AbilityFailure::Unassigned,"Explicit retirement restores bounded capacity");
    Fixture queue;auto initial=queue.server.request_ability(queue.request(1));queue.server.cancel_ability(initial.handle);queue.step(6);
    // Fill lifecycle work with repeated kit replacements; publishing must fail atomically.
    auto free=std::make_shared<AbilityDefinition>(*queue.light);free->costs.clear();free->cooldown_ticks=0;
    std::array<std::shared_ptr<const AbilityDefinition>,2> catalogue{free,queue.heavy};queue.server.equip_combat_kit(queue.owner,queue.kit,queue.input,catalogue);queue.server.drain_ability_actions();
    for(unsigned i=2;i<=65;++i){auto receipt=queue.server.request_ability(queue.request(i));check(receipt.committed,"Queue fill commits");queue.server.cancel_ability(receipt.handle);}
    auto before=queue.server.ability_snapshot(queue.owner);rejects([&]{queue.server.request_ability(queue.request(66));});auto after=queue.server.ability_snapshot(queue.owner);
    check(after.revision==before.revision&&after.retained_operations==before.retained_operations&&!after.active,"Queue overflow does not partially commit");queue.server.drain_ability_actions();check(queue.server.request_ability(queue.request(66)).committed,"Draining permits retry of uncommitted operation");
}
void death_and_interruption(){
    Fixture f;auto uninterruptible=std::make_shared<AbilityDefinition>(*f.light);uninterruptible->interruptible=false;
    std::array<std::shared_ptr<const AbilityDefinition>,2> catalogue{uninterruptible,f.heavy};f.server.equip_combat_kit(f.owner,f.kit,f.input,catalogue);
    auto active=f.server.request_ability(f.request(1));f.server.drain_ability_actions();
    check(f.server.cancel_ability(active.handle)==AbilityFailure::CancelDenied&&f.server.ability_snapshot(f.owner).active==active.handle,"Authored uninterruptible action rejects ordinary cancel");
    check(f.server.request_ability(f.request(2,CombatSlot::Heavy,InputEdge::Pressed,true)).failure==AbilityFailure::Busy&&value(f.server.ability_snapshot(f.owner),3)==40,"Replacement cannot bypass interruption policy");
    f.server.equip_combat_kit(f.owner,f.kit,f.input,catalogue);check(!f.server.ability_snapshot(f.owner).active&&f.server.drain_ability_actions().size()==1,"Grant removal forcibly cleans even uninterruptible execution");
    auto lethal=std::make_shared<AbilityDefinition>(*f.light);lethal->costs={{2,100}};lethal->cooldown_group=7;
    catalogue[0]=lethal;f.server.equip_combat_kit(f.owner,f.kit,f.input,catalogue);
    auto dead=f.server.request_ability(f.request(3));auto updates=f.server.drain_ability_actions();
    check(dead.committed&&!f.server.ability_snapshot(f.owner).active&&value(f.server.ability_snapshot(f.owner),2)==0&&updates.size()==2&&updates[1].reason==AbilityActionReason::Death,"Death after health commitment cleans newly entered action immediately");
    check(f.server.request_ability(f.request(4,CombatSlot::Heavy)).failure==AbilityFailure::Dead,"Dead owners cannot begin next action");
    Fixture tap;auto tapped=std::make_shared<AbilityDefinition>(*tap.light);tapped->activate_on=InputEdge::Tapped;catalogue={tapped,tap.heavy};tap.server.equip_combat_kit(tap.owner,tap.kit,tap.input,catalogue);
    check(tap.server.request_ability(tap.request(1)).failure==AbilityFailure::InputIgnored,"Tap ability does not spend on press");
    auto cancelled=tap.request(2,CombatSlot::Light,InputEdge::Tapped);cancelled.input.cancelled=true;
    check(tap.server.request_ability(cancelled).failure==AbilityFailure::InputIgnored,"Cancelled event cannot activate tap ability");
    check(tap.server.request_ability(tap.request(3,CombatSlot::Light,InputEdge::Tapped)).committed,"Ability owns tap activation");
    auto invalid=tap.request(4);invalid.input.value=std::numeric_limits<float>::quiet_NaN();check(tap.server.request_ability(invalid).failure==AbilityFailure::InvalidRequest,"Invalid input cannot activate");
}
std::vector<std::pair<std::uint64_t,ActionEvent>> render_trace(unsigned frames){
    Fixture f;std::vector<std::pair<std::uint64_t,ActionEvent>> result;
    auto capture=[&]{for(const auto& update:f.server.drain_ability_actions())for(const auto& event:update.batch.events)result.emplace_back(update.tick,event);};
    check(f.server.request_ability(f.request(1)).committed,"Trace initial activation");capture();
    unsigned accumulator{};std::uint64_t tick{};
    for(unsigned frame=0;frame<frames;++frame){
        accumulator+=60;
        while(accumulator>=frames){accumulator-=frames;f.server.advance_abilities(++tick);capture();
            if(tick==6||tick==12){check(f.server.request_ability(f.request(tick/6+1)).committed,"Trace cooldown expiry activation");capture();}
        }
    }
    check(tick==60&&value(f.server.ability_snapshot(f.owner),3)==0&&value(f.server.ability_snapshot(f.owner),4)==10,"Same fixed-tick resource outcomes at each render rate");
    return result;
}
}
int main(){try{commitment();swap_and_input();authority_and_health();bounds_and_completion();death_and_interruption();check(render_trace(30)==render_trace(60)&&render_trace(60)==render_trace(144),"Identical action/cost traces at 30/60/144 render schedules");std::cout<<"Ability grants, atomic action/cost/cooldown commits, duplicate receipts, cleanup, bounds and WorldSession Health checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
