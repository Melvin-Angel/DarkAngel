#include <darkangel/inventory_session.hpp>
#include <darkangel/world_session.hpp>
#include <iostream>
#include <stdexcept>

using namespace darkangel;
namespace {
void check(bool b,const char* e){if(!b)throw std::runtime_error(e);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Expected rejection");}
AssetId id(unsigned n){AssetId value;value.bytes[15]=std::uint8_t(n);return value;}
std::shared_ptr<const ItemCatalog> catalog(){auto value=std::make_shared<ItemCatalog>();value->insert({id(1),"Drink","","consumable",{},{},{},10,false,true});value->insert({id(2),"Mask","","mask",{},{},id(12),1,false,false,true});value->insert({id(3),"Stone","","regular",{},{},{},1});return value;}
void store_tests(){
    auto definitions=catalog();PartyInventory store(definitions);const StableId first{77,1},second{77,2};
    const InventoryStartingItem initial[]{ {id(1),12},{id(2),1} };store.join(first,initial);store.join(first,initial);store.join(second);
    auto initial_state=store.state(first);check(initial_state.inventory.entries.size()==3,"Starting inventory stacked/granted once");
    auto denied=store.request(first,{1,InventoryMutation::Grant,id(1),0,50});check(denied.receipt.failure==InventoryFailure::Invalid&&denied.inventory==initial_state.inventory,"Client granted items");
    auto consume=InventoryRequest{2,InventoryMutation::Consume,{},initial_state.inventory.entries.front().instance,2};auto result=store.request(first,consume);check(result.receipt.failure==InventoryFailure::None,"Owner consume failed");
    store.grant(first,id(1),3);auto after_grant=store.state(first);auto replay=store.request(first,consume);check(replay.inventory==after_grant.inventory&&replay.receipt==result.receipt,"Replay consumed twice after trusted grant");
    auto conflict=consume;conflict.quantity=3;check(store.request(first,conflict).receipt.failure==InventoryFailure::Invalid&&store.state(first).inventory==after_grant.inventory,"Conflicting retry changed inventory");
    check(store.state(second).inventory.entries.empty(),"Profiles shared entries");
    auto mask=initial_state.inventory.entries.back().instance;auto equip=store.request(first,{3,InventoryMutation::Equip,{},mask,0,2});check(equip.inventory.equipped[2]==mask,"Owned mask equip");
    check(store.request(second,{1,InventoryMutation::Equip,{},mask,0,2}).receipt.failure==InventoryFailure::Missing,"Another profile used instance");
    auto snapshot=store.capture();PartyInventory restored(definitions);restored.restore(snapshot);check(restored.capture()==snapshot,"Versioned party inventory restore");
    snapshot.push_back(snapshot.front());PartyInventory broken(definitions);rejects([&]{broken.restore(snapshot);});check(broken.capture().empty(),"Partial failed party restore published");
    auto before=store.state(first).inventory;rejects([&]{const InventoryStartingItem too_many[]{{id(3),129}};store.join({77,3},too_many);});check(!store.contains({77,3})&&store.state(first).inventory==before,"Failed starting inventory published");
    for(std::uint64_t operation=4;operation<=140;++operation)check(store.request(first,{operation,InventoryMutation::Unequip,{},{},0,2}).receipt.failure==InventoryFailure::None,"Receipt retirement stopped requests");
    before=store.state(first).inventory;check(store.request(first,consume).receipt.failure==InventoryFailure::Invalid&&store.state(first).inventory==before,"Retired operation replay changed state");
}
struct Link {
    LoopbackPair transport;ConnectionHandle hp,cp;WorldSession client;InventoryClientSession inventory;std::uint64_t network{};
    Link(SessionHandshake hello,StableId avatar,StableId profile,std::shared_ptr<const ItemCatalog> definitions):transport(create_loopback(hello,{2,1000,2000})),client(SessionRole::Client,hello,{128,4,2}),inventory(client,avatar,profile,std::move(definitions)){
        hp=transport.host->open(hello);cp=transport.client->open(hello);client.attach(*transport.client,cp);
    }
    void pump(){client.tick();inventory.pump(network);}
};
void scene_epoch_tests(){
    auto definitions=catalog();PartyInventory store(definitions);StableId profile{70,1},other{70,2};const InventoryStartingItem initial[]{ {id(1),15},{id(2),1} };store.join(profile,initial);store.join(other);
    InventorySnapshot retained;std::uint64_t retained_operation{};
    for(unsigned epoch=1;epoch<=3;++epoch){
        SessionHandshake hello{3,std::string(64,'a'),std::string(64,'b'),epoch};WorldSession server(SessionRole::Server,hello,{128,4,2});InventoryHostSession host(server,store);
        StableId avatar{80,epoch},second{90,epoch};const auto first_profile=epoch==3?other:profile,second_profile=epoch==3?profile:other;
        Link first(hello,avatar,first_profile,definitions),second_link(hello,second,second_profile,definitions);
        ObjectData object;object.id=avatar;first.network=server.create(object);object.id=second;second_link.network=server.create(object);
        server.attach(*first.transport.host,first.hp);server.attach(*second_link.transport.host,second_link.hp);server.own_motor(first.network,first.hp);server.own_motor(second_link.network,second_link.hp);host.bind(first.network,first_profile);host.bind(second_link.network,second_profile);
        rejects([&]{host.bind(99,profile);});
        auto drive=[&](unsigned count=50){for(unsigned i=0;i<count;++i){server.tick();host.pump();first.pump();second_link.pump();}};drive();
        check(first.inventory.state()&&second_link.inventory.state(),"Owner baseline did not synchronize");if(epoch<3)check(second_link.inventory.state()->inventory.entries.empty(),"Non-owner received first inventory");
        if(epoch==1){auto instance=first.inventory.state()->inventory.entries.front().instance;check(first.inventory.submit({0,InventoryMutation::Consume,{},instance,3}),"Owner request submit");check(!first.inventory.submit({0,InventoryMutation::Unequip,{},{},0,0}),"Client allowed concurrent operations");check(first.inventory.retry(),"Pending identical retry submit");drive();
            check(!first.inventory.pending()&&first.inventory.state()->receipt.failure==InventoryFailure::None,"Owner receipt failed");auto state=store.state(profile);check(state.inventory.entries.front().quantity==7&&state.highest_operation==1,"Duplicate wire consume applied twice");
            store.grant(profile,id(1),2);drive();retained=first.inventory.state()->inventory;retained_operation=first.inventory.state()->highest_operation;
        }else if(epoch==2){check(first.inventory.state()->inventory==retained&&first.inventory.state()->highest_operation==retained_operation,"Scene epoch lost profile inventory or replay scope");check(first.inventory.submit({0,InventoryMutation::Unequip,{},{},0,0}),"Reconnect next operation submit");drive();check(first.inventory.state()->highest_operation==retained_operation+1,"Reconnect used stale operation counter");retained=first.inventory.state()->inventory;}
        else check(first.inventory.state()->inventory.entries.empty()&&second_link.inventory.state()->inventory==retained,"Seat reassignment transferred inventory or lost the returning profile");
        server.disconnect(first.hp);host.unbind(first.network);check(store.state(profile).inventory.entries.size()==3,"Disconnect discarded persistent inventory");
        host.unbind(second_link.network);host.bind(second_link.network,second_profile);check(store.state(other).inventory.entries.empty(),"Seat reuse transferred inventory");
    }
}
void large_snapshot_and_forgery(){
    auto definitions=catalog();PartyInventory store(definitions);StableId profile{11,1},empty{11,2},avatar{22,1},other_avatar{22,2};const InventoryStartingItem full[]{ {id(3),128} };store.join(profile,full);store.join(empty);
    SessionHandshake hello{3,std::string(64,'a'),std::string(64,'b'),91};WorldSession server(SessionRole::Server,hello,{128,4,2});InventoryHostSession host(server,store);Link client(hello,avatar,profile,definitions);
    ObjectData object;object.id=avatar;client.network=server.create(object);object.id=other_avatar;auto other=server.create(object);server.attach(*client.transport.host,client.hp);server.own_motor(client.network,client.hp);host.bind(client.network,profile);
    for(unsigned i=0;i<60;++i){server.tick();host.pump();client.pump();if(i<5)check(!client.inventory.state(),"Partial inventory snapshot published");}
    check(client.inventory.state()&&client.inventory.state()->inventory.entries.size()==128,"Large inventory fragments failed under two-packet backpressure");
    auto intruder=create_loopback(hello);auto ih=intruder.host->open(hello),ic=intruder.client->open(hello);WorldSession forged(SessionRole::Client,hello);forged.register_owner_channel(1,avatar);forged.attach(*intruder.client,ic);server.attach(*intruder.host,ih);server.own_motor(other,ih);host.bind(other,empty);
    for(unsigned i=0;i<40;++i){server.tick();host.pump();client.pump();forged.tick();}
    const auto before=store.capture();std::vector<std::byte> sync(16);sync[0]=std::byte{1};check(forged.send_owner_message(client.network,1,sync),"Forgery test did not send");forged.tick();rejects([&]{server.tick();});
    check(!intruder.client->connected()&&store.capture()==before&&client.transport.client->connected(),"Forged owner identity changed authority or disconnected victim");
    // Malformed service schema closes only its authenticated sender.
    const std::byte invalid[]{std::byte{9}};check(client.client.send_owner_message(client.network,1,invalid),"Malformed request test send");client.client.tick();server.tick();host.pump();check(!client.transport.client->connected()&&store.capture()==before,"Malformed inventory message changed state");
}
void mailbox_bounds(){
    SessionHandshake hello{3,std::string(64,'a'),std::string(64,'b'),92};auto pair=create_loopback(hello,{2,1000,2000});auto hp=pair.host->open(hello),cp=pair.client->open(hello);
    WorldSession server(SessionRole::Server,hello,{128,4,2}),client(SessionRole::Client,hello,{128,4,2});StableId avatar{9,9};server.register_owner_channel(8);client.register_owner_channel(8,avatar);ObjectData object;object.id=avatar;auto network=server.create(object);server.attach(*pair.host,hp);client.attach(*pair.client,cp);server.own_motor(network,hp);
    for(unsigned i=0;i<20;++i){server.tick();client.tick();}
    std::vector<std::byte> payload(32768,std::byte{42});for(unsigned i=0;i<8;++i)check(server.send_owner_message(network,8,payload),"Bounded owner queue rejected early");check(!server.send_owner_message(network,8,payload),"Owner queue overflow accepted");payload.push_back(std::byte{0});rejects([&]{server.send_owner_message(network,8,payload);});payload.pop_back();
    unsigned received{};for(unsigned i=0;i<310;++i){server.tick();client.tick();for(auto message:client.drain_owner_messages(8)){check(message.network==network&&message.bytes==payload,"Fragmented owner message corrupted");++received;}}
    check(received==8,"Reliable owner messages lost under backpressure");check(server.packets_by_kind(hp).at(25)==8*36,"Owner fragments escaped packet accounting");
}
}
int main(){try{store_tests();scene_epoch_tests();large_snapshot_and_forgery();mailbox_bounds();std::cout<<"M8 inventory profile lifetime, requests/replay, ownership/privacy, restore, fragments/backpressure and forgery passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
