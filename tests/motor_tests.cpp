#include <darkangel/character_motor.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/collision_asset.hpp>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void require(bool b,const char* e){
    if(!b)throw std::runtime_error(e);
}
void floor(PhysicsWorld& w){
    w.add({
        1,{
            0,-.5,0
        },{
            50,.5,50
        }
    });
}
MotorState run(PhysicsWorld& w,CharacterMotor& c,unsigned count,double x=0,double z=0,bool crouch=false){
    MotorState s=c.state();
    for(unsigned n=0;n<count;++n){
        s=c.step({
            s.sequence+1,w.tick()+1,s.epoch,x,z,0,false,crouch
        });
        w.step();
        c.post_physics();
        s=c.state();
    }return s;
}
int hut_jump_probe(const char* path){
 std::ifstream input(path);require(bool(input),"Open hut collision source");std::string bytes{std::istreambuf_iterator<char>(input),{}};auto collision=decode_collision_source(bytes);unsigned failures=0,completed=0;
 const MotorVec starts[]={{-7,.51,2},{-10,.51,2},{-4,.51,2},{7,.51,2},{3,.51,2},{11,.51,2},{-7,.51,-10},{7,.51,-10},{-12,.51,-4},{-2,.51,-4},{1,.51,-4},{13,.51,-4}};
 for(unsigned n=0;n<std::size(starts);++n){PhysicsWorld world;world.load_scene(collision);CharacterMotor motor(world,starts[n],10);auto target=n<9?MotorVec{-7,0,-4}:MotorVec{7,0,-4};if(n>=3&&n<6||n==7)target={7,0,-4};auto delta=MotorVec{target.x-starts[n].x,0,target.z-starts[n].z};auto length=std::hypot(delta.x,delta.z);delta.x/=length;delta.z/=length;
  CollisionHistory history;std::vector<MotorInput> commands;auto baseline=motor.state();
  for(unsigned frame=0;frame<360;++frame){
   auto before=motor.state();auto tick=world.tick()+1;
   history.retain(world.capture());MotorInput command{tick,tick,1,delta.x,delta.z,0,frame%3==0,false};commands.push_back(command);
   try{
    motor.step(command);world.step();motor.post_physics();
    if(commands.size()==15){
     auto live=motor.state();MotorState replayed;
     require(replay_motor(baseline,commands,history,replayed,10)==ReplayResult::Applied,"Hut isolated replay failed");
     require(std::abs(replayed.position.x-live.position.x)<1e-5&&std::abs(replayed.position.y-live.position.y)<1e-5&&std::abs(replayed.position.z-live.position.z)<1e-5&&replayed.grounded==live.grounded,"Hut live/replay mismatch");
     require(world.tick()==tick&&motor.state().position==live.position,"Hut replay changed live world");baseline=live;commands.clear();
    }
   }catch(const std::exception& error){if(std::string_view(error.what()).find("Motor collision work overflow")==std::string_view::npos)throw;++failures;std::cout<<"Hut jump overflow case="<<n<<" tick="<<tick<<" previous_foot="<<before.position.x<<","<<before.position.y<<","<<before.position.z<<" jump="<<(frame%3==0)<<" input="<<delta.x<<","<<delta.z<<"\n";break;}
   if(frame==359)++completed;
  }
 }
 std::cout<<"Hut jump probe completed="<<completed<<" overflow="<<failures<<" cases="<<std::size(starts)<<"\n";return failures?2:0;
}
int main(int argc,char** argv){
    try{
        if(argc==3&&std::string_view(argv[1])=="--hut-jump")return hut_jump_probe(argv[2]);
        PhysicsWorld w;
        floor(w);
        w.add({
            2,{
                3,1,0
            },{
                .05,1,2
            }
        });
        CharacterMotor c(w,{
            0,.05,0
        });
        run(w,c,20);
        auto s=run(w,c,60,1);
        require(s.position.x<2.75&&s.grounded,"Thin wall obstruction");
        auto before=s.position;
        MotionRequest root;
        root.root={
            1,0,0
        };
        root.lock=true;
        root.action=44;
        s=c.step({
            s.sequence+1,w.tick()+1,s.epoch
        },root);
        w.step();
        require(s.achieved.x<.1,"Collision-constrained root");
        run(w,c,10,0,0);
        require(c.state().position.x<2.75,"No hidden blocked root displacement");
        require(!c.teleport({
            3,.1,0
        }),"Teleport rejects wall overlap");
        require(c.teleport({
            -4,.1,0
        })&&c.state().epoch==2,"Validated teleport epoch");
        run(w,c,20);
        s=c.step({
            c.state().sequence+1,w.tick()+1,2,0,0,0,true
        });
        w.step();
        require(s.position.y>.08&&!s.grounded,"Jump detaches and disables adhesion");
        MotionRequest impulse;
        impulse.impulse={
            3,2,0
        };
        auto old=s.position;
        s=c.step({
            s.sequence+1,w.tick()+1,2
        },impulse);
        w.step();
        require(s.position.x>old.x,"Impulse movement");
        std::uint64_t hit{
        };
        require(w.ray({
            s.position.x,3,s.position.z
        },{
            0,-4,0
        },hit),"Query-visible character proxy");
        PhysicsWorld ceiling;
        floor(ceiling);
        ceiling.add({
            2,{
                0,1.4,0
            },{
                2,.1,2
            }
        });
        CharacterMotor low(ceiling,{
            3,.05,0
        });
        run(ceiling,low,1,0,0,true);
        require(low.teleport({
            0,.05,0
        }),"Low stance clear teleport");
        // Teleport preserves the validated low stance.
        PhysicsWorld stance;
        floor(stance);
        CharacterMotor crouched(stance,{
            0,.05,0
        });
        run(stance,crouched,5,0,0,true);
        stance.add({
            2,{
                0,1.4,0
            },{
                2,.1,2
            }
        });
        s=run(stance,crouched,5);
        require(s.crouched,"Blocked standing retains low stance");
        stance.remove(2);
        s=run(stance,crouched,2);
        require(!s.crouched,"Clear standing expands");
        PhysicsWorld stairs;
        floor(stairs);
        stairs.add({
            2,{
                1.4,.15,0
            },{
                .6,.15,2
            }
        });
        CharacterMotor walker(stairs,{
            0,.05,0
        });
        s=run(stairs,walker,27,1);
        require(s.position.x>.9&&s.position.y>.25,"Step up");
        PhysicsWorld slope;
        slope.add({
            1,{
                0,0,0
            },{
                4,.25,4
            },{
            },{
            },0,.45
        });
        CharacterMotor climber(slope,{
            -2,0,0
        });
        s=run(slope,climber,50,1);
        require(s.grounded&&s.position.y>-.3,"Walkable slope");
        PhysicsWorld steep;
        steep.add({
            1,{
                0,0,0
            },{
                4,.25,4
            },{
            },{
            },0,1.1
        });
        CharacterMotor slider(steep,{
            -1,2,0
        });
        s=run(steep,slider,20,1);
        require(!s.grounded,"Steep slope not grounded");
        for(double speed:{
            1.0,-1.0
        }){
            PhysicsWorld elevator;
            elevator.add({
                1,{
                    0,0,0
                },{
                    3,.25,3
                },{
                    0,speed,0
                },{
                },0,0,true
            });
            CharacterMotor rider(elevator,{
                0,.3,0
            });
            s=run(elevator,rider,60);
            auto f=elevator.capture();
            require(std::abs(s.position.y-(f.boxes[0].center.y+.25))<.1&&s.support==1,"Ascending/descending elevator carry");
            elevator.remove(1);
            s=run(elevator,rider,3);
            require(!s.support&&!s.grounded,"Support removal detach");
        }
        PhysicsWorld rotating;
        rotating.add({
            1,{
                0,0,0
            },{
                3,.25,3
            },{
            },{
                0,.6,0
            },0,0,true
        });
        CharacterMotor rider(rotating,{
            1,.3,0
        });
        s=run(rotating,rider,60);
        require(std::abs(s.position.z)>.25&&s.support==1,"Rotating contact point carry");
        PhysicsWorld live;
        floor(live);
        live.add({
            2,{
                0,0,0
            },{
                3,.25,3
            },{
                .2,0,0
            },{
                0,.2,0
            },0,0,true
        });
        CharacterMotor owner(live,{
            1,.3,0
        });
        run(live,owner,8);
        auto baseline=owner.state();
        CollisionHistory history;
        std::vector<MotorInput> commands;
        for(unsigned i=0;i<15;++i){
            history.retain(live.capture());
            MotorInput input{
                baseline.sequence+i+1,live.tick()+1,1,.2,0
            };
            commands.push_back(input);
            owner.step(input);
            live.step();
            owner.post_physics();
        }auto expected=owner.state();
        auto live_before=live.capture();
        MotorState replayed;
        auto result=replay_motor(baseline,commands,history,replayed);
        require(result==ReplayResult::Applied,"Historical replay applied");
        require(std::abs(replayed.position.x-expected.position.x)<.02&&std::abs(replayed.position.z-expected.position.z)<.02,"Historical platform correction replay agrees");
        require(live.capture().boxes[1].center==live_before.boxes[1].center&&live.tick()==live_before.tick,"Replay leaves live world untouched");
        MotorState sentinel;
        sentinel.position.x=999;
        CollisionHistory empty;
        require(replay_motor(baseline,commands,empty,sentinel)==ReplayResult::MissingHistory&&sentinel.position.x==999,"Missing history atomic bounded failure");
        auto bad=baseline;
        bad.topology++;
        require(replay_motor(bad,commands,history,sentinel)==ReplayResult::TopologyMismatch,"Topology mismatch resync");
        auto many=commands;
        many.resize(31);
        require(replay_motor(baseline,many,history,sentinel)==ReplayResult::WorkLimit,"Replay budget resync");
        commands[0].epoch=2;
        require(replay_motor(baseline,commands,history,sentinel)==ReplayResult::Discontinuity,"Teleport discontinuity resync");
        PhysicsWorld carry;
        carry.add({
            1,{
                0,0,0
            },{
                3,.25,3
            },{
                1,0,0
            },{
            },0,0,true
        });
        carry.add({
            2,{
                1.5,1.5,0
            },{
                .05,1.5,3
            }
        });
        CharacterMotor carried(carry,{
            0,.25,0
        });
        auto carried_state=run(carry,carried,120);
        require(carried_state.position.x<1.25,"Obstructed platform carry remains swept");
        bool spawn_denied=false;
        try{
            CharacterMotor invalid(carry,{
                1.5,.25,0
            });
        }catch(...){
            spawn_denied=true;
        }require(spawn_denied,"Invalid overlapping spawn bounded diagnostic");
        PhysicsWorld crush;
        crush.add({1,{0,0,0},{3,.25,3},{0,1,0},{},0,0,true});
        crush.add({2,{0,2.4,0},{3,.2,3}});
        CharacterMotor crushed(crush,{0,.25,0});
        bool crush_failed=false;
        try{run(crush,crushed,60);}catch(...){crush_failed=true;}
        require(crush_failed&&crushed.needs_resync(),"Crushing produces bounded resynchronization");
        PhysicsWorld predicted;
        floor(predicted);
        CharacterMotor predicted_motor(predicted,{
            0,0,0
        });
        OwnerPrediction prediction(predicted,predicted_motor);
        MotorState correction;
        for(unsigned n=1;n<=12;++n){
            MotionRequest motion;
            motion.lock=true;
            if(n>6)motion.root={
                .08,0,0
            };
            prediction.predict({
                n,n,1
            },motion);
            predicted.step();
            predicted_motor.post_physics();
            if(n==6)correction=predicted_motor.state();
        }correction.position.x=-.1;
        require(prediction.reconcile(correction)==ReplayResult::Applied&&prediction.pending()==6,"Owner correction/replay retires covered commands");
        require(std::abs(predicted_motor.state().position.x-.38)<.02,"Root request replay from corrected baseline");
        require(std::abs(prediction.visual_offset().x-.1)<.02,"Visual offset separated from collision");
        PhysicsWorld pairworld;
        floor(pairworld);
        CharacterMotor left(pairworld,{
            0,0,0
        }),right(pairworld,{
            1,0,0
        });
        for(unsigned n=1;n<=20;++n){
            left.step({
                n,n,1,1
            });
            right.step({
                n,n,1,-1
            });
            pairworld.step();
            left.post_physics();
            right.post_physics();
        }require(right.state().position.x-left.state().position.x>.5,"Virtual motor separation without double proxy collision");
        ObserverMotor observer;
        MotorState a;
        a.topology=1;
        a.tick=10;
        a.position.x=0;
        auto b=a;
        b.tick=12;
        b.position.x=4;
        observer.push(a);
        observer.push(b);
        require(observer.sample(11).position.x==2,"Observer interpolation");
        b.tick=14;
        b.epoch=2;
        b.position.x=100;
        observer.push(b);
        require(observer.size()==1&&observer.sample(11).position.x==100,"Observer teleport reset");
        auto query=w.sweep({
            0,1,0
        },{
            5,0,0
        },.1);
        require(!query.hits.empty(),"Swept query visibility");
        PhysicsWorld crowded;
        for(unsigned i=1;i<=4;++i)crowded.add({
            i,{
                double(i)*.2,0,0
            },{
                .1,.1,.1
            }
        });
        auto overlap=crowded.overlap({
            .5,0,0
        },2,2);
        require(overlap.overflow&&overlap.hits.size()==2,"Bounded collector overflow");
        CollisionHistory expired;
        for(unsigned i=0;i<40;++i)expired.retain({
            i,1,{
            }
        });
        require(!expired.find(0)&&expired.find(39),"Collision history eviction");
        double end=-1;
        for(unsigned fps:{
            30,60,144
        }){
            PhysicsWorld clockworld;
            floor(clockworld);
            CharacterMotor mover(clockworld,{
                0,.05,0
            });
            SimulationClock clock;
            for(unsigned frame=0;frame<fps*2;++frame){
                auto n=clock.advance(1.0/fps);
                run(clockworld,mover,n,1);
            }require(clockworld.tick()==120,"Render rate fixed simulation count");
            if(end>=0)require(std::abs(mover.state().position.x-end)<1e-5,"30/60/144 outcome equality");
            end=mover.state().position.x;
        }SimulationClock debt;
        require(debt.advance(1)==8&&debt.debt()>.8,"Bounded clock retains debt");
        std::cout<<"M4 motor contracts: obstruction/root, teleport, jump, impulse, stance, steps/slopes, elevator/rotation/removal, isolated replay failures, observer and 30/60/144 Hz passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
