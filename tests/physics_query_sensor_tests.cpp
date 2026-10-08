#include <darkangel/character_motor.hpp>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace darkangel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
CollisionBox zone(std::uint64_t id=5){CollisionBox box;box.id=id;box.center={0,1,0};box.half={2,1,2};box.sensor=true;return box;}
void floor(PhysicsWorld& world){world.add({1,{0,-.5,0},{20,.5,20}});}
void tick(PhysicsWorld& world,CharacterMotor& motor,double x=0){auto n=world.tick()+1;motor.step({n,n,motor.state().epoch,x});world.step();motor.post_physics();world.finish_tick();}
}
int main(){try{
    PhysicsWorld world;floor(world);world.add({2,{3,1,0},{.1,1,2}});world.add(zone());
    CharacterMotor owner(world,{0,0,0},10),peer(world,{1.5,0,0},20);
    auto ray=world.query_ray({0,.8,0},{5,0,0},{CharacterCollision,10});
    require(!ray.overflow&&ray.hits.size()==1&&ray.hits[0].identity==20&&ray.hits[0].character&&world.valid_hit(ray.hits[0]),"Ray filters owner and returns a qualified character hit");
    require(ray.hits[0].fraction>.1&&ray.hits[0].fraction<.4&&ray.hits[0].normal.x<-.9,"Ray fraction and outward normal");
    PhysicsWorld foreign;floor(foreign);require(!foreign.valid_hit(ray.hits[0]),"Cross-world hit cannot resolve through a matching identity");
    const auto prior=ray.hits[0];require(peer.teleport({6,0,0})&&!world.valid_hit(prior),"Teleport epoch invalidates a prior character hit");
    auto blockers=world.query_ray({0,1,0},{5,0,0},{StaticCollision});
    require(blockers.hits.size()==1&&blockers.hits[0].identity==2&&!blockers.hits[0].character&&std::abs(blockers.hits[0].point.x-2.9)<.0001,"Static-only closest ray returns world-space surface point");
    const auto stale=blockers.hits[0];world.remove(2);world.add({2,{3,1,0},{.1,1,2}});require(!world.valid_hit(stale),"Recreated body identity cannot revive an old hit");
    const auto safe=owner.state();auto invalid=safe;invalid.topology=world.topology();invalid.position={3,0,0};bool unsafe_restore=false;try{owner.restore(invalid);}catch(...){unsafe_restore=true;}
    require(unsafe_restore&&owner.state().position==safe.position&&owner.state().crouched==safe.crouched,"Invalid correction rejects before changing live motor shape or pose");
    require(world.overlap({0,1,0},.2,16,{SensorCollision}).hits.empty(),"Sensors require explicit query admission");
    auto triggers=world.overlap({0,1,0},.2,16,{SensorCollision,0,true});require(triggers.hits.size()==1&&triggers.hits[0].sensor&&triggers.hits[0].identity==5,"Sensor query layer is explicit");
    auto sweep=world.sweep({0,1,0},{5,0,0},.1,16,{StaticCollision});require(sweep.hits.size()==1&&sweep.hits[0].identity==2&&sweep.hits[0].normal.x<-.9,"Sweep obeys layers and reports normal");
    // Equal-distance selection uses logical identity, independently of insertion.
    PhysicsWorld tied;tied.add({8,{2,1,0},{.1,1,1}});tied.add({7,{2,1,0},{.1,1,1}});
    require(tied.query_ray({0,1,0},{3,0,0}).hits.front().identity==7,"Closest equal-fraction ray uses stable identity ordering");
    PhysicsWorld contacts;floor(contacts);contacts.add(zone());auto character=std::make_unique<CharacterMotor>(contacts,MotorVec{0,0,0},10);
    bool wrong_phase=false;try{contacts.finish_tick();}catch(...){wrong_phase=true;}require(wrong_phase,"Sensor events cannot run before physics");
    character->step({1,1,1});contacts.step();wrong_phase=false;try{contacts.finish_tick();}catch(...){wrong_phase=true;}require(wrong_phase,"Sensor events require motor contact refresh");character->post_physics();contacts.finish_tick();
    auto state_before=character->state();bool backpressure=false;try{character->step({2,2,1});}catch(...){backpressure=true;}require(backpressure&&character->state().tick==state_before.tick&&character->state().position==state_before.position,"Unconsumed events block the next motor tick before mutation");
    auto events=contacts.take_sensor_events();require(events.size()==1&&events[0].phase==SensorPhase::Begin&&events[0].other==10&&events[0].character,"One copied sensor begin after completed tick");auto token=events[0].token;
    contacts.finish_tick();require(contacts.take_sensor_events().empty(),"Repeated finish cannot duplicate entry");
    for(unsigned n=0;n<10;++n){tick(contacts,*character);require(contacts.take_sensor_events().empty(),"Persisting contact does not synthesize repeated enters/exits");}
    require(character->teleport({5,0,0}),"Safe outside sensor teleport");tick(contacts,*character);events=contacts.take_sensor_events();require(events.size()==1&&events[0].phase==SensorPhase::End&&events[0].token==token,"Exit balances original ownership token");
    require(character->teleport({0,0,0}),"Safe inside sensor teleport");tick(contacts,*character);events=contacts.take_sensor_events();require(events.size()==1&&events[0].phase==SensorPhase::Begin&&events[0].token!=token,"Reentry owns a new token");token=events[0].token;
    contacts.add(zone(6));tick(contacts,*character);events=contacts.take_sensor_events();require(events.size()==1&&events[0].sensor==6&&events[0].phase==SensorPhase::Begin,"Overlapping sensor has independent ownership");auto second_token=events[0].token;
    contacts.remove(5);tick(contacts,*character);events=contacts.take_sensor_events();require(events.size()==1&&events[0].phase==SensorPhase::End&&events[0].token==token,"Sensor removal ends only its owned contact");
    CollisionBox crate;crate.id=30;crate.center={1,.15,0};crate.half={.1,.1,.1};crate.dynamic=true;contacts.add(crate);
    tick(contacts,*character);events=contacts.take_sensor_events();require(events.size()==1&&!events[0].character&&events[0].other==30&&events[0].phase==SensorPhase::Begin,"Rigid and virtual recipients share copied sensor lifecycle");
    for(unsigned n=0;n<240;++n){tick(contacts,*character);require(contacts.take_sensor_events().empty(),"Rigid body sleep is not a sensor exit");}require(contacts.sleeping(30),"Sleep case actually reaches a sleeping dynamic body");
    character.reset();character=std::make_unique<CharacterMotor>(contacts,MotorVec{0,0,0},10);tick(contacts,*character);events=contacts.take_sensor_events();
    require(events.size()==2&&events[0].phase==SensorPhase::End&&events[0].token==second_token&&events[1].phase==SensorPhase::Begin&&events[0].other_generation!=events[1].other_generation,"Same-ID replacement ends old generation before new entry");
    PhysicsWorld prediction(PhysicsWorld::Mode::Prediction);floor(prediction);prediction.add(zone());CharacterMotor predicted(prediction,{0,0,0});tick(prediction,predicted);require(prediction.take_sensor_events().empty(),"Prediction cannot emit authoritative sensor events");
    PhysicsWorld crowded;crowded.add(zone());for(unsigned id=20;id<60;++id){CollisionBox body;body.id=id;body.center={0,1,0};body.half={.1,.1,.1};body.moving=true;crowded.add(body);}crowded.step();
    bool overflow=false;try{crowded.finish_tick();}catch(...){overflow=true;}require(overflow&&crowded.needs_resync()&&crowded.take_sensor_events().empty(),"Sensor overflow rejects the complete batch and requests resynchronization");
    PhysicsWorld lifetimes;for(unsigned id=1;id<=5;++id)lifetimes.add(zone(id));for(unsigned id=20;id<48;++id){CollisionBox body;body.id=id;body.center={0,1,0};body.half={.1,.1,.1};body.moving=true;lifetimes.add(body);}lifetimes.step();overflow=false;try{lifetimes.finish_tick();}catch(...){overflow=true;}require(overflow&&lifetimes.needs_resync()&&lifetimes.take_sensor_events().empty(),"Lifetime budget overflow never publishes a prefix of owned events");
    PhysicsWorld ray_budget;for(unsigned id=1;id<=40;++id)ray_budget.add({id,{double(id),1,0},{.1,.1,.1}});auto bounded=ray_budget.query_ray({0,1,0},{50,0,0});require(bounded.overflow&&bounded.hits.empty(),"Ray overflow cannot masquerade as a closest gameplay hit");
    PhysicsWorld low;floor(low);low.add({2,{0,1.4,0},{5,.1,5}});CharacterMotor crouched(low,{0,0,0},10,true);
    for(unsigned n=1;n<=4;++n){crouched.step({n,n,1,0,0,0,false,true});low.step();crouched.post_physics();}
    auto baseline=crouched.state();CollisionHistory history;std::vector<MotorInput> inputs;
    for(unsigned n=5;n<=10;++n){history.retain(low.capture());MotorInput input{n,n,1,.1,0,0,false,true};inputs.push_back(input);crouched.step(input);low.step();crouched.post_physics();}
    MotorState replayed;require(replay_motor(baseline,inputs,history,replayed,10)==ReplayResult::Applied&&replayed.crouched&&std::abs(replayed.position.x-crouched.state().position.x)<.0001,"Crouched baseline replays beneath a standing obstruction");
    PhysicsWorld vertical;floor(vertical);CharacterMotor lifted(vertical,{0,0,0});
    MotionRequest pulse;pulse.root={0,.1,0};pulse.lock=true;lifted.step({1,1,1},pulse);vertical.step();lifted.post_physics();const auto height=lifted.state().position.y;
    lifted.step({2,2,1});vertical.step();lifted.post_physics();require(lifted.state().position.y<height+.001&&lifted.state().momentum.y<1,"Vertical root pulse cannot leave launch velocity behind");
    PhysicsWorld departing;departing.add({1,{0,0,0},{5,.25,5},{3,1,0},{},0,0,true});CharacterMotor jumper(departing,{0,.25,0});
    for(unsigned n=1;n<=5;++n){jumper.step({n,n,1});departing.step();jumper.post_physics();}
    jumper.step({6,6,1,0,0,0,true});departing.step();jumper.post_physics();require(!jumper.state().support&&jumper.state().momentum.x>2.5&&jumper.state().momentum.y>7,"Jump inherits horizontal and vertical support velocity once");
    jumper.step({7,7,1});departing.step();jumper.post_physics();require(jumper.state().momentum.x>2&&jumper.state().momentum.x<3&&jumper.state().momentum.y<8,"Inherited support follows defined braking/gravity without double carry");
    PhysicsWorld stopped;stopped.add({1,{0,0,0},{5,.25,5},{0,1,0},{},0,0,true});CharacterMotor rider(stopped,{0,.25,0});
    for(unsigned n=1;n<=5;++n){rider.step({n,n,1});stopped.step();rider.post_physics();}stopped.set_platform(1,{});
    rider.step({6,6,1});stopped.step();rider.post_physics();require(rider.state().grounded&&std::abs(rider.state().momentum.y)<.1,"Attached platform stop is not an accidental launch");
    PhysicsWorld ceiling;floor(ceiling);ceiling.add({2,{0,2.3,0},{5,.1,5}});CharacterMotor launched(ceiling,{0,0,0});MotionRequest kick;kick.impulse={0,10,0};
    launched.step({1,1,1},kick);ceiling.step();launched.post_physics();for(unsigned n=2;n<=10;++n){launched.step({n,n,1});ceiling.step();launched.post_physics();}
    require(launched.state().momentum.y<=0&&launched.state().position.y<.4,"Ceiling obstruction absorbs native upward impulse");
    std::cout<<"Qualified layer/owner/sensor queries, normals, stable ties, world/generation/epoch invalidation, balanced sensor tokens, sleep, replacement and atomic overflow passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
