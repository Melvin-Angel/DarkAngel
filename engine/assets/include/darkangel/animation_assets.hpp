#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/animation.hpp>
#include <darkangel/joint_mask.hpp>
#include <optional>
namespace darkangel {
struct CookedRig {RigDefinition definition;std::string archive;};
CookedRig load_cooked_rig(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
struct CookedClip {ClipDefinition definition;std::string archive;};
struct SkinnedVertex {Vertex geometry;std::array<unsigned,4> joints{};std::array<float,4> weights{};};
// R5 A3 skinned parity: each primitive is a material slot (its mesh key). A slot with an assigned material instance carries
// that material (id: the primitive's mesh product) and its textures; other primitives draw with the shared atlas and colour.
struct SkinnedMesh {std::vector<SkinnedVertex> vertices;std::vector<unsigned> indices;std::array<float,4> color{1,1,1,1};AssetId id;std::optional<CookedMaterial> material;};
struct RuntimeSkinnedModel {AssetId id;CookedRig rig;std::vector<std::array<float,16>> inverse_bind;std::vector<SkinnedMesh> meshes;CookedTexture texture;bool textured{};std::vector<std::pair<AssetId,CookedTexture>> textures;};
RuntimeSkinnedModel load_cooked_skinned_model(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
CookedClip load_cooked_clip(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
JointMaskDefinition load_cooked_joint_mask(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
std::string load_cooked_human_binding(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
