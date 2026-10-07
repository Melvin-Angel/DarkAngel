#include <darkangel/transport.hpp>
#include <darkangel/world_session.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace darkangel;
void check(bool test,const char* error){if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Expected transport rejection");}
void authority(){
    SessionHandshake hello{1,std::string(64,'a'),std::string(64,'b'),99};
    WorldSession server(SessionRole::Server,hello),client(SessionRole::Client,hello);
    auto pair=create_loopback(hello,{2,1000,2000});auto hp=pair.host->open(hello),cp=pair.client->open(hello);
    server.attach(*pair.host,hp);client.attach(*pair.client,cp);
    std::vector<std::uint64_t> ids;
    for(unsigned i=0;i<23;++i){ObjectData o;o.id={1,i+1};o.transform.x=i;ids.push_back(server.create(o));}
    auto drive=[&]{for(unsigned i=0;i<50;++i){server.tick();client.tick();}check(client.readiness(cp)==SessionReadiness::Ready && server.readiness(hp)==SessionReadiness::Ready,"Client not ready after complete baseline ACK");check(client.revision()==server.revision(),"Baseline catch-up revision mismatch");};
    server.tick();client.tick();check(client.objects().empty(),"Partial baseline visible");drive();check(client.objects().size()==23 && client.objects().at(ids[10]).transform.x==10,"Late join lost authoritative objects");
    rejects([&]{client.create(ObjectData{});});rejects([&]{client.move(ids[0],Transform{});});rejects([&]{client.destroy(ids[0]);});
    auto revision=server.revision();Transform bad;bad.x=std::numeric_limits<double>::quiet_NaN();rejects([&]{server.move(ids[0],bad);});check(server.revision()==revision,"Invalid server mutation changed revision");
    Transform moved;moved.x=45;server.move(ids[0],moved);server.destroy(ids[1]);drive();check(client.objects().at(ids[0]).transform.x==45 && !client.objects().contains(ids[1]),"Create/move/despawn replication mismatch");
    auto late_pair=create_loopback(hello);auto lh=late_pair.host->open(hello),lc=late_pair.client->open(hello);WorldSession late(SessionRole::Client,hello);server.attach(*late_pair.host,lh);late.attach(*late_pair.client,lc);for(unsigned i=0;i<30;++i){server.tick();client.tick();late.tick();}check(late.objects().size()==22 && late.objects().at(ids[0]).transform.x==45 && late.readiness(lc)==SessionReadiness::Ready,"Late baseline not current");
    ObjectData replacement;replacement.id={1,2};auto replacement_id=server.create(replacement);check(replacement_id!=ids[1],"Network ID reused within session");drive();
    // A peer cannot send arbitrary authoritative operations, even through an
    // admitted transport. Protocol failure closes it without changing host state.
    const std::byte forged[]={std::byte{0}};late_pair.client->send(lc,Delivery::ReliableOrdered,forged);revision=server.revision();rejects([&]{server.tick();});check(server.revision()==revision && !late_pair.client->connected(),"Forged wire operation changed authority");
    auto epoch=hello;epoch.session_epoch++;auto wrong=create_loopback(epoch);auto wh=wrong.host->open(epoch),wc=wrong.client->open(epoch);WorldSession stale(SessionRole::Client,epoch);stale.attach(*wrong.client,wc);server.attach(*wrong.host,wh);stale.tick();rejects([&]{server.tick();});
    std::cout<<"M3 authority/chunked atomic baseline/late join/catch-up/identity/forgery checks passed\n";
}
int main(int argc,char** argv){try{
    if(argc==2 && std::string_view(argv[1])=="authority"){authority();return 0;}
    SessionHandshake hello{1,std::string(64,'a'),std::string(64,'b'),3};auto pair=create_loopback(hello,{2,8,12});auto host_peer=pair.host->open(hello);auto bad=hello;bad.content_hash=std::string(64,'c');rejects([&]{pair.client->open(bad);});check(!pair.host->connected(),"Bad peer completed admission");auto client_peer=pair.client->open(hello);
    const std::byte bytes[]={std::byte{1},std::byte{2},std::byte{3}};check(pair.host->send(host_peer,Delivery::ReliableOrdered,bytes) && pair.host->send(host_peer,Delivery::UnreliableState,bytes),"Local round trip send failed");check(!pair.host->send(host_peer,Delivery::ReliableOrdered,bytes),"Packet capacity ignored");auto batch=pair.client->poll(1);check(batch.size()==1 && batch[0].sender==client_peer && batch[0].sequence==1 && batch[0].bytes.size()==3,"Envelope/source/order mismatch");check(pair.client->poll(2)[0].sequence==2,"Queue reorder");check(pair.client->send(client_peer,Delivery::ReliableOrdered,bytes),"Reverse send failed");
    auto other=create_loopback(hello);auto foreign=other.host->open(hello);other.client->open(hello);rejects([&]{pair.host->send(foreign,Delivery::ReliableOrdered,bytes);});pair.client->disconnect(client_peer);check(pair.host->poll(2).empty(),"Disconnect retained old packets");auto new_peer=pair.host->open(hello);pair.client->open(hello);check(new_peer.epoch!=host_peer.epoch,"Reconnect did not advance connection epoch");rejects([&]{pair.host->send(host_peer,Delivery::ReliableOrdered,bytes);});check(pair.host->send(new_peer,Delivery::ReliableOrdered,bytes),"Reconnected transport failed");rejects([&]{pair.client->poll(3);});
    other.host.reset();check(!other.client->connected(),"Destroyed endpoint left a ghost connection");
    std::cout<<"M3 LocalLoopback admission/round-trip/bounds/epoch/ownership checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
