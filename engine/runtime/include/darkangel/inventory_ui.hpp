#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace darkangel {
struct InventoryUiItem {std::string key,element,name,description,kind;int quantity{},equipped{-1};bool mask{},usable{};bool operator==(const InventoryUiItem&)const=default;};
struct InventoryUiSlot {int index{};std::string name,label,element,equip_element,unequip_element;bool empty{true};bool operator==(const InventoryUiSlot&)const=default;};
struct InventoryUiModel {
    bool open{},ready{},busy{};std::string owner,status;std::vector<InventoryUiItem> items;std::vector<InventoryUiSlot> slots;
    bool operator==(const InventoryUiModel&)const=default;
};
enum class InventoryUiActionKind {Toggle,Close,Equip,Unequip,Use};
struct InventoryUiAction {InventoryUiActionKind kind{};std::uint64_t instance{};unsigned slot{};};
}
