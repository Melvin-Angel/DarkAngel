#include <darkangel/gns_transport.hpp>
#include <darkangel/inventory_session.hpp>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <thread>

using namespace darkangel;
namespace {
void check(bool b,const char* e){if(!b)throw std::runtime_error(e);}
AssetId id(unsigned n){AssetId value;value.bytes[15]=std::uint8_t(n);return value;}
unsigned quantity(const InventorySnapshot& value,AssetId item){unsigned total{};for(auto entry:value.entries)if(entry.item==item)total+=entry.quantity;return total;}
}
int main(int argc,char** argv){try{
    check(argc==3,"Use --host/--client endpoint");const bool hosting=std::string_view(argv[1])=="--host";check(hosting||std::string_view(argv[1])=="--client","Unknown inventory GNS role");
    auto definitions=std::make_shared<ItemCatalog>();definitions->insert({id(1),"Drink","","consumable",{},{},{},10,false,true});definitions->insert({id(2),"Mask","","mask",{},{},id(12),1,false,false,true});definitions->insert({id(3),"Stone","","regular",{},{},{},1});
    const StableId avatar{510,1},profile{511,1};SessionHandshake hello{3,std::string(64,'a'),std::string(64,'b'),39175};
    auto transport=hosting?create_gns_host(argv[2],hello):create_gns_client(argv[2],hello);auto peer=transport->open(hello);WorldSession session(hosting?SessionRole::Server:SessionRole::Client,hello,{128,4,2,600,30,12,3,5,true});
    PartyInventory store(definitions);std::unique_ptr<InventoryHostSession> host;std::unique_ptr<InventoryClientSession> owner;
    if(hosting){const InventoryStartingItem starting[]{{id(1),15},{id(2),1},{id(3),90}};store.join(profile,starting);host=std::make_unique<InventoryHostSession>(session,store);ObjectData object;object.id=avatar;check(session.create(object)==1,"GNS owner network identity");host->bind(1,profile);}
    else owner=std::make_unique<InventoryClientSession>(session,avatar,profile,definitions);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(!transport->connected()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));check(transport->connected(),"Inventory GNS connection deadline");session.attach(*transport,peer);if(hosting)session.own_motor(1,peer);
    unsigned phase{};bool granted{};
    while(std::chrono::steady_clock::now()<deadline){session.tick();
        if(hosting){host->pump();auto state=store.state(profile);if(!granted&&state.highest_operation>=2){check(quantity(state.inventory,id(1))==12,"Duplicate consume changed host quantity");check(store.grant(profile,id(1),3).failure==InventoryFailure::None,"Trusted GNS grant");granted=true;}
            if(state.highest_operation==5){check(granted&&quantity(state.inventory,id(1))==15&&state.inventory.equipped[1],"Host authoritative GNS result");std::cout<<"Inventory GNS host passed: denied client grant, consume once, trusted grant, owned mask and receipt-delivery fence\n";std::this_thread::sleep_for(std::chrono::milliseconds(400));return 0;}}
        else{
            if(session.objects().contains(1))owner->pump(1);
            if(owner->state()&&!owner->pending()){
                const auto& state=*owner->state();check(state.inventory.entries.size()>=92,"Large GNS owner snapshot incomplete");
                if(phase==0){check(quantity(state.inventory,id(1))==15,"Initial GNS inventory");check(owner->submit({0,InventoryMutation::Grant,id(1),0,99}),"Client grant test submit");++phase;}
                else if(phase==1){check(state.highest_operation==1&&state.receipt.failure==InventoryFailure::Invalid&&quantity(state.inventory,id(1))==15,"Client grant was not refused");auto instance=state.inventory.entries.front().instance;check(owner->submit({0,InventoryMutation::Consume,{},instance,3})&&owner->retry(),"GNS consume/retry submit");++phase;}
                else if(phase==2&&state.highest_operation==2&&quantity(state.inventory,id(1))==15){auto mask=std::find_if(state.inventory.entries.begin(),state.inventory.entries.end(),[](auto entry){return entry.item==id(2);});check(owner->submit({0,InventoryMutation::Equip,{},mask->instance,0,1}),"GNS equip submit");++phase;}
                else if(phase==3){check(state.highest_operation==3&&state.inventory.equipped[1]&&quantity(state.inventory,id(1))==15,"Owner GNS correction/equipment");check(owner->submit({0,InventoryMutation::Unequip,{},{},0,3}),"GNS delivery fence submit");++phase;}
                else if(phase==4){check(state.highest_operation==4&&state.receipt.failure==InventoryFailure::None,"GNS final receipt");check(owner->submit({0,InventoryMutation::Unequip,{},{},0,3}),"GNS receipt-delivery acknowledgment");session.tick();std::cout<<"Inventory GNS client passed: fragmented owner view, consume retry once, host correction and mask slots\n";std::this_thread::sleep_for(std::chrono::milliseconds(400));return 0;}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(hosting?5:2));
    }
    throw std::runtime_error("Inventory GNS verification deadline");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
