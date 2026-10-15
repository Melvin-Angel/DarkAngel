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

    std::optional<render::UploadedSkin> authoring_skin;
    CharacterDefinition authoring_character_definition;
    std::unique_ptr<GraphPosePreview> graph_pose;
    std::unique_ptr<ComposerPosePreview> composer_pose;
    std::vector<JointMatrix> composer_matrices;

  private:
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
