#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace darkangel {
// Native domain data. Asset cooking and transport adapters validate their inputs before
// constructing these values; definitions never contain an owned inventory instance.
struct ItemKind {std::string name;bool usable{},equippable{},mask{};};
struct ItemDefinition {
    AssetId id;std::string name,description,classification;
    AssetId icon,model,mask;unsigned stack_limit{1};bool unique{},usable{},equippable{};
};
// Canonical .daitem schema-1 source codec. Cooking will resolve these asset references
// into the existing CAS closure; this codec does not load source files at runtime.
ItemDefinition decode_item_definition(std::string_view);
std::string encode_item_definition(const ItemDefinition&);
class ItemCatalog {
public:
    ItemCatalog();
    void register_kind(ItemKind);
    void insert(ItemDefinition);
    const ItemDefinition& get(AssetId)const;
private:
    std::map<std::string,ItemKind,std::less<>> kinds_;
    std::map<AssetId,ItemDefinition> items_;
};
struct InventoryEntry {std::uint64_t instance{};AssetId item;unsigned quantity{};bool operator==(const InventoryEntry&)const=default;};
struct InventorySnapshot {
    unsigned schema{1};StableId profile;std::uint64_t revision{},next_instance{1};
    std::vector<InventoryEntry> entries;std::array<std::uint64_t,4> equipped{};
    bool operator==(const InventorySnapshot&)const=default;
};
enum class InventoryMutation {Grant,Remove,Consume,Equip,Unequip};
struct InventoryRequest {
    std::uint64_t operation{};InventoryMutation mutation{};AssetId item;
    std::uint64_t instance{};unsigned quantity{},slot{};
    bool operator==(const InventoryRequest&)const=default;
};
enum class InventoryFailure {None,Invalid,Capacity,Unique,Missing,NotUsable,NotMask,Equipped};
struct InventoryReceipt {std::uint64_t operation{},revision{};InventoryFailure failure{};bool operator==(const InventoryReceipt&)const=default;};
// Host-owned per-profile state, deliberately independent of SceneAuthority and avatar
// epochs. The caller supplies the authenticated profile, never an untrusted wire field.
// Mutations prepare a complete candidate before publishing. Duplicate operations return
// their original result; explicit retirement prevents unbounded receipt retention.
class Inventory {
public:
    explicit Inventory(StableId profile,unsigned capacity=128);
    InventoryReceipt apply(StableId authenticated,const ItemCatalog&,const InventoryRequest&);
    void retire(std::uint64_t through);
    unsigned quantity(AssetId)const;
    const InventorySnapshot& snapshot()const{return state_;}
    // Restore is for a fresh session/save load; replay receipts belong to a transport
    // session and are not serialized. No rollback into an active request stream.
    void restore(const ItemCatalog&,const InventorySnapshot&);
private:
    InventorySnapshot state_;unsigned capacity_;std::uint64_t highest_{},retired_{};
    std::map<std::uint64_t,std::pair<InventoryRequest,InventoryReceipt>> receipts_;
};
std::string encode_inventory(const InventorySnapshot&);
InventorySnapshot decode_inventory(std::string_view);
}
