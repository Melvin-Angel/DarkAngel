#pragma once
#include <darkangel/asset_id.hpp>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// R4 prefab: a reusable assembly source (.daprefab under prefabs/) with an explicit kind and display name. Scenes link it by
// AssetID and mount it with their own placement, retained identities and instance patches; Play consumes the resolved plan.
inline constexpr std::size_t prefab_member_limit=64;
struct PrefabDefinition {AssetId id;std::string name;std::vector<std::string> members;};
// Validates the source (fields, member roles, and a full standalone assembly resolve) and returns its identity, name and
// member local keys in key order.
PrefabDefinition decode_prefab(std::string_view json);
}
