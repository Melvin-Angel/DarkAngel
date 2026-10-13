#include <darkangel/inventory.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){bool rejected{};try{action();}catch(const std::exception&){rejected=true;}check(rejected,"Expected validation rejection");}
}
int main(){try{
    ItemCatalog catalog;const auto material=AssetId::random(),key=AssetId::random(),potion=AssetId::random(),mask=AssetId::random();
    catalog.insert({material,"Ore","","material",{},{},{},3});
    catalog.insert({key,"Temple key","","key",{},{},{},1,true});
    catalog.insert({potion,"Potion","","consumable",{},{},{},5,false,true});
    catalog.insert({mask,"Mask","","mask",{},{},AssetId::random(),1,false,false,true});
    catalog.register_kind({"relic"});catalog.insert({AssetId::random(),"Relic","","relic"});
    auto source=encode_item_definition(catalog.get(mask));auto definition=decode_item_definition(source);check(definition.mask==catalog.get(mask).mask&&definition.id==mask,"Mask item source roundtrip failed");
    auto bad_source=nlohmann::json::parse(source);bad_source["schema"]=2;rejects([&]{decode_item_definition(bad_source.dump());});bad_source=nlohmann::json::parse(source);bad_source["properties"]["mask"]=nullptr;rejects([&]{decode_item_definition(bad_source.dump());});
    bad_source=nlohmann::json::parse(source);bad_source["stack_limit"]=-1;rejects([&]{decode_item_definition(bad_source.dump());});bad_source=nlohmann::json::parse(source);bad_source["extra"]=true;rejects([&]{decode_item_definition(bad_source.dump());});
    rejects([&]{catalog.register_kind({"mask"});});rejects([&]{catalog.insert({AssetId::random(),"Broken","","mask"});});
    const StableId alice{21,1},bob{21,2};Inventory owned(alice,4),other(bob);std::uint64_t operation{};
    auto grant=[&](AssetId id,unsigned quantity){return owned.apply(alice,catalog,{++operation,InventoryMutation::Grant,id,0,quantity});};
    auto receipt=grant(material,7);check(receipt.failure==InventoryFailure::None&&owned.snapshot().entries.size()==3&&owned.quantity(material)==7,"Stack splitting failed");
    auto before=owned.snapshot();InventoryRequest duplicate{operation,InventoryMutation::Grant,material,0,7};
    check(owned.apply(alice,catalog,duplicate)==receipt&&owned.snapshot()==before,"Duplicate grant changed state");
    duplicate.quantity=6;rejects([&]{owned.apply(alice,catalog,duplicate);});check(owned.snapshot()==before,"Conflicting replay changed state");
    rejects([&]{owned.apply(bob,catalog,{operation+1,InventoryMutation::Grant,key,0,1});});check(other.quantity(material)==0,"Per-profile inventory isolation failed");
    check(grant(material,6).failure==InventoryFailure::Capacity&&owned.snapshot()==before,"Partial stack fill survived capacity failure");
    check(grant(key,1).failure==InventoryFailure::None,"Unique grant failed");before=owned.snapshot();check(grant(key,1).failure==InventoryFailure::Unique&&owned.snapshot()==before,"Unique item duplicated");
    auto removed=owned.apply(alice,catalog,{++operation,InventoryMutation::Remove,{},owned.snapshot().entries[0].instance,3});check(removed.failure==InventoryFailure::None,"Remove failed");
    check(grant(potion,2).failure==InventoryFailure::None,"Consumable grant failed");const auto potion_instance=owned.snapshot().entries.back().instance;
    check(owned.apply(alice,catalog,{++operation,InventoryMutation::Consume,{},potion_instance,2}).failure==InventoryFailure::None&&owned.quantity(potion)==0,"Consume failed");
    before=owned.snapshot();check(owned.apply(alice,catalog,{++operation,InventoryMutation::Consume,{},before.entries[0].instance,1}).failure==InventoryFailure::NotUsable&&owned.snapshot()==before,"Nonusable item consumed");
    check(grant(mask,1).failure==InventoryFailure::None,"Mask grant failed");auto instance=owned.snapshot().entries.back().instance;
    check(owned.apply(alice,catalog,{++operation,InventoryMutation::Equip,{},instance,0,1}).failure==InventoryFailure::None,"Owned mask equip failed");
    before=owned.snapshot();check(owned.apply(alice,catalog,{++operation,InventoryMutation::Equip,{},instance,0,2}).failure==InventoryFailure::Equipped&&owned.snapshot()==before,"One instance occupied two slots");
    check(owned.apply(alice,catalog,{++operation,InventoryMutation::Remove,{},instance,1}).failure==InventoryFailure::Equipped,"Equipped item removed");
    const auto text=encode_inventory(owned.snapshot());const auto snapshot=decode_inventory(text);check(snapshot==owned.snapshot(),"Versioned inventory roundtrip failed");
    Inventory restored(alice);restored.restore(catalog,snapshot);check(restored.snapshot()==owned.snapshot(),"Inventory restore changed instances/equipment");
    Inventory wrong_owner(bob);rejects([&]{wrong_owner.restore(catalog,snapshot);});
    auto bad=snapshot;bad.entries[0].quantity=100;Inventory invalid(alice);rejects([&]{invalid.restore(catalog,bad);});check(invalid.snapshot().entries.empty(),"Failed restore published state");
    auto document=nlohmann::json::parse(text);document["schema"]=2;rejects([&]{decode_inventory(document.dump());});document=nlohmann::json::parse(text);document["entries"][0]["quantity"]=-1;rejects([&]{decode_inventory(document.dump());});
    document=nlohmann::json::parse(text);document["entries"].push_back(document["entries"][0]);rejects([&]{decode_inventory(document.dump());});
    document=nlohmann::json::parse(text);document["equipped"][0]=999999;rejects([&]{decode_inventory(document.dump());});
    rejects([&]{decode_inventory(std::string(65537,' '));});
    owned.retire(operation);rejects([&]{owned.apply(alice,catalog,{1,InventoryMutation::Grant,material,0,1});});
    check(owned.apply(alice,catalog,{++operation,InventoryMutation::Unequip,{},0,0,1}).failure==InventoryFailure::None,"Unequip failed");
    Inventory bounded(alice);for(std::uint64_t id=1;id<=128;++id)bounded.apply(alice,catalog,{id,InventoryMutation::Unequip});before=bounded.snapshot();rejects([&]{bounded.apply(alice,catalog,{129,InventoryMutation::Unequip});});check(before==bounded.snapshot(),"Receipt overflow changed state");bounded.retire(128);check(bounded.apply(alice,catalog,{129,InventoryMutation::Unequip}).failure==InventoryFailure::None,"Receipt retirement did not recover capacity");
    std::cout<<"Inventory: stack/capacity atomicity, unique items, ownership, consume, mask slots, replay/conflict/retirement, bounded snapshots and restore passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
