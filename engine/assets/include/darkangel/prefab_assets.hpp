#pragma once
#include <darkangel/asset_id.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// R4 prefab: a reusable assembly source (.daprefab under prefabs/) with an explicit kind and display name. Scenes link it by
// AssetID and mount it with their own placement, retained identities and instance patches; Play consumes the resolved plan.
inline constexpr std::size_t prefab_member_limit=64;
struct PrefabDefinition {AssetId id;std::string name;std::vector<std::string> members;};
// Validates the source (fields, member roles, and a full assembly resolve) and returns its identity, name and member local keys
// in key order. A prefab may mount other prefabs (nested, placed by position and yaw) or be a variant of one base prefab
// (explicit typed patches and the base's fingerprint; a changed base needs an explicit rebase). `linked` holds the sources of
// the prefabs it mounts or derives from, transitively.
PrefabDefinition decode_prefab(std::string_view json,const std::map<AssetId,std::string>& linked={});
// The prefabs a prefab source mounts or derives from (direct references only).
std::vector<AssetId> prefab_references(std::string_view json);
}
