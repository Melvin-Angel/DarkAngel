#pragma once
#include <darkangel/player_ui.hpp>
#include <darkangel/asset_id.hpp>
#include <array>
namespace ashen_roots {
// Explicit shipped UI asset identities. Layout, style, models and fonts are cooked document contracts.
inline constexpr const char* ui_health="2bf2038a-945a-4ce8-8fe5-e569055eb948";
inline constexpr const char* ui_stamina="3b837dcb-72fe-400e-a83a-5a43d2d54357";
inline constexpr const char* ui_target="1e63be9e-d233-4997-886d-29b3c65950d1";
inline constexpr const char* ui_aim="9089b151-6178-44f3-bef1-4838c650a410";
inline constexpr const char* ui_interaction="f79a35d0-e4a8-497f-b67f-a08b541e9f51";
inline constexpr const char* ui_partystatus="e8c39049-bc5b-46d1-961d-c152e3909a22";
inline constexpr const char* ui_loading="39e87de5-950c-41ff-913f-d2fda28147e2";
inline constexpr const char* ui_inventory="1c3845fc-d253-4a1a-8ee0-1157553ba21e";
inline constexpr const char* ui_altar="22196656-1f6e-4816-aa96-a7f24baf4d45";
inline constexpr const char* ui_actormarker="c7d21ad1-1557-4264-adbc-97f91ea932ad";
inline darkangel::PlayerUiDocuments player_ui_documents(){return {{ui_health,ui_stamina,ui_target,ui_aim,ui_interaction,ui_partystatus,ui_loading},ui_inventory,ui_altar,ui_actormarker};}
inline std::vector<darkangel::AssetId> core_ui_roots(){return {darkangel::AssetId::parse(ui_health),darkangel::AssetId::parse(ui_stamina),darkangel::AssetId::parse(ui_target),darkangel::AssetId::parse(ui_aim),darkangel::AssetId::parse(ui_interaction),darkangel::AssetId::parse(ui_partystatus),darkangel::AssetId::parse(ui_loading),darkangel::AssetId::parse(ui_inventory),darkangel::AssetId::parse(ui_altar),darkangel::AssetId::parse(ui_actormarker)};}
}
