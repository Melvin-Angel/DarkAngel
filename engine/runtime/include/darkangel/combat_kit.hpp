#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/input.hpp>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace darkangel {
// Eight canonical combat slots, then one optional utility ingress: Dodge. Dodge is an
// ordinary native ability with its own semantic input; it is not a ninth combat slot
// in authored kit "slots" and a kit without it leaves the entry unassigned.
enum class CombatSlot {Light,Heavy,RangedLight,RangedHeavy,Spell1,Spell2,Block,Parry,Dodge,Count};
inline constexpr unsigned canonical_combat_slot_count=8;
inline constexpr unsigned combat_slot_count=static_cast<unsigned>(CombatSlot::Count);
struct CombatKitSlot {CombatSlot slot{};std::uint32_t input_action{};AssetId ability;};
struct CombatKitEffectBinding {AssetId ability;unsigned hit_block{};AssetId effect;double power{};};
// Frozen references only: abilities choose their own input-edge semantics.
// death: optional presentation-only timeline shown from public Health 0.
struct CombatKitDefinition {AssetId id,locomotion_stance,death;std::string generation;std::array<CombatKitSlot,combat_slot_count> slots;std::vector<AssetId> effects;std::vector<CombatKitEffectBinding> effect_bindings;};
void validate_combat_kit(const CombatKitDefinition&,const InputProfile&);
struct CombatKitInput {AssetId ability;CombatSlot slot{};InputEvent event;std::uint64_t grant_generation{};};
struct CombatKitReplacement {std::vector<AssetId> removed;std::uint64_t grant_generation{};};
// Intent/grant routing foundation, not an authority or ability executor. Replacement
// callers must prepare native action cancellation before publishing this candidate.
class CombatKitInstance {
public:
 CombatKitInstance(std::shared_ptr<const CombatKitDefinition>,const InputProfile&);
 std::vector<CombatKitInput> route(std::uint64_t expected_grant_generation,std::span<const InputEvent>);
 CombatKitReplacement replace(std::shared_ptr<const CombatKitDefinition>,const InputProfile&);
 const CombatKitDefinition& definition()const{return *definition_;}
 std::uint64_t grant_generation()const{return generation_;}
private:
 friend class AbilityState;
 std::shared_ptr<const CombatKitDefinition> definition_;std::uint64_t generation_{1};
 std::array<bool,combat_slot_count> suppressed_{};
};
}
