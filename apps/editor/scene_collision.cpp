#include "scene_collision.hpp"
#include <darkangel/hash.hpp>
#include <darkangel/model_collision.hpp>
#include <fstream>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {
void require(bool b,const char* e){if(!b)throw std::runtime_error(e);}
// A canonical UUIDv4 AssetID from the first 32 hex digits of a digest (version and variant digits set).
AssetId derived(std::string_view seed){auto hex=sha256(seed).substr(0,32);hex[12]='4';hex[16]="89ab"[std::string_view("0123456789abcdef").find(hex[16])%4];hex.insert(20,1,'-');hex.insert(16,1,'-');hex.insert(12,1,'-');hex.insert(8,1,'-');return AssetId::parse(hex);}
}
SceneCollisionFile scene_collision_file(AssetId scene,const std::filesystem::path& scene_path,const std::filesystem::path& source_root){
    SceneCollisionFile file;file.asset=derived("darkangel.scene-collision/"+scene.text());file.source=scene_path;file.source.replace_extension(".dacollision");
    file.relative=std::filesystem::relative(file.source,source_root).generic_string();require(!file.relative.empty()&&!file.relative.starts_with(".."),"A scene's collision must live inside the project's sources");return file;
}
std::optional<AssetId> scene_collision(const SpawnPlan& plan,AssetId scene,const std::filesystem::path& scene_path,const std::filesystem::path& source_root){
    if(!plan.collision_profiles.empty())return plan.collision_profiles.begin()->second;
    const auto file=scene_collision_file(scene,scene_path,source_root);if(std::filesystem::exists(file.source))return file.asset;return std::nullopt;
}
std::optional<std::string> bake_scene_collision(AssetId collision,const SpawnPlan& plan,const World& world,AssetId skin,const std::function<RuntimeModel(AssetId)>& load){
    CollisionDefinition definition;definition.id=collision;definition.schema=3;std::uint64_t body=1000;
    for(const auto& [object,asset]:plan.models){
        if(asset==skin||plan.pickups.contains(object)||plan.altars.contains(object)||plan.movers.contains(object))continue;
        const auto handle=world.find(object);if(!world.valid(handle))continue;
        require(definition.meshes.size()<16,"A scene's baked collision holds at most 16 placed models");
        auto baked=bake_model_collision(load(asset),world.read(handle).transform,derived("darkangel.collision-body/"+collision.text()+"/"+object.text()+"/"+asset.text()));
        definition.meshes.push_back({++body,baked.data});
    }
    if(definition.meshes.empty())return std::nullopt;
    return encode_collision_source(definition);
}
bool publish_scene_collision(const SceneCollisionFile& file,std::string_view source){
    if(std::filesystem::exists(file.source)){std::ifstream current(file.source,std::ios::binary);const std::string previous{std::istreambuf_iterator<char>(current),{}};if(previous==source)return false;}
    publish_collision_source(file.source,source);return true;
}
}
