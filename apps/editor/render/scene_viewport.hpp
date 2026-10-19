#pragma once
#include <RefCntAutoPtr.hpp>
#include <Texture.h>
#include <Buffer.h>
#include <PipelineState.h>
#include <ShaderResourceBinding.h>
#include <DirectXMath.h>
#include <darkangel/rendering.hpp>
#include <imgui.h>
#include "gpu_resources.hpp"
#include <cstddef>
#include <array>

namespace darkangel {
struct VfxView;
namespace editor_app {
class Controller;
class Shell;
class RenderSystem;
class VfxWorkspace;
class PlayController;
class PosePreviews;
struct EditorOptions;
struct ViewArea;
namespace render {
class SceneResources;
// What the scene viewport draws from. Play and the pose previews are absent in builds without character scenes.
struct SceneViewportContext {
    RenderSystem& render;
    SceneResources& scene;
    const EditorOptions& options;
    Controller& editor;
    Shell& shell;
    VfxWorkspace& vfx;
    PlayController* play{};
    PosePreviews* previews{};
};
// The Scene and Game viewport's frame: its render targets (and the second local player's), the camera of the open
// workspace, the scene's models, scattered models, characters and worn masks, and projectiles in flight.
class SceneViewport {
  public:
    explicit SceneViewport(SceneViewportContext);
    SceneViewport(const SceneViewport&) = delete;
    SceneViewport& operator=(const SceneViewport&) = delete;
    // Before the UI: the targets at the size the UI gave the viewports on the previous frame.
    void prepare_targets();
    ImTextureID scene_texture() const;
    // After the UI: the viewport's size for the next frame's target.
    void resize(const ViewArea& area);
    // Draws the frame into the viewport target and leaves it bound for the effects drawn against its depth.
    void render(double elapsed_seconds);
    // The frame's camera, for effects.
    VfxView vfx_view() const;
    bool second_target() const {
        return bool(scene_color_second);
    }
    static constexpr float background[4] = {.015f, .025f, .045f, 1};
    // What the last frames drew, for the end-of-run checks.
    std::size_t projectiles_seen{};
    unsigned scatter_drawn = 0;
    // CPU reference assignment for the current primary view. The renderer keeps this receipt so the editor and
    // later GPU assignment path can report occupancy and deterministic overflow without changing shading semantics.
    rendering::ClusterStats cluster_stats{};
    rendering::ClusterGpuStats cluster_gpu_stats{};

  private:
    SceneViewportContext context_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> scene_color, scene_display, scene_depth, scene_color_second, scene_display_second, scene_depth_second;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> post_buffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> shadow_instance_buffer;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> post_pso;
    struct SkyCache {
        Diligent::RefCntAutoPtr<Diligent::ITexture> texture;
        std::array<DirectX::XMFLOAT4, 7> settings{};
        bool valid{};
    };
    std::array<SkyCache, 2> sky_cache;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> sky_cache_pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> post_binding, post_binding_second;
    GpuClusterResources gpu_clusters;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> outline_pso, outline_cull, outline_skin_pso, outline_skin_cull;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> outline_instanced_pso, outline_instanced_cull;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> outline_instance_buffer;
    Diligent::RefCntAutoPtr<Diligent::ITexture> shadow_map, shadow_dummy, outline_info, outline_info_second, outline_object_id, outline_object_id_second;
    Diligent::RefCntAutoPtr<Diligent::ITexture> sky_texture, sky_fallback, sky_fallback_cube;
    AssetId sky_asset{};
    bool sky_texture_cube{};
    Diligent::RefCntAutoPtr<Diligent::ITextureView> shadow_srv;
    std::array<Diligent::RefCntAutoPtr<Diligent::ITextureView>,4> shadow_dsv{}, shadow_dummy_rtv{};
    unsigned scene_width = 700, scene_height = 500;
    // The viewport's field of view this frame.
    float camera_fov = DirectX::XM_PIDIV4;
    DirectX::XMFLOAT4X4 view_matrix{}, projection_matrix{};
    unsigned shadow_resolution{};
    void ensure_shadow_targets(unsigned resolution);
};
} // namespace render
} // namespace editor_app
} // namespace darkangel
