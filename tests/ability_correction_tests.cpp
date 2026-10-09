#include <darkangel/world_session.hpp>
#include "../engine/runtime/ability_wire.hpp"
#include <algorithm>
#include <deque>
#include <iostream>
#include <limits>
using namespace darkangel;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
std::uint64_t word(const std::vector<std::byte>& bytes,std::size_t at){std::uint64_t value{};for(unsigned n=0;n<8;++n)value|=std::uint64_t(std::to_integer<unsigned char>(bytes.at(at+n)))<<(n*8);return value;}
class Delayed:public Transport {
    Transport& inner;
public:
    bool hold{};std::deque<TransportMessage> delayed;
    explicit Delayed(Transport& transport):inner(transport){}
    ConnectionHandle open(const SessionHandshake& h)override{return inner.open(h);}
    bool connected()const override{return inner.connected();}
    bool valid(ConnectionHandle h)const override{return inner.valid(h);}
    TransportLimits limits()const override{return inner.limits();}
    bool send(ConnectionHandle h,Delivery d,std::span<const std::byte> bytes)override{return inner.send(h,d,bytes);}
    void disconnect(ConnectionHandle h)override{inner.disconnect(h);}
    std::vector<TransportMessage> poll(std::size_t count)override{
        std::vector<TransportMessage> result;if(!hold)while(!delayed.empty()&&result.size()<count){result.push_back(std::move(delayed.front()));delayed.pop_front();}
        for(auto& message:inner.poll(count-result.size()))if(hold&&word(message.bytes,8)==11){check(delayed.size()<128,"Test correction queue bound");delayed.push_back(std::move(message));}else result.push_back(std::move(message));return result;
    }
};
struct Fixture {
    SessionHandshake hello{3,std::string(64,'a'),std::string(64,'b'),77};LoopbackPair transport=create_loopback(hello);Delayed delayed{*transport.client};WorldSession server{SessionRole::Server,hello},client{SessionRole::Client,hello};ConnectionHandle hp,cp;std::uint64_t id{};AbilityOwnerHandle owner;
    Fixture(){hp=transport.host->open(hello);cp=delayed.open(hello);server.attach(*transport.host,hp);client.attach(delayed,cp);ObjectData object;object.id={3,1};id=server.create(object);
        std::vector<AttributeDefinition> schema{{1,"MaxHealth",AttributeKind::Statistic,100,0,1000,0},{2,"Health",AttributeKind::Resource,100,0,1000,1},{3,"Stamina",AttributeKind::Resource,60,0,60,0}};
        // Exercise fragments at the supported 64-attribute maximum.
        for(unsigned i=4;i<=64;++i)schema.push_back({i,"Stat"+std::to_string(i),AttributeKind::Statistic,double(i),0,1000,0});owner=server.configure_abilities(id,schema,2,1);
        InputProfile input;input.id=AssetId::random();auto kit=std::make_shared<CombatKitDefinition>();kit->id=AssetId::random();kit->generation="kit";kit->locomotion_stance=AssetId::random();for(unsigned i=0;i<combat_slot_count;++i){input.actions.push_back({i+1,"combat."+std::to_string(i),InputActionKind::Button});kit->slots[i]={static_cast<CombatSlot>(i),i+1,{}};}
        auto action=std::make_shared<ActionDefinition>();action->id=AssetId::random();action->generation=std::string(64,'a');action->duration=4*action_tick_units;action->loops=1;auto ability=std::make_shared<AbilityDefinition>();ability->id=AssetId::random();ability->generation=std::string(64,'b');ability->action=action;ability->costs={{3,20}};ability->cooldown_group=1;ability->cooldown_ticks=6;kit->slots[0].ability=ability->id;std::array<std::shared_ptr<const AbilityDefinition>,1> catalogue{ability};server.equip_combat_kit(owner,kit,input,catalogue);server.own_motor(id,hp);MotorState motor;server.publish_motor(id,motor);
    }
    void pump(unsigned count=12){for(unsigned i=0;i<count;++i){server.tick();client.tick();}}
    AbilityRequest request(std::uint64_t operation){return {owner,operation,server.ability_snapshot(owner).grant_generation,CombatSlot::Light,{1,InputEdge::Pressed,operation*1000,0,1,false},false};}
    void step(){auto tick=server.ability_snapshot(owner).tick+1;server.advance_abilities(tick);server.drain_ability_actions();auto motor=server.motors().at(id);motor.tick=tick;motor.sequence=tick;server.publish_motor(id,motor);}
};
void bundle(){Fixture f;f.pump();auto first=f.client.ability_corrections().at(f.id);check(first.ability.attributes.size()==64&&first.motor.tick==first.ability.tick&&first.ability.owner.entity!=f.owner.entity,"Complete owner correction uses local checked identities");check(f.client.world().valid(first.ability.owner.entity),"Correction identity resolves through lifecycle baseline");auto receipt=f.server.request_ability(f.request(1));check(receipt.committed,"Native request committed");auto rejected=f.server.request_ability(f.request(5));check(rejected.failure==AbilityFailure::Busy,"Sparse operation has explicit terminal rejection");f.pump();const auto& current=f.client.ability_corrections().at(f.id);check(current.ability.operations.size()==2&&current.ability.operations[0].operation==1&&current.ability.operations[0].committed&&current.ability.operations[1].operation==5&&current.ability.operations[1].failure==AbilityFailure::Busy,"Exact included operations retain acceptance/rejection rather than inferring gaps");check(current.ability.active&&current.ability.action&&current.ability.active->activation==receipt.handle.activation&&current.ability.cooldowns.size()==1&&current.ability.attributes[2].value==40,"Coherent costs cooldowns and active action");f.client.acknowledge_ability_correction(f.id);f.pump();check(f.server.ability_snapshot(f.owner).retained_operations==0&&f.server.request_ability(f.request(1)).failure==AbilityFailure::StaleOperation,"Prepared snapshot ACK retires storage without permitting duplicate commitments");f.step();f.pump();check(f.client.ability_corrections().at(f.id).motor.tick==1&&f.client.ability_corrections().at(f.id).ability.action->clock==action_tick_units,"Later correction carries aligned motor/action tick");}
void fragments(){Fixture f;f.delayed.hold=true;f.pump();check(f.client.ability_corrections().empty()&&f.delayed.delayed.size()>=2,"Partial/held fragments cannot publish an owner correction");std::reverse(f.delayed.delayed.begin(),f.delayed.delayed.end());f.delayed.hold=false;f.pump();check(f.client.ability_corrections().contains(f.id),"Reordered fragments publish one complete owner bundle");}
void timeout(){Fixture f;f.delayed.hold=true;f.pump();check(f.delayed.delayed.size()>=2,"Captured fragmented correction");auto fragment=std::move(f.delayed.delayed.front());f.delayed.delayed.clear();f.delayed.delayed.push_back(std::move(fragment));f.delayed.hold=false;f.client.tick();f.delayed.hold=true;for(unsigned i=0;i<32;++i)f.client.tick();check(f.client.ability_correction_needs_resync()&&f.client.ability_corrections().empty(),"Incomplete fragment timeout retains no partial authoritative state and exposes resync");f.delayed.delayed.clear();f.delayed.hold=false;f.pump(64);check(f.client.ability_corrections().contains(f.id),"Periodic keyframe recovers missing fragments");f.client.acknowledge_ability_correction(f.id);check(!f.client.ability_correction_needs_resync(),"Native preparation explicitly clears resync");}
void privacy(){Fixture f;f.pump();auto second=create_loopback(f.hello);WorldSession observer(SessionRole::Client,f.hello);auto hp=second.host->open(f.hello),cp=second.client->open(f.hello);f.server.attach(*second.host,hp);observer.attach(*second.client,cp);for(unsigned i=0;i<32;++i){f.server.tick();observer.tick();}check(observer.objects().contains(f.id)&&observer.ability_corrections().empty(),"Observers receive existing public baseline but no private owner correction");rejects([&]{observer.acknowledge_ability_correction(f.id);});rejects([&]{f.server.acknowledge_ability_correction(f.id);});}
void codec(){Fixture f;f.pump();auto correction=f.client.ability_corrections().at(f.id);auto bytes=session_detail::encode_ability_correction(correction);check(session_detail::decode_ability_correction(bytes).ability.attributes.size()==64,"Explicit codec roundtrip");bytes.pop_back();rejects([&]{session_detail::decode_ability_correction(bytes);});auto invalid=correction;invalid.ability.attributes[0].value=std::numeric_limits<double>::infinity();rejects([&]{session_detail::decode_ability_correction(session_detail::encode_ability_correction(invalid));});invalid=correction;invalid.ability.attributes[1].id=invalid.ability.attributes[0].id;rejects([&]{session_detail::decode_ability_correction(session_detail::encode_ability_correction(invalid));});invalid=correction;invalid.motor.tick=9;rejects([&]{session_detail::decode_ability_correction(session_detail::encode_ability_correction(invalid));});}
}
int main(){try{bundle();fragments();timeout();privacy();codec();std::cout<<"Atomic owner ability/motor correction fragments, exact operation inclusion, ACK retirement, privacy and codec validation passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
