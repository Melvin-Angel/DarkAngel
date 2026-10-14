#include <darkangel/world_objects.hpp>
#include <darkangel/party_lifecycle.hpp>
#include <darkangel/party_inventory.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
}
int main(){try{
    // Components decode with their kind's defaults and refuse malformed fields.
    {const auto pickup=decode_pickup(R"({"item":"8c600aed-a2f8-4b9b-8e37-4c21d389cd1f","quantity":2})");check(pickup.quantity==2&&pickup.prompt=="Pick up"&&pickup.collected.scope==LedgerScope::Save&&!pickup.collected.resets,"Pickup defaults: a one-time saved pickup");
        const auto respawning=decode_pickup(R"({"item":"8c600aed-a2f8-4b9b-8e37-4c21d389cd1f","policies":{"collected":{"scope":"save","resets":["rest","party_wipe"]}}})");check(respawning.collected.on(LedgerReset::Rest)&&respawning.collected.on(LedgerReset::PartyWipe)&&!respawning.collected.on(LedgerReset::SceneEntry),"Pickup policy override");
        check(rejects([]{decode_pickup(R"({"quantity":1})");})&&rejects([]{decode_pickup(R"({"item":"8c600aed-a2f8-4b9b-8e37-4c21d389cd1f","quantity":0})");})&&rejects([]{decode_pickup(R"({"item":"8c600aed-a2f8-4b9b-8e37-4c21d389cd1f","colour":1})");})&&rejects([]{decode_policy(R"({"resets":["sunrise"]})");}),"Malformed pickups or policies were accepted");
        const auto policy=decode_policy(encode_policy({LedgerScope::Session,LedgerReset::SceneEntry}));check(policy.scope==LedgerScope::Session&&policy.on(LedgerReset::SceneEntry),"Policy round trip");}
    {const auto altar=decode_altar(R"({"name":"Temple Altar"})");check(altar.rest&&altar.fast_travel&&altar.save&&altar.checkpoint==AltarCheckpoint::Rest&&altar.forward==1.5,"Altar defaults");
        const auto at=altar_arrival(altar,{10,0,10},0);check(std::abs(at.x-10)<1e-9&&std::abs(at.z-11.5)<1e-9,"Altar arrival lies along its facing");
        check(rejects([]{decode_altar(R"({"prompt":"Rest"})");})&&rejects([]{decode_altar(R"({"name":"A","checkpoint":"always"})");}),"Malformed altars were accepted");}
    {const auto door=decode_mover(R"({"locked":true,"requires":{"item":"94a81491-da8b-478f-9d4e-e78cfcd46092","consume":true}})");check(door.door&&door.locked&&door.requirement.consume&&door.opened.on(LedgerReset::Rest)&&door.unlocked.scope==LedgerScope::Save&&!door.unlocked.resets,"Door defaults: unlocked for good, open until a rest");
        const auto platform=decode_mover(R"({"door":false,"activation":"auto","behaviour":"loop","offset":[0,0,6],"speed":3,"wait":1})");check(!platform.door&&platform.opened.scope==LedgerScope::Session&&platform.opened.on(LedgerReset::SceneEntry),"Platform defaults: session state, reset on scene entry");
        check(rejects([]{decode_mover(R"({"locked":true})");})&&rejects([]{decode_mover(R"({"activation":"event"})");})&&rejects([]{decode_mover(R"({"offset":[0,0,0]})");})&&rejects([]{decode_mover(R"({"activation":"auto","locked":true,"requires":{"tag":"x"}})");}),"Malformed movers were accepted");}
    // A mover's state machine: toggle, return after waiting, loop by itself; locked until unlocked; restored at its end.
    {MoverDefinition d;d.offset={0,2,0};d.speed=2;MoverState door(d);check(!door.open()&&door.activate()&&door.open()&&door.moving(),"A toggled door starts opening");double travelled=0;for(int i=0;i<120;++i)travelled+=door.step(1./60).y/60;check(!door.moving()&&std::abs(door.at()-1)<1e-9&&std::abs(travelled-2)<1e-6,"A door reaches its end exactly");
        check(door.activate()&&!door.open(),"Toggling again closes it");
        d.locked=true;d.requirement.tag="key";MoverState gate(d);check(!gate.activate()&&!gate.open(),"A locked gate refused to stay shut");gate.unlock();check(gate.activate()&&gate.open(),"An unlocked gate opens");
        MoverState restored(d);restored.restore(true,true);check(restored.unlocked()&&restored.open()&&restored.at()==1&&!restored.moving(),"Restore places it at its end");
        MoverDefinition p;p.door=false;p.offset={4,0,0};p.speed=4;p.wait=.5;p.behaviour=MoverBehaviour::Return;MoverState lift(p);check(lift.activate()&&!lift.activate(),"A returning platform takes one activation at a time");
        for(int i=0;i<60;++i)lift.step(1./60);check(lift.at()==1,"Platform arrived");for(int i=0;i<31;++i)lift.step(1./60);check(!lift.open(),"Platform returns after its wait");for(int i=0;i<60;++i)lift.step(1./60);check(lift.at()==0,"Platform is back");
        p.behaviour=MoverBehaviour::Loop;p.activation=MoverActivation::Auto;MoverState loop(p);for(int i=0;i<31;++i)loop.step(1./60);check(loop.open(),"An automatic loop starts after waiting at its start");}
    // The world view and requests round trip; malformed ones are refused.
    {WorldStateView v;v.revision=7;v.collected={{1,2}};v.open={{3,4}};v.members={{5,PartyMemberState::Downed,.4}};v.destinations={{AssetId::random(),{6,7},"Gate Altar"}};v.checkpoint=v.destinations.front();v.wiped=true;
        check(decode_world_state(encode_world_state(v))==v,"World view round trip");WorldRequest r{WorldRequestKind::Travel,{1,9},v.destinations.front()};check(decode_world_request(encode_world_request(r))==r,"World request round trip");
        check(rejects([]{decode_world_request(R"({"schema":1,"kind":"fly","altar":"00000000000000010000000000000009"})");})&&rejects([]{decode_world_state(R"({"schema":2})");}),"Malformed world messages were accepted");
        check(world_text(world_bytes("altar"))=="altar","Owner payload conversion");}
    // Party records: discovery is kept for good and lists fast-travel destinations; the checkpoint is one record.
    {WorldLedger ledger;const AssetId a=AssetId::random(),b=AssetId::random();AltarDefinition one;one.name="One";AltarDefinition two;two.name="Two";two.fast_travel=false;
        check(!read_checkpoint(ledger)&&fast_travel_destinations(ledger).empty(),"A fresh ledger has a checkpoint");record_altar(ledger,a,{1,1},one);record_altar(ledger,b,{1,2},two);
        check(altar_discovered(ledger,a,{1,1})&&altar_discovered(ledger,b,{1,2})&&fast_travel_destinations(ledger).size()==1&&fast_travel_destinations(ledger).front().name=="One","Discovered fast-travel altars");
        write_checkpoint(ledger,{b,{1,2},"Two"});ledger.reset(LedgerReset::Rest);ledger.reset(LedgerReset::PartyWipe);check(read_checkpoint(ledger)&&read_checkpoint(ledger)->scene==b&&altar_discovered(ledger,a,{1,1}),"Rests and wipes kept the checkpoint and discoveries");
        const std::array<LedgerKind,2> known{LedgerKind{"altar",1},LedgerKind{"checkpoint",1}};auto loaded=WorldLedger::deserialize(ledger.serialize(),known);check(read_checkpoint(loaded)==read_checkpoint(ledger)&&fast_travel_destinations(loaded)==fast_travel_destinations(ledger),"Party records survive save and load");}
    // Downed, revive, wipe.
    {PartyRules rules;rules.revive_seconds=1;rules.wipe_seconds=.5;PartyLifecycleCoordinator party(rules);using M=PartyLifecycleCoordinator::Member;
        std::vector<M> members{{1,{0,0,0},100},{2,{1,0,0},100}};party.step(members,1./60);check(party.state(1)==PartyMemberState::Alive&&!party.wiped(),"Healthy party");
        members[1].health=0;auto outcome=party.step(members,1./60);check(outcome.downed==std::vector<std::uint64_t>{2}&&party.state(2)==PartyMemberState::Downed&&!party.wiped(),"A member at zero Health is downed, not a wipe");
        for(int i=0;i<30;++i){party.hold(1,2);party.step(members,1./60);}check(party.progress(2)>.45&&party.progress(2)<.55,"Revive progress advances while held");
        for(int i=0;i<20;++i)party.step(members,1./60);check(party.progress(2)==0,"Releasing Interact resets revive progress");
        members[0].position={5,0,0};for(int i=0;i<10;++i){party.hold(1,2);party.step(members,1./60);}check(party.progress(2)==0,"A reviver out of range revives nothing");members[0].position={0,0,0};
        // Two revivers share one progress and the revive completes once.
        members.push_back({3,{0,0,1},100});unsigned revived=0;for(int i=0;i<70;++i){party.hold(1,2);party.hold(3,2);auto o=party.step(members,1./60);revived+=unsigned(o.revived.size());if(!o.revived.empty())members[1].health=50;}check(revived==1&&party.state(2)==PartyMemberState::Alive,"Two revivers completed one revive once");
        members[2].connected=false;members[2].health=0;party.step(members,1./60);check(!party.wiped(),"A disconnected member does not count toward a wipe");
        members[0].health=0;members[1].health=0;outcome=party.step(members,1./60);check(outcome.wipe&&party.wiped()&&!party.respawn_due(),"Every connected member downed is a wipe");
        for(int i=0;i<31;++i)party.step(members,1./60);check(party.respawn_due(),"The respawn falls due after the wipe time");}
    {PartyLifecycleCoordinator solo;std::vector<PartyLifecycleCoordinator::Member> one{{1,{},0}};check(solo.step(one,1./60).wipe,"Solo play wipes at once");}
    check(rejects([]{PartyRules r;r.revive_health=0;validate_party_rules(r);}),"Zero revive Health accepted");
    // Trusted take: all or nothing across unequipped stacks.
    {auto catalog=std::make_shared<ItemCatalog>();ItemDefinition key;key.id=AssetId::random();key.name="Temple Key";key.classification="key";key.stack_limit=2;catalog->insert(key);PartyInventory party(catalog);const StableId who{1,1};party.join(who);
        check(party.grant(who,key.id,3).failure==InventoryFailure::None&&party.quantity(who,key.id)==3,"Grant across stacks");check(party.take(who,key.id,4).failure==InventoryFailure::Missing&&party.quantity(who,key.id)==3,"A short take changed the inventory");
        check(party.take(who,key.id,3).failure==InventoryFailure::None&&party.quantity(who,key.id)==0,"Take across stacks");}
    std::cout<<"World object and party lifecycle tests passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
