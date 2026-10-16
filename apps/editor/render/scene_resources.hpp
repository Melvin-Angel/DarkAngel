#pragma once
#include "gpu_resources.hpp"
#include <darkangel/assets.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace darkangel::editor_app {
class RenderSystem;
struct EditorOptions;
namespace render {
// The device resources made from cooked models for the editor's views: the main model, the models and skins the open
// scenes place, the models Play's actors wear, the projectile shape, the skin pipelines and palette buffer. Made after the
// render system's frame pipelines; released before it.
class SceneResources {
  public:
    SceneResources(RenderSystem&, const EditorOptions&, AssetId model, const ModelGeneration& lease);
    SceneResources(const SceneResources&) = delete;
    SceneResources& operator=(const SceneResources&) = delete;
    // Uploads a model or skin a scene places, once (the Controller's prepare_model), from the Play package or from the given
    // package of that model alone (its registry and content store).
    void prepare_model(AssetId, const std::filesystem::path& registry = {}, const std::filesystem::path& cas = {},bool refresh=false);
    void prepare_material(AssetId model,AssetId slot,const RuntimeMaterial&);
    std::map<AssetId,UploadedMaterial> native_materials;
    // The editor's main model, drawn when a scene places it.
    AssetId model() const {
        return id;
    }
    // Rebuilds the prop pipelines from shader text and uploads every model with them; committed at GPU idle.
    void reload_shader(const std::string& text, const ModelGeneration& lease);

    // A reload is uploaded beside the current resources, then committed at GPU idle.
    struct Reload {
        ModelGeneration candidate;
        UploadedModel resources;
        std::map<AssetId, UploadedModel> other;
        std::map<AssetId,UploadedMaterial> materials;
#ifdef DAE_ANIMATION
        std::map<AssetId, UploadedSkin> skins;
#endif
    };
    Reload prepare_reload(const ModelStore&);
    // Waits for the GPU and swaps the scene's models and skins.
    void commit_models(Reload&);
    // Publishes the candidate generation and swaps the main model.
    void commit_model(Reload&, ModelStore&, ModelGeneration& lease);

    // A projectile in flight is drawn as a small arrow-like solid built here, not from an asset.
    UploadedModel projectile_gpu;
    UploadedModel gpu;
    std::map<AssetId, UploadedModel> scene_models;
    // Models worn by the active Play actor (mask visuals); released when Play stops.
    std::map<AssetId, UploadedModel> worn_models;
    std::set<AssetId> worn_failed;
#ifdef DAE_ANIMATION
    RefCntAutoPtr<IBuffer> bone_buffer;
    RefCntAutoPtr<IPipelineState> skin_pso, skin_cull, skin_blend, skin_blend_cull;
    std::map<AssetId, UploadedSkin> scene_skins;
    std::unique_ptr<AnimationClip> diagnostic_clip;
#endif

  private:
    bool is_skin(AssetId, const std::filesystem::path& registry = {});
    RenderSystem& render_;
    const EditorOptions& args;
    AssetId id;
    std::shared_ptr<const RuntimeModel> refreshed_main;
    std::map<AssetId,std::shared_ptr<const RuntimeModel>> refreshed_models;
};
} // namespace render
} // namespace darkangel::editor_app
