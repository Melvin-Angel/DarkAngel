#include <darkangel/character_motor.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/gns_transport.hpp>
#include <chrono>
#include <thread>
#include <iostream>
using namespace darkangel;
void require(bool b,const char* e){
    if(!b)throw std::runtime_error(e);
}
int main(int argc,char** argv){
    try{
        bool processes=argc==2,host=processes&&std::string_view(argv[1])=="--host";
        SessionHandshake hello{
            2,std::string(64,'a'),std::string(64,'b'),401
        };
        auto loop=create_loopback(hello);
        std::unique_ptr<Transport> adapter;
        if(processes)adapter=host?create_gns_host("127.0.0.1:27894",hello):create_gns_client("127.0.0.1:27894",hello);
        if(processes){
            auto peer=adapter->open(hello);
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!adapter->connected()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            require(adapter->connected(),"GNS motor connect deadline");
            WorldSession session(host?SessionRole::Server:SessionRole::Client,hello);
            session.attach(*adapter,peer);
            PhysicsWorld physics;
            physics.add({
                1,{
                    0,-.5,0
                },{
                    50,.5,50
                }
            });
            physics.add({3,{20,1.5,0},{.5,.5,.5},{},{},0,0,false,true,50});
            CharacterMotor motor(physics,{
                0,0,0
            });
            std::uint64_t id=1;
            if(host){
                ObjectData object;
                object.id={
                    4,1
                };
                id=session.create(object);
                session.own_motor(id,peer);
                session.publish_motor(id,motor.state());session.publish_collision(collision_stream(physics.capture()));
            }unsigned sent=0,executed=0;
            bool ready=false;
            deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(std::chrono::steady_clock::now()<deadline){
                session.tick();
                if(session.readiness(peer)==SessionReadiness::Ready){
                    ready=true;
                    if(!host&&session.motors().contains(id)&&!session.collisions().empty()){
                        prepare_collision_frame(session.collisions().back(),{});session.acknowledge_collision(session.collisions().back().tick);session.tick();
                        require(session.collision_control_ready(),"GNS prepared collision control readiness");
                        const auto& state=session.motors().at(id);
                        while(sent<state.tick+4&&sent<90){
                            MotorInput input{
                                sent+1,sent+1,1,1,0
                            };
                            require(session.submit_motor(id,input),"GNS input backpressure");
                            sent++;
                        }if(state.tick>=90&&session.collisions().back().tick>=90){
                            require(session.collisions().back().actors.size()==1&&session.collisions().back().actors[0].foot==state.position&&session.collisions().back().boxes.size()==2&&session.collisions().back().boxes[1].dynamic,"GNS character and dynamic-crate collision state");
                            require(state.sequence==90&&state.position.x>6&&state.position.x<8,"GNS authoritative motor outcome");
                            prepare_collision_frame(session.collisions().back(),{});session.acknowledge_collision(state.tick);
                            require(session.submit_motor(id,{
                                91,91,1
                            }),"Final application receipt");
                            session.tick();
                            std::cout<<"M4 GNS client 90 inputs/45 snapshots x="<<state.position.x<<"\n";
                            std::this_thread::sleep_for(std::chrono::milliseconds(150));
                            return 0;
                        }
                    }
                    if(host&&executed<90&&session.motor_input_ready(id,physics.tick()+1)){
                        // one command/step;
                        // Wait for the queued command in this explicitly stepped acceptance fixture.
                        auto input=session.consume_motor(id,physics.tick()+1,1);
                        if(executed==0)physics.apply_impulse(3,{50,0,0});
                        motor.step(input);
                        physics.step();
                        motor.post_physics();
                        executed++;session.publish_collision(collision_stream(physics.capture()));
                        if(executed%2==0)session.publish_motor(id,motor.state());
                        if(executed==90){
                            require(motor.state().sequence>=1,"GNS consumed owner input");
                        }
                    }
                    if(host&&executed==90&&session.motor_input_ready(id,91)&&session.collision_diagnostics(peer).has_ack&&session.collision_diagnostics(peer).ack_tick==90){
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        std::cout<<"M4 GNS host fixed movement x="<<motor.state().position.x<<" sequence="<<motor.state().sequence<<"\n";
                        return 0;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(host?40:2));
            }throw std::runtime_error(ready?"Motor process outcome deadline":"Motor Ready deadline");
        }
        auto hp=loop.host->open(hello),cp=loop.client->open(hello);
        WorldSession server(SessionRole::Server,hello),client(SessionRole::Client,hello);
        server.attach(*loop.host,hp);
        client.attach(*loop.client,cp);
        ObjectData object;
        object.id={
            4,1
        };
        auto id=server.create(object);
        server.own_motor(id,hp);
        PhysicsWorld world;
        world.add({
            1,{
                0,-.5,0
            },{
                50,.5,50
            }
        });
        CharacterMotor motor(world,{
            0,0,0
        });
        server.publish_motor(id,motor.state());
        for(unsigned n=0;n<20;++n){
            server.tick();
            client.tick();
        }require(client.readiness(cp)==SessionReadiness::Ready&&client.motors().contains(id),"Motor baseline Ready");
        for(unsigned n=1;n<=60;++n){
            MotorInput command{
                n,n,1,1,0
            };
            require(client.submit_motor(id,command),"Input submit");
            require(client.submit_motor(id,command),"Duplicate resend");
            server.tick();
            auto input=server.consume_motor(id,n,1);
            require(input.sequence==n&&input.x==1,"Owner command execution");
            motor.step(input);
            world.step();
            motor.post_physics();
            server.publish_motor(id,motor.state());
            server.tick();
            client.tick();
        }require(client.motors().at(id).sequence==60&&client.motors().at(id).position.x>4,"Loopback motor snapshots");
        auto neutral=server.consume_motor(id,61,1);
        require(neutral.x==0&&!neutral.jump,"Missing input releases immediately");
        bool denied=false;
        try{
            client.publish_motor(id,motor.state());
        }catch(...){
            denied=true;
        }require(denied,"Client publication denied");
        denied=false;
        try{
            server.consume_motor(id,61,1);
        }catch(...){
            denied=true;
        }require(denied,"Duplicate tick denied");
        motor.step(neutral);
        world.step();
        motor.post_physics();
        server.publish_motor(id,motor.state());
        require(motor.teleport({
            0,0,2
        }),"Server teleport validation");
        server.publish_motor(id,motor.state());
        server.tick();
        client.tick();
        require(client.motors().at(id).epoch==2,"Teleport replicated discontinuity");
        require(client.submit_motor(id,{
            1,62,2,1
        }),"Fresh epoch sequence");
        server.tick();
        auto fresh=server.consume_motor(id,62,2);
        require(fresh.sequence==1&&fresh.x==1,"Teleport clears old input sequence fence");
        std::cout<<"M4 Loopback ownership, duplicates, motor snapshots, missing input and fixed step passed\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
