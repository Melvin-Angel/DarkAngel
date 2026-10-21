#pragma once
#include "character_preview.hpp"
#include "render/gpu_resources.hpp"
#include <darkangel/animation_graph.hpp>
#include <memory>
#include <optional>
#include <vector>

namespace darkangel::editor_app {
class Controller;
class Shell;
class RenderSystem;
namespace render {
class SceneResources;
}
// The authoring pose previews: the Composer's frozen clip or action pose, the Animation tab's frozen graph pose and the
// Character tab's previewed skin. They sample native results while Play is stopped and never touch gameplay state.
struct PosePreviewContext {
    Controller& editor;
    Shell& shell;
    RenderSystem& render;
    render::SceneResources& scene;
    // Play's prepared character, which the Composer previews pose.
    const std::unique_ptr<CharacterPreviewResources>& resources;
};
class PosePreviews {
  public:
    explicit PosePreviews(PosePreviewContext);
    PosePreviews(const PosePreviews&) = delete;
    PosePreviews& operator=(const PosePreviews&) = delete;
    // The Shell's preview commands and panels, for an editor with Play resources and a project.
    void install();
    // A model reload drops the previews, which were made from the old resources.
    void reset();

    std::shared_ptr<render::UploadedSkin> authoring_skin;
    std::shared_ptr<render::UploadedSkin> ability_target_skin;
    CharacterDefinition authoring_character_definition;
    std::shared_ptr<GraphPosePreview> graph_pose;
    std::shared_ptr<ComposerPosePreview> composer_pose;
    std::vector<JointMatrix> composer_matrices;

  private:
    // A prepared Character, action or graph preview. Switching tabs or assets returns to a recent one without preparing it
    // again while its cooked sources are unchanged and its closure has no unsaved drafts (preview_cache_key).
    struct Prepared {
        std::string key;
        std::shared_ptr<ComposerPosePreview> composer;
        std::shared_ptr<GraphPosePreview> graph;
        std::shared_ptr<render::UploadedSkin> skin;
        CharacterDefinition definition;
        unsigned duration{};
    };
    Prepared current;
    std::vector<Prepared> cache;
    std::string cache_key(const char* kind, std::initializer_list<AssetId> roots);
    Prepared take_cached(const std::string& key);
    void stash();
    std::shared_ptr<render::UploadedSkin> upload(RuntimeSkinnedModel skin);
    GraphParameters graph_parameters(const GraphPosePreview& pose);
    // The tags a graph preview samples with; parameters point at it.
    ActorTagSnapshot preview_graph_tags;
    Controller& editor;
    Shell& shell;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice>& device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext>& context;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState>& skin_pso;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState>& skin_cull;
    const std::unique_ptr<CharacterPreviewResources>& character_resources;
};
} // namespace darkangel::editor_app
