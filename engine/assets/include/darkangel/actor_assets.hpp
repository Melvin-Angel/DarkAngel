#pragma once
#include <darkangel/combat_kit_assets.hpp>
namespace darkangel {
// Initial authored references only; never a live actor or a gameplay inheritance tree.
struct CharacterDefinition {AssetId id,skin;std::string generation;};
// A Mask owns what a stance used to: its eight-slot kit plus its worn presentation
// (visual model, tint and the Character socket it attaches to). Presentation only
// beyond the kit reference; it holds no live state.
struct MaskDefinition {AssetId id,kit,visual;std::string name,generation,socket{"head"};std::array<float,4> tint{1,1,1,1};std::array<float,3> position{},rotation{};float scale{1};};
inline constexpr unsigned mask_slot_count=4;
// masks: up to four equipped Mask references (empty slots are legal); active_mask
// selects the one whose kit the actor starts with. Without masks, kit is used.
struct ActorLoadoutDefinition {AssetId kit;std::vector<AbilityAttributeValue> starting_attributes;std::array<AssetId,mask_slot_count> masks{};unsigned active_mask{};bool masked{};};
struct CookedMask {MaskDefinition definition;CookedCombatKit kit;};
struct PlayerDefinition {AssetId id,character;ActorLoadoutDefinition loadout;std::string generation;AssetId input_profile;};
struct CookedCharacter {CharacterDefinition definition;RuntimeSkinnedModel skin;};
// kit is the effective starting kit: the active mask's kit when masks are equipped.
struct CookedPlayer {std::array<std::optional<MaskDefinition>,mask_slot_count> masks;PlayerDefinition definition;CookedCharacter character;CookedCombatKit kit;std::vector<AttributeDefinition> attributes;InputProfile input;};
std::vector<AttributeDefinition> resolve_actor_attributes(const AttributeAsset&,std::span<const AbilityAttributeValue>);
CookedCharacter load_cooked_character(const std::filesystem::path&,const std::filesystem::path&,AssetId);
CookedPlayer load_cooked_player(const std::filesystem::path&,const std::filesystem::path&,AssetId);
CookedMask load_cooked_mask(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
