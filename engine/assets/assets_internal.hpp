#pragma once
#include <darkangel/assets.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <span>
namespace darkangel::assets_detail {
using Json=nlohmann::json;
void require(bool,const char*);
std::string read(const std::filesystem::path&,std::size_t limit=64*1024*1024);
Json json(std::string_view,std::size_t limit=1024*1024);
std::filesystem::path within(const std::filesystem::path& root,const std::filesystem::path& relative);
struct Product {AssetId id;std::string kind,extension,bytes;std::vector<AssetId> required;};
struct Import {
    std::vector<Product> products;
    std::map<std::string,std::string> inputs;
    std::vector<std::string> keys;
    std::size_t product_count{}; // includes frozen transitive products, not extra source identities
};
Import import_skeleton(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_clip(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,const std::filesystem::path&,bool loop,bool inspect=false);
Import import_human(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,const std::filesystem::path& canonical,bool inspect=false);
Import import_collision(const std::filesystem::path&,const std::filesystem::path&,const std::map<std::string,AssetId>&,bool inspect=false);
Import import_gltf(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect=false);
CookedMesh decode_mesh(std::string_view);
CookedTexture decode_texture(std::string_view);
}
