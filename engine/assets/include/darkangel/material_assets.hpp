#pragma once
#include <darkangel/asset_id.hpp>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// R5 A3: what an imported texture's texels mean. Colour textures are sRGB; data textures (packed occlusion, roughness and
// metallic) and tangent-space normal maps are linear. The texture's import metadata records it (its importer), so the same
// image file cannot silently be read with two transfer functions.
enum class TextureSemantic {Color,Data,Normal};
std::string_view texture_importer(TextureSemantic);          // "texture-color-v1", "texture-data-v1", "texture-normal-v1"
std::optional<TextureSemantic> texture_semantic(std::string_view importer);
std::string_view texture_semantic_label(TextureSemantic);
// How a surface uses its alpha: ignored, alpha-tested against a cutoff, or blended (drawn after opaque surfaces).
enum class AlphaMode {Opaque,Cutout,Transparent};
std::string_view alpha_mode_name(AlphaMode);
// R5 material definitions (DAE-005 MaterialDefinition): a registered, versioned family names the parameters and texture slots
// (each with the semantic it requires) its shaders read and the render states it allows. Instances (.damaterial) hold values
// for one family. The editor shows only a family's own controls; unknown families and fields are refused.
//   lit-color-v1:   base colour factor and texture, roughness, metallic, double-sided (A1).
//   lit-surface-v1: lit-color-v1 plus a normal map and its scale, a packed surface map (R occlusion, G roughness, B metallic,
//                   as glTF), occlusion strength, emissive colour and map, and opaque/cutout/transparent alpha.
// Both families draw through one shared surface shader: a feature a material does not use is skipped, not sampled.
inline constexpr std::string_view material_family_lit_color="lit-color-v1";
inline constexpr std::string_view material_family_lit_surface="lit-surface-v1";
struct MaterialTextureSlot {std::string key;TextureSemantic semantic;};
struct MaterialDefinition {std::string family;std::vector<std::string> parameters;std::vector<MaterialTextureSlot> textures;std::vector<AlphaMode> alpha_modes;};
const std::vector<MaterialDefinition>& material_definitions();
// Throws for a family that is not registered.
const MaterialDefinition& material_definition(std::string_view family);
// A texture an instance names: its AssetID and the source locator ("sources.<slot>") it is validated against.
struct MaterialTextureRef {AssetId id;std::string source;explicit operator bool() const{return id!=AssetId{};}};
struct MaterialInstanceDefinition {
    AssetId id,texture;std::string name,family,texture_source;std::array<float,4> color{1,1,1,1};float roughness{1},metallic{};bool double_sided{};
    // lit-surface-v1 only (defaults leave the surface exactly as lit-color-v1 draws it).
    MaterialTextureRef normal_texture,surface_texture,emissive_texture;float normal_scale{1},occlusion_strength{1},alpha_cutoff{.5f};
    std::array<float,3> emissive{0,0,0};AlphaMode alpha_mode{AlphaMode::Opaque};
    // Every texture the instance names with the semantic its slot requires: base colour first, then normal, surface, emissive.
    std::vector<std::pair<MaterialTextureRef,TextureSemantic>> textures() const;
};
// Validates the family's fields and ranges (colours and factors 0..1, normal scale 0..2, cutoff 0..1); each texture needs its
// "sources.<slot>" locator.
MaterialInstanceDefinition decode_material_instance(std::string_view json);
}
