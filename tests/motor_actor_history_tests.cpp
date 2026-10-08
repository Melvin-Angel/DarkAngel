#include <darkangel/character_motor.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void floor(PhysicsWorld& world){world.add({1,{0,-.5,0},{20,.5,20}});}
void tick(PhysicsWorld& world,CharacterMotor& owner,CharacterMotor& peer,bool moving,bool crouching){
    const auto t=world.tick()+1;
    owner.step({t,t,1,1});
    peer.step({t,t,1,moving?-.1:0,0,0,false,crouching});
    world.step();owner.post_physics();peer.post_physics();
}
void trace(bool moving,bool crouching){
    PhysicsWorld world;floor(world);
    CharacterMotor owner(world,{0,0,0},10),peer(world,{1.1,0,0},20);
    require(owner.identity()==10&&peer.identity()==20,"Stable logical actor identities");
    tick(world,owner,peer,moving,crouching);
    const auto baseline=owner.state();
    CollisionHistory history,missing,discontinuous,without_peer,missing_peer;
    std::vector<MotorInput> inputs;
    for(unsigned n=0;n<24;++n){
        auto frame=world.capture();
        require(frame.actors.size()==2&&frame.actors[1].id==20&&frame.actors[1].crouched==crouching,"Retained actor pose and stance");
        history.retain(frame);
        auto partial=frame;if(n)partial.actors.pop_back();missing_peer.retain(partial);
        auto unblocked=frame;unblocked.actors.pop_back();without_peer.retain(unblocked);
        auto absent=frame;absent.actors.erase(absent.actors.begin());missing.retain(absent);
        auto epoch=frame;epoch.actors[0].epoch++;discontinuous.retain(epoch);
        inputs.push_back({world.tick()+1,world.tick()+1,1,1});
        tick(world,owner,peer,moving,crouching);
    }
    const auto live=world.capture();
    MotorState output;output.position.x=999;
    require(replay_motor(baseline,inputs,history,output)==ReplayResult::Invalid&&output.position.x==999,"Ambiguous owner rejects atomically");
    require(replay_motor(baseline,inputs,missing,output,10)==ReplayResult::MissingHistory&&output.position.x==999,"Missing owner proxy requires resynchronization");
    require(replay_motor(baseline,inputs,missing_peer,output,10)==ReplayResult::MissingHistory&&output.position.x==999,"Incomplete peer frame requires resynchronization");
    require(replay_motor(baseline,inputs,discontinuous,output,10)==ReplayResult::Discontinuity&&output.position.x==999,"Historical owner epoch rejects atomically");
    require(replay_motor(baseline,inputs,history,output,10)==ReplayResult::Applied,"Actor proxy replay applies");
    std::cout<<"Actor replay moving="<<moving<<" crouched="<<crouching<<" live_x="<<owner.state().position.x<<" replay_x="<<output.position.x<<'\n';
    require(std::abs(output.position.x-owner.state().position.x)<.02&&peer.state().position.x-output.position.x>.5,"Replay preserves character separation and matches original motor");
    MotorState unblocked;
    require(replay_motor(baseline,inputs,without_peer,unblocked,10)==ReplayResult::Applied&&unblocked.position.x>output.position.x+.025,"Retained peer collision changes replay outcome");
    auto after=world.capture();
    require(after.tick==live.tick&&after.topology==live.topology&&after.actors[0].foot==live.actors[0].foot&&after.actors[1].foot==live.actors[1].foot&&after.actors[1].velocity==live.actors[1].velocity,"Replay cannot rewind or push live actors");
    require(!owner.teleport(peer.state().position),"Teleport validates peer clearance");
    bool overlap=false,duplicate=false;
    try{CharacterMotor bad(world,peer.state().position,30);}catch(...){overlap=true;}
    try{CharacterMotor bad(world,{10,0,0},20);}catch(...){duplicate=true;}
    require(overlap&&duplicate&&world.capture().actors.size()==2,"Invalid spawn and duplicate identity preserve registry");
    auto query=world.overlap(peer.state().position,.2);
    unsigned count=0;for(auto hit:query.hits)if(hit.character&&hit.identity==20)++count;
    require(count==1,"A peer has one query representation");
}
}
int main(){try{
    trace(false,false);trace(true,false);trace(false,true);trace(true,true);
    PhysicsWorld world;floor(world);CharacterMotor owner(world,{0,0,0},10);
    auto peer=std::make_unique<CharacterMotor>(world,MotorVec{2,0,0},20);
    tick(world,owner,*peer,false,false);
    const auto baseline=owner.state();CollisionHistory history;history.retain(world.capture());
    const auto tick1=world.tick()+1;MotorInput first{tick1,tick1,1,1};
    tick(world,owner,*peer,false,false);peer.reset();history.retain(world.capture());
    const auto tick2=world.tick()+1;MotorInput second{tick2,tick2,1,1};
    MotorState sentinel;sentinel.position.x=999;
    const MotorInput commands[]={first,second};
    require(replay_motor(baseline,commands,history,sentinel,10)==ReplayResult::TopologyMismatch&&sentinel.position.x==999,"Character removal changes topology and fails bounded replay");
    // The owner helper supplies its identity rather than relying on inference.
    PhysicsWorld predicted;floor(predicted);CharacterMotor local(predicted,{0,0,0},10),remote(predicted,{1.1,0,0},20);
    OwnerPrediction prediction(predicted,local);MotorState correction;
    for(unsigned n=1;n<=20;++n){prediction.predict({n,n,1,1});remote.step({n,n,1});predicted.step();local.post_physics();remote.post_physics();if(n==10)correction=local.state();}
    const auto expected=local.state().position.x;
    require(prediction.reconcile(correction)==ReplayResult::Applied&&prediction.pending()==10&&std::abs(local.state().position.x-expected)<.02,"Owner reconciliation uses isolated peer history");
    std::cout<<"Historical actor identity, stance, collision, removal and owner reconciliation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
