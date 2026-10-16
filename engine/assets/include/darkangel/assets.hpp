#pragma once
#include <span>
#include <darkangel/asset_id.hpp>
#include <darkangel/material_assets.hpp>
#include <array>
#include <cstdint>
#include <compare>
#include <string_view>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>
namespace darkangel {
struct ActionDefinition;
struct SpawnPlan;
void validate_cooked_material_bindings(const std::filesystem::path& registry,const std::filesystem::path& cas,const SpawnPlan&);
struct InputProfile;
InputProfile load_cooked_input(const std::filesystem::path&,const std::filesystem::path&,AssetId);
ActionDefinition load_cooked_action(const std::filesystem::path&,const std::filesystem::path&,AssetId);
struct CollisionDefinition;
struct Vertex {std::array<float,3> position,normal;std::array<float,2> uv;};
struct MeshPart {AssetId material;std::uint32_t first,count;};
struct CookedMesh {std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;std::vector<MeshPart> parts;std::array<float,3> minimum,maximum;};
struct TextureMip {std::uint32_t width,height;std::vector<std::uint8_t> rgba;};
struct CookedTexture {std::vector<TextureMip> mips;bool srgb{true};bool hdr{};std::uint32_t layers{1};};
CookedTexture load_cooked_texture(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
// R5 A3 surface features are optional: a material without them draws exactly as a lit-color-v1 material. alpha_mode: 0 opaque,
// 1 cutout (alpha tested against alpha_cutoff), 2 transparent (blended after opaque surfaces). The surface texture packs
// occlusion (R), roughness (G) and metallic (B) as glTF does; the normal texture is a linear tangent-space map.
struct CookedMaterial {AssetId id;std::string family;std::array<float,4> color{1,1,1,1};float roughness{1},metallic{};bool double_sided{};AssetId texture;bool has_texture{};
    AssetId normal_texture,surface_texture,emissive_texture;bool has_normal{},has_surface{},has_emissive_texture{};float normal_scale{1},occlusion_strength{1},alpha_cutoff{.5f};
    std::array<float,3> emissive{0,0,0};int alpha_mode{};
    std::array<float,3> toon_thresholds{.18f,.52f,.82f},shadow_tint{.22f,.25f,.38f},ambient_color{.22f,.25f,.34f};
    float toon_softness{.035f},ambient_strength{1},local_light_strength{1};bool triplanar{};float triplanar_scale{1};
    std::array<float,4> hit_flash_color{1,1,1,0},status_color{1,1,1,0};float hit_flash_strength{},status_strength{},dissolve{};
    AssetId dissolve_texture,terrain_layer1,terrain_layer2;bool has_dissolve_texture{},has_terrain_layer1{},has_terrain_layer2{};
    std::array<float,3> dissolve_edge{1,.35f,.1f};float dissolve_edge_width{.05f};
    float terrain_blend_sharpness{4},terrain_slope_start{.35f},terrain_slope_end{.75f},terrain_height_blend{.5f};};
struct RuntimeMaterial {CookedMaterial value;std::vector<std::pair<AssetId,CookedTexture>> textures;std::string generation;};
// Standalone native material and its verified texture closure, without artist sources or a model copy.
RuntimeMaterial load_cooked_material_instance(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
// collision: the model's "model-collision" product (load_cooked_model_collision), nil when it was imported without collision.
struct RuntimeModel {AssetId id;std::vector<CookedMesh> meshes;std::vector<CookedMaterial> materials;std::vector<std::pair<AssetId,CookedTexture>> textures;AssetId collision;};
// Load solely from the verified cooked registry/CAS; no SQLite, source assets or editor state.
std::string load_cooked_collision(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
CollisionDefinition load_cooked_collision_scene(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
// Runtime package bounds: products and roots per package. A scene's package holds every model it places and their closures.
constexpr std::size_t runtime_package_product_limit=16384,runtime_package_root_limit=4096;
RuntimeModel load_cooked_model(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
struct ModelGeneration {std::uint64_t number;std::shared_ptr<const RuntimeModel> model;};
class ModelStore {
public:
    ModelGeneration prepare(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId) const;
    void publish(ModelGeneration); // after dependent GPU upload/validation succeeds
    ModelGeneration acquire() const{return current_;}
    std::size_t collect(); // leases keep retired CPU generations alive
    std::size_t retired() const{return retired_.size();}
private:
    ModelGeneration current_{};std::vector<std::shared_ptr<const RuntimeModel>> retired_;
};
struct AssetInfo {AssetId id;std::string path;std::uint64_t generation{};};
struct CookResult {AssetId root;std::uint64_t generation;bool changed;};
// create=true requires an absent owned path and empty expected hash; preparation never writes the real source.
struct NativeSourceEdit {std::string path,expected_sha256,draft;bool create{};};
class PreparedNativeEdit {
public:
    PreparedNativeEdit()=default;
    explicit operator bool() const{return bool(state_);}
    std::filesystem::path registry() const;
private:
    struct State;std::shared_ptr<State> state_;
    friend class AssetService;
};
enum class AssetImportType {Model,SkinnedMesh,Animation,Texture,Vfx,Audio,Material,UI};
std::string_view asset_import_label(AssetImportType);
std::string_view asset_import_folder(AssetImportType);
std::string_view asset_import_support(AssetImportType); // empty when supported by this build
struct AssetImportRequest {
    std::filesystem::path file;
    AssetImportType type{AssetImportType::Model};
    std::string canonical_skeleton;
    bool loop{};
    // Textures only (R5 A3): what the texels mean. Normal maps and packed surface data are linear; colour is sRGB.
    TextureSemantic semantic{TextureSemantic::Color};
};
class PreparedAssetImport {
public:
    PreparedAssetImport()=default;
    explicit operator bool() const{return bool(state_);}
    std::string destination() const;
    std::vector<std::string> files() const;
    AssetId id() const;
private:
    struct State;std::shared_ptr<State> state_;
    friend class AssetService;
};
class AssetService {
public:
    AssetService(std::filesystem::path source_root,std::filesystem::path cache_root);
    ~AssetService();
    AssetService(const AssetService&)=delete;
    AssetService& operator=(const AssetService&)=delete;
    AssetId adopt(std::string_view relative_source); // explicit creation; existing IDs are never regenerated
    AssetId adopt_human(std::string_view relative_source,std::string_view canonical_skeleton,bool renderable=false);
    AssetId adopt_clip(std::string_view relative_source,std::string_view canonical_skeleton,bool loop=false);
    PreparedAssetImport prepare_import(const AssetImportRequest&); // bounded, validates full cook before publication
    CookResult commit_import(const PreparedAssetImport&); // fresh sources/destination; external originals remain intact
    std::filesystem::path source_root() const;
    void scan(); // validate complete source identity inventory before updating the index
    std::vector<AssetInfo> assets() const;
    CookResult cook(std::string_view relative_source);
    // Owned ability/effect/action/kit source edit. Reject stale bytes or identity
    // changes; failed native validation/cook restores the previous source.
    CookResult edit_native(std::string_view relative_source,std::string_view expected_source_sha256,std::string_view draft);
    // Private source snapshot, complete changed/dependent kit and selected scene
    // cook/package validation. No source or catalog head changes in prepare.
    PreparedNativeEdit prepare_native(std::span<const NativeSourceEdit>,std::span<const AssetId> scene_roots={});
    // Editor preview of drafts: cooks only `roots` over a private mount of linked sources plus the drafts, packaging each
    // root whose recipe is unchanged from the catalog instead of converting it. No consumer closure, mount hash or
    // validation of other roots; the result can be read (registry()) but never committed.
    PreparedNativeEdit preview_native(std::span<const NativeSourceEdit>,std::span<const AssetId> roots);
    std::vector<CookResult> commit_native(const PreparedNativeEdit&);
    CookResult create_native(std::string_view relative_source,std::string_view draft); // fresh owned UUID/path; validates before cook
    // R5: assigns a native material instance (or none, returning the slot to its imported default) to a stable material
    // slot key of an imported static model, in the model's import metadata. The model must cook with it, or the previous
    // metadata is restored.
    CookResult assign_model_material(std::string_view relative_model,std::string_view slot,AssetId material,std::string_view material_source={});
    // Compose this metadata edit with native material creation/edits in prepare_native; publication is one recoverable Save.
    NativeSourceEdit model_material_edit(std::string_view relative_model,std::string_view slot,AssetId material,std::string_view material_source={}) const;
    struct ModelReimportChanges {std::vector<std::string> removed,added;};
    ModelReimportChanges model_reimport_changes(std::string_view relative_model) const;
    // Every removed key requires an explicit destination among the added keys, or an empty destination to retire it.
    // Matching identities survive; explicitly unmapped new keys receive fresh identities. Stale metadata refuses publication.
    NativeSourceEdit model_reimport_edit(std::string_view relative_model,std::string_view expected_metadata_sha256,const std::map<std::string,std::string>& remap) const;
    CookResult repair_model_reimport(std::string_view relative_model,std::string_view expected_metadata_sha256,const std::map<std::string,std::string>& remap);
    // The cooked values of one material slot as the model currently cooks them (its material product JSON).
    std::string material_values(std::string_view relative_model,std::string_view slot) const;
    // R5 A3: changes what an imported texture's texels mean (its importer) and recooks it. Refused while a material slot or
    // an item icon uses the texture with its current meaning; a failed cook restores the previous metadata.
    CookResult set_texture_semantic(std::string_view relative_texture,TextureSemantic);
    // Unreferenced item or prefab (R4) only. Stale writes reject; source bytes are archived in the cache.
    std::filesystem::path delete_item(std::string_view relative_source,std::string_view expected_source_sha256);
    // R4: moves an owned prefab source to a new path under prefabs/. Its AssetID lives in the source, so scenes and prefabs
    // that link it by identity keep resolving. Stale bytes and existing targets are refused; a failed cook moves it back.
    CookResult rename_native(std::string_view relative_source,std::string_view expected_source_sha256,std::string_view new_relative_source);
    void package(AssetId root,const std::filesystem::path& registry,std::span<const AssetId> additional_roots={}) const;
    std::filesystem::path cas_path() const;
    std::uint64_t conversion_count() const;
private:
    AssetId import_identity() const;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
