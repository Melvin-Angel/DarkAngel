#pragma once
#include <span>
#include <darkangel/asset_id.hpp>
#include <array>
#include <cstdint>
#include <compare>
#include <string_view>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace darkangel {
struct ActionDefinition;
ActionDefinition load_cooked_action(const std::filesystem::path&,const std::filesystem::path&,AssetId);
struct CollisionDefinition;
struct Vertex {std::array<float,3> position,normal;std::array<float,2> uv;};
struct MeshPart {AssetId material;std::uint32_t first,count;};
struct CookedMesh {std::vector<Vertex> vertices;std::vector<std::uint32_t> indices;std::vector<MeshPart> parts;std::array<float,3> minimum,maximum;};
struct TextureMip {std::uint32_t width,height;std::vector<std::uint8_t> rgba;};
struct CookedTexture {std::vector<TextureMip> mips;bool srgb{true};};
CookedTexture load_cooked_texture(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
struct CookedMaterial {AssetId id;std::array<float,4> color{1,1,1,1};float roughness{1},metallic{};bool double_sided{};AssetId texture;bool has_texture{};};
struct RuntimeModel {AssetId id;std::vector<CookedMesh> meshes;std::vector<CookedMaterial> materials;std::vector<std::pair<AssetId,CookedTexture>> textures;};
// Load solely from the verified cooked registry/CAS; no SQLite, source assets or editor state.
std::string load_cooked_collision(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
CollisionDefinition load_cooked_collision_scene(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
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
enum class AssetImportType {Model,SkinnedMesh,Animation,Texture,Vfx,Audio,Material,UI};
std::string_view asset_import_label(AssetImportType);
std::string_view asset_import_folder(AssetImportType);
std::string_view asset_import_support(AssetImportType); // empty when supported by this build
struct AssetImportRequest {
    std::filesystem::path file;
    AssetImportType type{AssetImportType::Model};
    std::string canonical_skeleton;
    bool loop{};
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
    void package(AssetId root,const std::filesystem::path& registry,std::span<const AssetId> additional_roots={}) const;
    std::filesystem::path cas_path() const;
    std::uint64_t conversion_count() const;
private:
    AssetId import_identity() const;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
