#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/input.hpp>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace darkangel {
enum class CombatSlot {Light,Heavy,RangedLight,RangedHeavy,Spell1,Spell2,Block,Parry,Count};
inline constexpr unsigned combat_slot_count=static_cast<unsigned>(CombatSlot::Count);
struct CombatKitSlot {CombatSlot slot{};std::uint32_t input_action{};AssetId ability;};
struct CombatKitEffectBinding {AssetId ability;unsigned hit_block{};AssetId effect;double power{};};
// Frozen references only: abilities choose their own input-edge semantics.
struct CombatKitDefinition {AssetId id,locomotion_stance;std::string generation;std::array<CombatKitSlot,combat_slot_count> slots;std::vector<AssetId> effects;std::vector<CombatKitEffectBinding> effect_bindings;};
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
