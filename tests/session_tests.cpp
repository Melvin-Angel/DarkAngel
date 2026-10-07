#include <darkangel/transport.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
void check(bool test,const char* error){if(!test)throw std::runtime_error(error);}
template<class F>void rejects(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Expected transport rejection");}
int main(){try{
    SessionHandshake hello{1,std::string(64,'a'),std::string(64,'b'),3};auto pair=create_loopback(hello,{2,8,12});auto host_peer=pair.host->open(hello);auto bad=hello;bad.content_hash=std::string(64,'c');rejects([&]{pair.client->open(bad);});check(!pair.host->connected(),"Bad peer completed admission");auto client_peer=pair.client->open(hello);
    const std::byte bytes[]={std::byte{1},std::byte{2},std::byte{3}};check(pair.host->send(host_peer,Delivery::ReliableOrdered,bytes) && pair.host->send(host_peer,Delivery::UnreliableState,bytes),"Local round trip send failed");check(!pair.host->send(host_peer,Delivery::ReliableOrdered,bytes),"Packet capacity ignored");auto batch=pair.client->poll(1);check(batch.size()==1 && batch[0].sender==client_peer && batch[0].sequence==1 && batch[0].bytes.size()==3,"Envelope/source/order mismatch");check(pair.client->poll(2)[0].sequence==2,"Queue reorder");check(pair.client->send(client_peer,Delivery::ReliableOrdered,bytes),"Reverse send failed");
    auto other=create_loopback(hello);auto foreign=other.host->open(hello);other.client->open(hello);rejects([&]{pair.host->send(foreign,Delivery::ReliableOrdered,bytes);});pair.client->disconnect(client_peer);check(pair.host->poll(2).empty(),"Disconnect retained old packets");auto new_peer=pair.host->open(hello);pair.client->open(hello);check(new_peer.epoch!=host_peer.epoch,"Reconnect did not advance connection epoch");rejects([&]{pair.host->send(host_peer,Delivery::ReliableOrdered,bytes);});check(pair.host->send(new_peer,Delivery::ReliableOrdered,bytes),"Reconnected transport failed");rejects([&]{pair.client->poll(3);});
    other.host.reset();check(!other.client->connected(),"Destroyed endpoint left a ghost connection");
    std::cout<<"M3 LocalLoopback admission/round-trip/bounds/epoch/ownership checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
