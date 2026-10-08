#include <darkangel/character_motor.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
CollisionBox crate(){CollisionBox box;box.id=2;box.center={1.5,.26,0};box.half={.25,.25,.25};box.dynamic=true;return box;}
void floor(PhysicsWorld& world){world.add({1,{0,-.5,0},{20,.5,20}});}
void tick(PhysicsWorld& world,CharacterMotor& motor,double x){auto n=world.tick()+1;motor.step({n,n,1,x});world.step();motor.post_physics();}
}
int main(){try{
    PhysicsWorld live;floor(live);live.add(crate());CharacterMotor owner(live,{0,0,0},10);
    for(unsigned n=0;n<20;++n)tick(live,owner,0);
    const auto baseline=owner.state();CollisionHistory history,incomplete;std::vector<MotorInput> commands;
    for(unsigned n=0;n<30;++n){auto frame=live.capture();history.retain(frame);incomplete.retain(frame);commands.push_back({live.tick()+1,live.tick()+1,1,1});tick(live,owner,1);}
    history.retain(live.capture());const auto before=live.capture();
    require(before.boxes[1].center.x>1.6&&std::abs(before.boxes[1].velocity.x)<30,"Server motor pushes a bounded dynamic crate");
    MotorState replayed;const auto result=replay_motor(baseline,commands,history,replayed,10);
    std::cout<<"Dynamic live owner_x="<<owner.state().position.x<<" crate_x="<<before.boxes[1].center.x<<" replay_result="<<int(result)<<" replay_x="<<replayed.position.x<<'\n';
    require(result==ReplayResult::Applied,"Dynamic authoritative-proxy replay applies");
    require(std::abs(replayed.position.x-owner.state().position.x)<.02,"Authoritative dynamic trajectory replay agrees within 2 cm");
    MotorState repeat;require(replay_motor(baseline,commands,history,repeat,10)==ReplayResult::Applied&&std::abs(repeat.position.x-replayed.position.x)<.000001,"Repeated proxy replay is consistent");
    auto after=live.capture();require(after.tick==before.tick&&after.boxes[1].center==before.boxes[1].center&&after.boxes[1].velocity==before.boxes[1].velocity&&after.boxes[1].angular==before.boxes[1].angular,"Repeated replay cannot reapply a live prop impulse");
    MotorState sentinel;sentinel.position.x=999;require(replay_motor(baseline,commands,incomplete,sentinel,10)==ReplayResult::MissingHistory&&sentinel.position.x==999,"Missing dynamic completed-tick history rejects atomically");
    PhysicsWorld predicted(PhysicsWorld::Mode::Prediction);floor(predicted);auto stationary=crate();stationary.center={.7,.25,0};predicted.add(stationary);CharacterMotor local(predicted,{0,0,0},10);OwnerPrediction prediction(predicted,local);
    const auto proxy_start=predicted.capture().boxes[1].center;MotorState correction;
    for(unsigned n=1;n<=15;++n){prediction.predict({n,n,1,1});predicted.step();local.post_physics();if(n==8)correction=local.state();}
    require(predicted.capture().boxes[1].center==proxy_start,"Owner motor cannot push an authoritative prop proxy");
    require(prediction.reconcile(correction)==ReplayResult::Applied,"Owner dynamic proxy reconciliation succeeds");
    prediction.predict({16,16,1,1});predicted.step();local.post_physics();
    require(predicted.capture().boxes[1].center==proxy_start,"Prediction continues after completed-tick history capture");
    bool denied=false;try{predicted.apply_impulse(2,{1,0,0});}catch(...){denied=true;}require(denied,"Client prop impulse denied");
    const auto velocity=live.capture().boxes[1].velocity;bool excessive=false;try{live.apply_impulse(2,{501,0,0});}catch(...){excessive=true;}require(excessive&&live.capture().boxes[1].velocity==velocity,"Impulse budget rejects before mutation");
    live.apply_impulse(2,{10,0,0});require(live.capture().boxes[1].velocity.x>velocity.x,"Server impulse uses native dynamic body");
    PhysicsWorld rotated(PhysicsWorld::Mode::Prediction);auto tilted=crate();tilted.center={0,2,0};tilted.rotation={std::sin(.3),0,0,std::cos(.3)};tilted.angular={1,.2,.3};rotated.add(tilted);rotated.step();auto frame=rotated.capture();
    PhysicsWorld restored(PhysicsWorld::Mode::Replay);restored.load(frame);auto copy=restored.capture();
    require(std::abs(copy.boxes[0].rotation[0]-frame.boxes[0].rotation[0])<.000001&&copy.boxes[0].angular==frame.boxes[0].angular,"Historical proxies preserve pitch/roll quaternion and angular velocity");
    bool misuse=false;try{OwnerPrediction bad(live,owner);bad.predict({51,51,1});}catch(...){misuse=true;}require(misuse,"Owner prediction refuses a live authoritative dynamic world");
    std::cout<<"Dynamic authority, proxy history, impulse exclusion, completed-tick bounds and full rotation passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
