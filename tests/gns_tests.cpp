#include <darkangel/gns_transport.hpp>
#include <darkangel/world_session.hpp>
#include <chrono>
#include <iostream>
#include <thread>
using namespace darkangel;
int main(){try{
    SessionHandshake hello{1,std::string(64,'a'),std::string(64,'b'),50};auto host=create_gns_host("127.0.0.1:27891",hello),client=create_gns_client("127.0.0.1:27891",hello);auto hp=host->open(hello),cp=client->open(hello);
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while((!host->connected() || !client->connected()) && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if(!host->connected() || !client->connected())throw std::runtime_error("GNS connection deadline");WorldSession server(SessionRole::Server,hello),view(SessionRole::Client,hello);server.attach(*host,hp);view.attach(*client,cp);ObjectData o;o.id={2,1};auto id=server.create(o);
    auto drive=[&]{auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(std::chrono::steady_clock::now()<end){server.tick();view.tick();if(view.readiness(cp)==SessionReadiness::Ready && view.revision()==server.revision())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error("GNS baseline deadline");};
    drive();Transform t;t.x=12;server.move(id,t);drive();if(view.objects().at(id).transform.x!=12)throw std::runtime_error("GNS authority movement mismatch");server.destroy(id);drive();if(!view.objects().empty())throw std::runtime_error("GNS despawn mismatch");client->disconnect(cp);bool rejected=false;try{client->send(cp,Delivery::ReliableOrdered,{});}catch(...){rejected=true;}if(!rejected)throw std::runtime_error("GNS stale handle accepted");std::cout<<"M3 GNS UDP localhost shared protocol/create/move/despawn/offline checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
