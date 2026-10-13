#include <darkangel/scene_authority.hpp>
#include <darkangel/scene_client.hpp>
#include <darkangel/combat_kit_assets.hpp>
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// External delivery clock, independent of WorldSession's pump count. Gameplay still uses the real wire codec.
class Delayed final:public Transport {
    Transport& inner;std::uint64_t& now;
    struct Pending {std::uint64_t due;TransportMessage message;};std::deque<Pending> pending;
public:
    unsigned delay{},drop_every{},state_packets{},dropped{};bool hold{};
    Delayed(Transport& transport,std::uint64_t& tick):inner(transport),now(tick){}
    ConnectionHandle open(const SessionHandshake& h)override{return inner.open(h);}
    bool connected()const override{return inner.connected();}bool valid(ConnectionHandle h)const override{return inner.valid(h);}
    TransportLimits limits()const override{return inner.limits();}
    bool send(ConnectionHandle h,Delivery d,std::span<const std::byte> b)override{return inner.send(h,d,b);}
    std::vector<TransportMessage> poll(std::size_t count)override{
        for(auto& message:inner.poll(count)){if(message.delivery==Delivery::UnreliableState&&drop_every&&++state_packets%drop_every==0){++dropped;continue;}pending.push_back({now+delay,std::move(message)});}
        check(pending.size()<4096,"Delayed test queue exceeded its bound");std::vector<TransportMessage> out;
        while(!hold&&!pending.empty()&&pending.front().due<=now&&out.size()<count){out.push_back(std::move(pending.front().message));pending.pop_front();}return out;
    }
    void disconnect(ConnectionHandle h)override{inner.disconnect(h);pending.clear();}
};
}
int main(){try{
    const auto content=std::filesystem::path(DAE_SOURCE_DIR)/"content",cache=std::filesystem::path(DAE_BINARY_DIR)/("scene-delay-"+AssetId::random().text());
    std::ifstream source(content/"royal_district/combat/player.dakit");auto data=nlohmann::json::parse(source);auto id=AssetId::parse(data.at("asset").get<std::string>());
    AssetService assets(content,cache);assets.cook("royal_district/combat/player.dakit");assets.package(id,cache/"registry.json");auto cooked=load_cooked_combat_kit(cache/"registry.json",assets.cas_path(),id);
    auto configured=std::make_shared<CharacterSceneCombat>();auto& combat=*configured;combat.kit=cooked.definition;combat.input=cooked.input;combat.attributes=cooked.attributes.definitions();combat.health=2;combat.maximum_health=1;combat.abilities=cooked.abilities;if(cooked.tags)combat.tags=cooked.tags->dictionary();
    combat.evaluator=1;combat.damage=[](const DamageContext& context){return context.power;};
    for(const auto& ability:cooked.abilities){auto clip=load_cooked_clip(cache/"registry.json",assets.cas_path(),ability->action->motion->clip.id);combat.clips.emplace(clip.definition.id,std::make_shared<const AnimationClip>(clip.definition,clip.archive));}
    const StableId one{19,1},two{19,2};ObjectData player;player.id=one;player.transform.yaw=.7;
    CollisionDefinition collision;collision.id=AssetId::random();collision.schema=2;collision.boxes={{100,{0,-.5,0},{32,.5,32}},{101,{20,1,0},{1,1,4}}};
    SessionHandshake hello{3,sha256("independent scene wire3"),sha256(cooked.definition->generation),1801};
    std::uint64_t delivery_tick{};auto a=create_loopback(hello),b=create_loopback(hello);Delayed ah(*a.host,delivery_tick),ac(*a.client,delivery_tick),bh(*b.host,delivery_tick),bc(*b.client,delivery_tick);
    auto ap=ah.open(hello),aq=ac.open(hello),bp=bh.open(hello),bq=bc.open(hello);
    const std::array<ObjectData,1> objects{player};const std::array<StableId,2> party{one,two};const std::array<SceneSeat,2> seats{{{one,&ah,ap,{0,0,0},.7},{two,&bh,bp,{4,0,0},.7}}};
    SceneAuthority host(hello,objects,seats,collision,configured,{},SceneTiming::Independent);
    const auto& rig=cooked.stance.rig;SceneClient first(hello,ac,aq,one,party,collision,cooked.stance.plan,rig.definition,rig.archive,configured,SceneTiming::Independent);
    SceneClient second(hello,bc,bq,two,party,collision,cooked.stance.plan,rig.definition,rig.archive,configured,SceneTiming::Independent);
    auto pump=[&]{for(unsigned n=0;n<8;++n){host.pump();first.receive();second.receive();}};
    pump();check(first.bootstrapped()&&second.bootstrapped(),"Independent bootstrap did not become ready");first.start();second.start();
    ah.delay=2;ac.delay=2;bh.delay=1;bc.delay=3;
    std::size_t pending{};bool older{},saw_other{};
    for(unsigned n=0;n<120;++n){++delivery_tick;CharacterSceneInput input;input.x=n<65?.5:0;input.yaw=.7;
        if(n==20)input.combat_events={{8,InputEdge::Pressed,1000,0,1,false}};
        if(n==21)input.combat_events={{8,InputEdge::Released,2000,0,0,false},{8,InputEdge::Tapped,2000,0,1,false}};
        first.advance(1./60,input);second.advance(1./60,{});host.step(host.tick()+1);pump();
        pending=std::max(pending,first.pending_prediction());older|=first.ability()&&first.ability()->tick<first.predicted_motor().tick;saw_other|=first.pose(two)&&second.pose(one);
        check(first.tick()==first.predicted_motor().tick&&first.graph().tick==first.tick(),"Independent motor/presentation clocks disagree");
    }
    check(older&&pending>1&&pending<=8&&saw_other,"Delayed corrections did not retain newer input or present the other client");
    std::cout<<"Delayed progress host="<<host.tick()<<" clients="<<first.tick()<<','<<second.tick()<<" lead="<<first.input_lead()<<" pending="<<pending<<" rebases="<<first.resynchronizations()<<','<<second.resynchronizations()<<" x="<<host.motor(one)->position.x<<" sequence="<<host.motor(one)->sequence<<'\n';
    check(first.tick()>100&&second.tick()>100&&first.input_lead()<=6&&host.motor(one)->position.x>.5&&first.resynchronizations()==0&&second.resynchronizations()==0,"Delayed client did not keep simulating bounded movement without rebasing");
    check(first.ability()->highest_operation>=2,"Delayed combat inputs did not reach authority");
    // A missing command holds movement and facing; it must not repeat a one-shot jump.
    CharacterSceneInput jump;jump.x=.5;jump.yaw=1.2;jump.jump=true;first.advance(1./60,jump);
    for(unsigned n=0;n<10;++n){++delivery_tick;host.step(host.tick()+1);pump();}
    ah.hold=true;const auto before=*host.motor(one);for(unsigned n=0;n<6;++n){++delivery_tick;host.step(host.tick()+1);pump();}
    check(before.yaw==1.2&&host.motor(one)->sequence==before.sequence&&host.motor(one)->yaw==before.yaw&&host.motor(one)->desired.x>0&&host.motor(one)->jump_buffer==0,"Missing owner input lost movement/sequence/facing or repeated jump");ah.hold=false;
    for(unsigned n=0;n<20;++n){++delivery_tick;first.advance(1./60,{});second.advance(1./60,{});host.step(host.tick()+1);pump();}
    // A client paused for longer than the history window rebases from the correction, not the authored transform.
    const auto rebuilt=first.resynchronizations();ac.hold=true;
    for(unsigned n=0;n<40;++n){++delivery_tick;host.step(host.tick()+1);host.pump();first.receive();second.receive();}
    ac.hold=false;for(unsigned n=0;n<12;++n){++delivery_tick;pump();first.advance(0,{});second.advance(0,{});}
    check(first.resynchronizations()>rebuilt&&first.tick()>=host.tick(),"Client behind the history window did not rebase and catch up");
    check(std::abs(first.predicted_motor().position.x-host.motor(one)->position.x)<.01&&std::abs(first.predicted_motor().position.y-host.motor(one)->position.y)<.01,"Late rebase restored an old object transform");
    // A scene topology change rebuilds prediction while the client is ahead.
    const auto topology_rebuild=first.resynchronizations();host.remove_collision(101);
    for(unsigned n=0;n<20;++n){++delivery_tick;host.step(host.tick()+1);pump();first.advance(1./60,{});second.advance(1./60,{});}
    check(first.resynchronizations()>topology_rebuild&&first.predicted_motor().topology==host.motor(one)->topology,"Delayed topology replacement did not rebase prediction");
    // Delivery of an ability after its requested tick is a terminal refusal, followed by a coherent client baseline.
    ah.hold=true;const auto late_operation=first.ability()->highest_operation+1;CharacterSceneInput press;press.combat_events={{8,InputEdge::Pressed,3000,0,1,false}};first.advance(1./60,press);
    for(unsigned n=0;n<12;++n){++delivery_tick;host.step(host.tick()+1);pump();}ah.hold=false;bool expired{};
    for(unsigned n=0;n<20;++n){++delivery_tick;host.step(host.tick()+1);pump();for(const auto& operation:first.ability()->operations)expired|=operation.operation==late_operation&&operation.failure==AbilityFailure::InputExpired;first.advance(0,{});second.advance(0,{});}
    check(expired&&first.ability()->highest_operation>=late_operation,"Late ability was not refused with InputExpired");
    // Drop a bounded fraction of whole unreliable packets; reliable requests and answers remain ordered.
    ac.drop_every=9;bc.drop_every=11;for(unsigned n=0;n<60;++n){++delivery_tick;first.advance(1./60,{});second.advance(1./60,{});host.step(host.tick()+1);pump();}
    check(ac.dropped>0&&bc.dropped>0&&first.tick()+8>=host.tick()&&second.tick()+8>=host.tick()&&first.pending_prediction()<=8,"Clients did not recover from dropped unreliable state packets");
    // Late bootstrap: the object was received while airborne, but start uses the newest completed motor tick.
    {auto late_hello=hello;late_hello.session_epoch++;auto link=create_loopback(late_hello);auto hp=link.host->open(late_hello),cp=link.client->open(late_hello);auto falling=player;falling.transform.y=4;
        const std::array<ObjectData,1> scene{falling};const std::array<StableId,1> roster{one};const std::array<SceneSeat,1> seat{{{one,link.host.get(),hp}}};
        SceneAuthority late_host(late_hello,scene,seat,collision,configured,{},SceneTiming::Independent);
        SceneClient late(late_hello,*link.client,cp,one,roster,collision,cooked.stance.plan,rig.definition,rig.archive,configured,SceneTiming::Independent);
        for(unsigned n=0;n<8;++n){late_host.pump();late.receive();}for(unsigned n=0;n<60;++n){late_host.step(late_host.tick()+1);for(unsigned p=0;p<4;++p){late_host.pump();late.receive();}}
        check(late.bootstrapped(),"Late scene bootstrap was unavailable");late.start();check(late.tick()==60&&late.predicted_motor().position==late_host.motor(one)->position&&late.graph().tick==60&&late.predicted_motor().position.y<.01,"Late bootstrap did not use the correction's tick and grounded state");late.advance(0,{});
    }
    std::cout<<"Independent scene delay, retained inputs, combat, observers, host fallback, history rebase and topology passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Independent scene failure: "<<e.what()<<'\n';return 1;}}
