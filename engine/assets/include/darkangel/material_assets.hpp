#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <string>
#include <string_view>
namespace darkangel {
// R5 material instance (.damaterial under materials/): engine-owned values for the registered "lit-color-v1" family, the
// family the static-model renderer draws (base colour factor and optional base-colour texture, roughness, metallic,
// double-sided). Models assign instances to their stable material slot keys in their import metadata; the assignment
// survives reimport as long as the slot key does.
inline constexpr std::string_view material_family_lit_color="lit-color-v1";
struct MaterialInstanceDefinition {AssetId id,texture;std::string name,family,texture_source;std::array<float,4> color{1,1,1,1};float roughness{1},metallic{};bool double_sided{};};
// Validates fields and ranges (colour and factors 0..1); a texture reference needs its "sources.texture" locator.
MaterialInstanceDefinition decode_material_instance(std::string_view json);
}
