#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace darkangel {
using AttributeId=std::uint32_t;
enum class AttributeKind {Resource,Statistic};
struct AttributeDefinition {AttributeId id{};std::string name;AttributeKind kind{};double base{},minimum{},maximum{};AttributeId maximum_attribute{};};
enum class AttributeModifierKind {Flat,ChannelBonus,Post,Override};
struct AttributeModifier {std::uint64_t owner{},sequence{};AttributeId attribute{};AttributeModifierKind kind{};double magnitude{};std::string channel;int priority{};};
struct ResourceDelta {AttributeId attribute{};double delta{};};
// Native calculation foundation. Mutations are staged atomically; caller owns
// authority, costs/reservations, notifications and actor-generation checks.
class AttributeSet {
public:
 explicit AttributeSet(std::vector<AttributeDefinition>);
 double value(AttributeId)const;
 std::span<const AttributeDefinition> definitions()const{return definitions_;}
 void add(std::span<const AttributeModifier>);
 void remove_owner(std::uint64_t);
 // Costs reject insufficient resources instead of silently clamping. Damage/heal
 // use reject_underflow=false and clamp to the current derived resource bounds.
 void transact(std::span<const ResourceDelta>,bool reject_underflow);
 const std::vector<AttributeModifier>& modifiers()const{return modifiers_;}
private:
 std::vector<AttributeDefinition> definitions_;std::vector<double> values_;
 std::vector<AttributeModifier> modifiers_;
 std::size_t index(AttributeId)const;void recompute();
};
}
