#pragma once
#include <darkangel/material_assets.hpp>
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <span>
namespace darkangel::assets_detail {
using Json=nlohmann::json;
void require(bool,const char*);
std::string read(const std::filesystem::path&,std::size_t limit=64*1024*1024);
Json json(std::string_view,std::size_t limit=1024*1024);
// A runtime package registry, parsed once per file revision (path, size and write time) and shared by every loader that reads
// it, instead of each load parsing the whole manifest again. Validates the schema and the product bound.
std::shared_ptr<const Json> registry_json(const std::filesystem::path&);
std::filesystem::path within(const std::filesystem::path& root,const std::filesystem::path& relative);
struct Product {AssetId id;std::string kind,extension,bytes;std::vector<AssetId> required;};
struct Import {
    std::vector<Product> products;
    std::map<std::string,std::string> inputs;
    std::vector<std::string> keys;
    std::vector<AssetId> identities; // frozen transitive identities for composite inspection
    std::size_t product_count{}; // includes frozen transitive products, not extra source identities
};
Import import_skeleton(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_clip(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,const std::filesystem::path&,bool loop,bool inspect=false);
// Clip conversions (the ozz optimisation and build) are remembered across cooks, previews and sessions: an entry in
// `folder` is keyed by the clip's exact input digests, owned identities and loop setting, and its products stay in `cas`.
// The asset service sets them for its cooker build; empty paths turn the memo off.
void set_clip_memo(std::filesystem::path folder,std::filesystem::path cas);
Import import_human(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,const std::filesystem::path& canonical,bool inspect=false,bool renderable=false);
std::string cook_color_texture(std::string_view);
Import import_texture(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_action(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_actor(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_material(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);

Import material_texture(const std::filesystem::path& root,const MaterialInstanceDefinition&,bool inspect);
Import import_prefab(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_combat_kit(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
unsigned graph_product_layer_bound(const std::map<AssetId,Product>&,AssetId);
Import import_graph(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_ability(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_effect(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_vfx(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_sound(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_cue(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_item(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_primitive(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_sound_definition(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_audio_mix(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_audio_profile(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_input(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_joint_mask(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_collision(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
// collision: also cook the model's own collision shape (the sidecar's "collision": "mesh", the default) or not ("none").
// A static model sidecar's collision setting: "mesh" (the default) or "none".
bool static_model_collision(const Json& sidecar);
Import import_gltf(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect=false,bool collision=true);
CookedMesh decode_mesh(std::string_view);
CookedTexture decode_texture(std::string_view);
}
