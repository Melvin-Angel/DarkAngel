#include <darkangel/character_motor.hpp>
#include <darkangel/world_session.hpp>
#include <deque>
#include <iostream>
using namespace darkangel;
void require(bool b,const char* e){
    if(!b)throw std::runtime_error(e);
}
class Delay:public Transport {
    Transport& inner;
    unsigned& clock;
    unsigned lag,loss,counter{
    },loss_accumulator{
    };
    struct Pending{
        unsigned due;
        TransportMessage message;
    };
    std::deque<Pending> pending;
    public:
    unsigned dropped{
    };
    Delay(Transport& i,unsigned& t,unsigned delay,unsigned loss_percent):inner(i),clock(t),lag(delay),loss(loss_percent){
    }
    ConnectionHandle open(const SessionHandshake& h)override{
        return inner.open(h);
    }bool connected()const override{
        return inner.connected();
    }bool valid(ConnectionHandle h)const override{
        return inner.valid(h);
    }TransportLimits limits()const override{
        return inner.limits();
    }bool send(ConnectionHandle h,Delivery d,std::span<const std::byte> b)override{
        return inner.send(h,d,b);
    }void disconnect(ConnectionHandle h)override{
        inner.disconnect(h);
        pending.clear();
    }
    std::vector<TransportMessage> poll(std::size_t limit)override{
        for(auto message:inner.poll(limit)){
            ++counter;
            if(message.delivery==Delivery::UnreliableState){
                loss_accumulator+=loss;
                if(loss_accumulator>=100){
                    loss_accumulator-=100;
                    ++dropped;
                    continue;
                }
            }require(pending.size()<128,"Fault harness bounded queue");
            unsigned jitter=message.delivery==Delivery::UnreliableState?counter%3:0;
            pending.push_back({
                clock+lag+jitter,message
            });
            if(message.delivery==Delivery::UnreliableState&&counter%7==0)pending.push_back({
                clock+lag+4,message
            });
        }std::vector<TransportMessage> out;
        for(auto i=pending.begin();i!=pending.end()&&out.size()<limit;){
            if(i->due<=clock){
                out.push_back(std::move(i->message));
                i=pending.erase(i);
            }else ++i;
        }return out;
    }
};
int main(){
    try{
        for(unsigned lag:{
            0,2,5,8
        })for(unsigned loss:{
            0,2,5
        }){
            unsigned clock{
            };
            SessionHandshake hello{
                2,std::string(64,'a'),std::string(64,'b'),501
            };
            auto pair=create_loopback(hello);
            Delay host(*pair.host,clock,lag,loss),client_transport(*pair.client,clock,lag,loss);
            auto hp=host.open(hello),cp=client_transport.open(hello);
            WorldSession server(SessionRole::Server,hello),client(SessionRole::Client,hello);
            server.attach(host,hp);
            client.attach(client_transport,cp);
            ObjectData object;
            object.id={
                5,1
            };
            auto id=server.create(object);
            server.own_motor(id,hp);
            PhysicsWorld authoritative,predicted;
            authoritative.add({
                1,{
                    0,-.5,0
                },{
                    30,.5,30
                }
            });
            predicted.add({
                1,{
                    0,-.5,0
                },{
                    30,.5,30
                }
            });
            CharacterMotor authority(authoritative,{
                0,0,0
            }),owner(predicted,{
                0,0,0
            });
            OwnerPrediction prediction(predicted,owner);
            server.publish_motor(id,authority.state());
            for(unsigned n=0;n<150&&client.readiness(cp)!=SessionReadiness::Ready;++n){
                server.tick();
                client.tick();
                ++clock;
            }require(client.readiness(cp)==SessionReadiness::Ready,"Faulted bootstrap readiness");
            std::uint64_t last_correction{
            };
            unsigned corrections{
            };
            for(unsigned n=1;n<=120;++n){
                // Host consumes one step even when a delayed sample misses its execution tick.
                MotorInput input{
                    n,n,1,1
                };
                require(client.submit_motor(id,input),"Fault input queue");
                prediction.predict(input);
                predicted.step();
                owner.post_physics();
                server.tick();
                auto consumed=server.consume_motor(id,n,1);
                authority.step(consumed);
                authoritative.step();
                authority.post_physics();
                if(n%2==0)server.publish_motor(id,authority.state());
                server.tick();
                client.tick();
                if(client.motors().contains(id)){
                    auto state=client.motors().at(id);
                    if(state.tick>last_correction){
                        require(prediction.reconcile(state)==ReplayResult::Applied,"Delay/loss correction replay");
                        last_correction=state.tick;
                        ++corrections;
                    }
                }++clock;
            }
            if(loss)require(client_transport.dropped>0,"Loss injection actually dropped snapshots");
            require(corrections>10&&!prediction.needs_resync()&&prediction.pending()<30,"Faulted prediction bounded progress");
            require(std::abs(owner.state().position.x-authority.state().position.x)<1,"Bounded owner correction magnitude");
            std::cout<<"M4 trace lag_ticks="<<lag<<" snapshot_loss="<<loss<<" corrections="<<corrections<<" drops="<<client_transport.dropped<<" pending="<<prediction.pending()<<"\n";
        }return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
