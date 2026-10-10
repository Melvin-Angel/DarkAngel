#pragma once
#include <darkangel/combat_kit_assets.hpp>
namespace darkangel {
// Initial authored references only; never a live actor or a gameplay inheritance tree.
struct CharacterDefinition {AssetId id,skin;std::string generation;};
struct ActorLoadoutDefinition {AssetId kit;};
struct PlayerDefinition {AssetId id,character;ActorLoadoutDefinition loadout;std::string generation;};
struct CookedCharacter {CharacterDefinition definition;RuntimeSkinnedModel skin;};
struct CookedPlayer {PlayerDefinition definition;CookedCharacter character;CookedCombatKit kit;};
CookedCharacter load_cooked_character(const std::filesystem::path&,const std::filesystem::path&,AssetId);
CookedPlayer load_cooked_player(const std::filesystem::path&,const std::filesystem::path&,AssetId);
}
