#include "scene_viewport.hpp"
#include <darkangel/scene_client.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/ability_presentation.hpp>
#include "render_system.hpp"
#include "scene_resources.hpp"
#include "render_settings.hpp"
#include "preview_routing.hpp"
#include "shadow_camera.hpp"
#include "static_index_batches.hpp"
#include "../editor_controller.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../scene_splines.hpp"
#include "../vfx_workspace.hpp"
#ifdef DAE_CHARACTER_SCENE
#include "../play_controller.hpp"
#include "../pose_previews.hpp"
#include "../runtime_inventory.hpp"
#endif
#include <darkangel/check.hpp>
#include <darkangel/vfx.hpp>
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace darkangel::editor_app::render {
using namespace Diligent;
SceneViewport::SceneViewport(SceneViewportContext context) : context_(context) {}
void SceneViewport::ensure_shadow_targets(unsigned resolution) {
    if (shadow_map && shadow_resolution == resolution)
        return;
    auto& device = context_.render.device;
    auto& context = context_.render.context;
    context->Flush();
    context->WaitForIdle();
    shadow_map.Release();
    shadow_dummy.Release();
    shadow_srv.Release();
    for (auto& view : shadow_dsv) view.Release();
    for (auto& view : shadow_dummy_rtv) view.Release();
    TextureDesc shadow;
    shadow.Name = "DarkAngel cascaded main-light shadows";
    shadow.Type = RESOURCE_DIM_TEX_2D_ARRAY;
    shadow.Width = shadow.Height = resolution;
    shadow.ArraySize = 4;
    shadow.Format = TEX_FORMAT_D32_FLOAT;
    shadow.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    device->CreateTexture(shadow, nullptr, &shadow_map);
    require(shadow_map, "Shadow map creation failed");
    shadow_srv = shadow_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    require(shadow_srv, "Shadow shader-resource view creation failed");
    for (unsigned cascade = 0; cascade < 4; ++cascade) {
        TextureViewDesc view;
        view.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
        view.Format = TEX_FORMAT_D32_FLOAT;
        view.FirstArraySlice = cascade;
        view.NumArraySlices = 1;
        shadow_map->CreateView(view, &shadow_dsv[cascade]);
        require(shadow_dsv[cascade], "Shadow cascade view creation failed");
    }
    TextureDesc dummy;
    dummy.Name = "DarkAngel shadow caster dummy color";
    dummy.Type = RESOURCE_DIM_TEX_2D_ARRAY;
    dummy.Width = dummy.Height = resolution;
    dummy.ArraySize = 4;
    dummy.Format = TEX_FORMAT_RGBA8_UNORM;
    dummy.BindFlags = BIND_RENDER_TARGET;
    device->CreateTexture(dummy, nullptr, &shadow_dummy);
    require(shadow_dummy, "Shadow caster dummy color creation failed");
    for (unsigned cascade = 0; cascade < 4; ++cascade) {
        TextureViewDesc view;
        view.ViewType = TEXTURE_VIEW_RENDER_TARGET;
        view.Format = TEX_FORMAT_RGBA8_UNORM;
        view.FirstArraySlice = cascade;
        view.NumArraySlices = 1;
        shadow_dummy->CreateView(view, &shadow_dummy_rtv[cascade]);
        require(shadow_dummy_rtv[cascade], "Shadow caster dummy color view creation failed");
    }
    shadow_resolution = resolution;
}
void SceneViewport::prepare_targets() {
    auto& device = context_.render.device;
    auto& context = context_.render.context;
    auto& swap_desc = context_.render.swap_desc;
    auto& shell = context_.shell;
    resize(shell.viewport_area());
    if (!scene_color || scene_color->GetDesc().Width != scene_width || scene_color->GetDesc().Height != scene_height) {
        context->Flush();
        context->WaitForIdle();
        TextureDesc color;
        color.Name = "Editor scene viewport";
        color.Type = RESOURCE_DIM_TEX_2D;
        color.Width = scene_width;
        color.Height = scene_height;
        color.Format = TEX_FORMAT_RGBA16_FLOAT;
        color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(color, nullptr, &scene_color);
        color.Name = "Editor scene outline information";
        color.Format = TEX_FORMAT_RGBA16_FLOAT;
        color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(color, nullptr, &outline_info);
        color.Name = "Editor scene per-object outline IDs";
        color.Format = TEX_FORMAT_R32_FLOAT;
        device->CreateTexture(color, nullptr, &outline_object_id);
        color.Name = "Editor scene display";
        color.Format = swap_desc.ColorBufferFormat;
        device->CreateTexture(color, nullptr, &scene_display);
        color.Name = "Editor scene depth";
        color.Format = swap_desc.DepthBufferFormat;
        color.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        device->CreateTexture(color, nullptr, &scene_depth);
        require(scene_color && outline_info && outline_object_id && scene_display && scene_depth, "Viewport resource creation failed");
    }
    // The second local player's view has its own target, sized to its viewport of the previous frame.
    if (shell.second_view) {
        const auto size = viewport_render_size(shell.workspace == Workspace::Game, shell.game_resolution,
                                               shell.area_second.width, shell.area_second.height);
        const unsigned wide = size.width, high = size.height;
        if (!scene_color_second || scene_color_second->GetDesc().Width != wide || scene_color_second->GetDesc().Height != high) {
            context->Flush();
            context->WaitForIdle();
            TextureDesc color;
            color.Name = "Editor second player viewport";
            color.Type = RESOURCE_DIM_TEX_2D;
            color.Width = wide;
            color.Height = high;
            color.Format = TEX_FORMAT_RGBA16_FLOAT;
            color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            device->CreateTexture(color, nullptr, &scene_color_second);
            color.Name = "Editor second viewport outline information";
            color.Format = TEX_FORMAT_RGBA16_FLOAT;
            color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            device->CreateTexture(color, nullptr, &outline_info_second);
            color.Name = "Editor second viewport per-object outline IDs";
            color.Format = TEX_FORMAT_R32_FLOAT;
            device->CreateTexture(color, nullptr, &outline_object_id_second);
            color.Name = "Editor second viewport display";
            color.Format = swap_desc.ColorBufferFormat;
            device->CreateTexture(color, nullptr, &scene_display_second);
            color.Name = "Editor second player depth";
            color.Format = swap_desc.DepthBufferFormat;
            color.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
            device->CreateTexture(color, nullptr, &scene_depth_second);
            require(scene_color_second && outline_info_second && outline_object_id_second && scene_display_second && scene_depth_second, "Second viewport resource creation failed");
        }
    }
    if (!shadow_map)
        ensure_shadow_targets(2048);
    if (!post_buffer) { BufferDesc desc;desc.Name="Editor post-process constants";desc.Usage=USAGE_DYNAMIC;desc.BindFlags=BIND_UNIFORM_BUFFER;desc.CPUAccessFlags=CPU_ACCESS_WRITE;desc.Size=sizeof(PostConstants);device->CreateBuffer(desc,nullptr,&post_buffer);require(post_buffer,"Post-process constants buffer creation failed"); }
    if (!sky_fallback) {
        const std::array<std::uint8_t,4> pixels{8,12,24,255};
        TextureDesc fallback;fallback.Name="DarkAngel sky fallback";fallback.Type=RESOURCE_DIM_TEX_2D;fallback.Width=fallback.Height=1;fallback.MipLevels=1;fallback.Format=TEX_FORMAT_RGBA8_UNORM;fallback.BindFlags=BIND_SHADER_RESOURCE;TextureSubResData sub{pixels.data(),4,0};TextureData data{&sub,1};device->CreateTexture(fallback,&data,&sky_fallback);require(sky_fallback,"Sky fallback texture creation failed");
        fallback.Name="DarkAngel sky cube fallback";fallback.Type=RESOURCE_DIM_TEX_CUBE;fallback.ArraySize=6;std::array<TextureSubResData,6> faces{};for(auto& face:faces)face={pixels.data(),4,0};data={faces.data(),static_cast<Uint32>(faces.size())};device->CreateTexture(fallback,&data,&sky_fallback_cube);require(sky_fallback_cube,"Sky cube fallback texture creation failed");
    }
    if (!post_pso) { post_pso=post_pipeline(device.RawPtr(),post_buffer.RawPtr(),swap_desc.ColorBufferFormat);post_pso->CreateShaderResourceBinding(&post_binding,true);post_pso->CreateShaderResourceBinding(&post_binding_second,true);require(post_binding&&post_binding_second,"Post-process binding creation failed"); }
    shell.game_image_second = shell.second_view && scene_display_second
                                  ? reinterpret_cast<ImTextureID>(scene_display_second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
                                  : ImTextureID{};
    shell.viewport_render_sizes[0] = {scene_color->GetDesc().Width, scene_color->GetDesc().Height};
    shell.viewport_render_sizes[1] = shell.second_view && scene_color_second
        ? RenderSize{scene_color_second->GetDesc().Width, scene_color_second->GetDesc().Height} : RenderSize{};
}
ImTextureID SceneViewport::scene_texture() const {
    return reinterpret_cast<ImTextureID>(scene_display->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}
void SceneViewport::resize(const ViewArea& area) {
    const auto& shell = context_.shell;
    const auto size = viewport_render_size(shell.workspace == Workspace::Game, shell.game_resolution, area.width, area.height);
    scene_width = size.width;
    scene_height = size.height;
}
void SceneViewport::render(double elapsed_seconds) {
    context_.shell.scene_visible = context_.shell.scene_culled = context_.shell.scene_draws = 0;
    context_.shell.render_sections = {};
    context_.shell.render_draws = {};
    context_.shell.render_optimizations = {};
    auto section_start = std::chrono::steady_clock::now();
    const auto section = [&](unsigned index) {
        const auto now = std::chrono::steady_clock::now();
        context_.shell.render_sections[index] = std::chrono::duration<double, std::milli>(now - section_start).count();
        section_start = now;
    };
    auto& device = context_.render.device;
    auto& context = context_.render.context;
    auto& constant_buffer = context_.render.constant_buffer;
    auto& pso = context_.render.pso;
    auto& pso_cull = context_.render.pso_cull;
    auto& scene = context_.scene;
    if(!shadow_instance_buffer){BufferDesc desc;desc.Name="Static shadow instance transforms";desc.Size=4096*sizeof(DirectX::XMFLOAT4X4);desc.Usage=USAGE_DYNAMIC;desc.BindFlags=BIND_VERTEX_BUFFER;desc.CPUAccessFlags=CPU_ACCESS_WRITE;device->CreateBuffer(desc,nullptr,&shadow_instance_buffer);require(shadow_instance_buffer,"Shadow instance buffer creation failed");}
    const auto id = scene.model();
    auto& gpu = scene.gpu;
    auto& scene_models = scene.scene_models;
    auto& worn_models = scene.worn_models;
    auto& worn_failed = scene.worn_failed;
    auto& projectile_gpu = scene.projectile_gpu;
#ifdef DAE_ANIMATION
    auto& bone_buffer = scene.bone_buffer;
    auto& skin_pso = scene.skin_pso;
    auto& skin_cull = scene.skin_cull;
    auto& scene_skins = scene.scene_skins;
    auto& diagnostic_clip = scene.diagnostic_clip;
#endif
    const auto& args = context_.options;
    auto& editor = context_.editor;
    auto& shell = context_.shell;
    const auto& vfx_preview = context_.vfx.state;
    if (!outline_pso) {
        const auto source = shader_text();
        outline_pso = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, false, source.c_str(), nullptr, false, false, true);
        outline_cull = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, true, source.c_str(), nullptr, false, false, true);
        outline_instanced_pso = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, false, source.c_str(), nullptr, false, true, true);
        outline_instanced_cull = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, true, source.c_str(), nullptr, false, true, true);
#ifdef DAE_ANIMATION
        const auto skin_source = skin_shader();
        outline_skin_pso = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, false, skin_source.c_str(), bone_buffer, false, false, true);
        outline_skin_cull = pipeline(device, constant_buffer, TEX_FORMAT_RGBA16_FLOAT, context_.render.swap_desc.DepthBufferFormat, true, skin_source.c_str(), bone_buffer, false, false, true);
#endif
    }
    struct OutlineInstance { DirectX::XMFLOAT4X4 world; float object_id; };
    static_assert(sizeof(OutlineInstance) == 68);
    if (!outline_instance_buffer) {
        BufferDesc desc; desc.Name = "Static outline transforms and object IDs";
        desc.Size = 4096 * sizeof(OutlineInstance); desc.Usage = USAGE_DYNAMIC;
        desc.BindFlags = BIND_VERTEX_BUFFER; desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(desc, nullptr, &outline_instance_buffer);
        require(outline_instance_buffer, "Outline instance buffer creation failed");
    }
#ifdef DAE_CHARACTER_SCENE
    auto& play = *context_.play;
    auto* const character = play.character;
    const auto& character_resources = play.character_resources;
    const auto character_player = play.character_player;
    const auto& travel = play.travel;
    const auto first_actor = [&] { return play.first_actor(); };
    auto& previews = *context_.previews;
    auto& authoring_skin = previews.authoring_skin;
    const auto& graph_pose = previews.graph_pose;
    const auto& composer_pose = previews.composer_pose;
    constexpr StableId ability_target_id{UINT64_MAX,UINT64_MAX};
    const auto& composer_matrices = previews.composer_matrices;
#endif
    const bool character_render =
#ifdef DAE_CHARACTER_SCENE
        shell.workspace == darkangel::editor_app::Workspace::Character && authoring_skin && shell.character_preview_asset == shell.character_selection;
#else
        false;
#endif
    const bool graph_render =
#ifdef DAE_CHARACTER_SCENE
        shell.workspace == darkangel::editor_app::Workspace::Animation && authoring_skin && graph_pose && shell.graph_preview_asset == shell.authored_selection;
#else
        false;
#endif
    const bool composer_render =
#ifdef DAE_CHARACTER_SCENE
        shell.composer_visible && (composer_pose || graph_pose) && !editor.preview.active() &&
        (shell.workspace == darkangel::editor_app::Workspace::Ability || shell.workspace == darkangel::editor_app::Workspace::Animation || character_render);
#else
        false;
#endif
    const bool gameplay_active = editor.preview.active() != nullptr;
    const bool isolated_preview = PreviewRouting::isolated_content(composer_render, character_render, graph_render, gameplay_active);
    const bool scene_camera_render = PreviewRouting::scene_camera_is_primary(
        shell.workspace == darkangel::editor_app::Workspace::Scene, gameplay_active);
#ifdef DAE_CHARACTER_SCENE
    const bool gameplay_camera_render = PreviewRouting::gameplay_camera_is_primary(
        gameplay_active, character != nullptr);
#else
    const bool gameplay_camera_render = false;
#endif
#ifdef DAE_CHARACTER_SCENE
    const UploadedSkin* preview_skin = authoring_skin ? &*authoring_skin : nullptr;
    if (!preview_skin && character_resources && scene_skins.contains(character_resources->skin))
        preview_skin = &scene_skins.at(character_resources->skin);
    const auto preview_center = preview_skin ? preview_skin->gpu.center : gpu.center;
    const auto preview_target = args.scene.empty() ? std::array<float, 3>{0.f, 0.f, 0.f} : preview_center;
    const float preview_radius = std::max(.25f, preview_skin ? preview_skin->gpu.radius : gpu.radius);
#else
    const auto preview_center = gpu.center;
    const float preview_radius = std::max(.25f, gpu.radius);
#endif
    auto* rtv = scene_color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = scene_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport viewport;
    viewport.Width = static_cast<float>(scene_color->GetDesc().Width);
    viewport.Height = static_cast<float>(scene_color->GetDesc().Height);
    context->SetViewports(1, &viewport, scene_color->GetDesc().Width, scene_color->GetDesc().Height);
    using namespace DirectX;
    auto view = XMMatrixLookAtRH(
        XMVectorSet(0, gpu.radius * .2f, gpu.radius * std::max(3.8f, 2.4f / (.4142f * viewport.Width / viewport.Height)), 1),
        XMVectorZero(), XMVectorSet(0, 1, 0, 0));
    if (!args.scene.empty())
        view = XMMatrixLookAtRH(XMVectorSet(26, 24, 34, 1), XMVectorSet(0, 1, 0, 1), XMVectorSet(0, 1, 0, 0));
    if (args.camera_close)
        view = XMMatrixLookAtRH(XMVectorSet(4, 2.5f, 8, 1), XMVectorSet(0, 1.3f, 3, 1), XMVectorSet(0, 1, 0, 0));
#ifdef DAE_CHARACTER_SCENE
    camera_fov = XM_PIDIV4;
    if (gameplay_camera_render) {
        const auto* play_camera = play.game_camera(elapsed_seconds);
        if (play_camera) {
            view = XMMatrixLookAtRH(
                XMVectorSet(float(play_camera->position[0]), float(play_camera->position[1]), float(play_camera->position[2]), 1),
                XMVectorSet(float(play_camera->target[0]), float(play_camera->target[1]), float(play_camera->target[2]), 1),
                XMVectorSet(0, 1, 0, 0));
            camera_fov = float(play_camera->fov_degrees) * XM_PI / 180.f;
        }
    } else if (!scene_camera_render && shell.workspace == darkangel::editor_app::Workspace::Player && shell.camera_preview_visible && shell.camera_preview_rig &&
               shell.camera_preview >= 0 && shell.camera_preview < static_cast<int>(shell.camera_preview_rig->cameras.size())) {
        // Authoring preview: one drafted virtual camera around the scene's player placement.
        CameraTargets targets;
        if (character_resources) {
            auto entity = editor.displayed().find(character_resources->player);
            if (editor.displayed().valid(entity)) {
                auto placed = editor.displayed().read(entity).transform;
                targets.follow = {placed.x, placed.y, placed.z};
            }
        }
        CameraDirector preview(*shell.camera_preview_rig, shell.camera_preview_yaw * XM_PI / 180., shell.camera_preview_pitch);
        auto camera = preview.solve(static_cast<unsigned>(shell.camera_preview), targets);
        view = XMMatrixLookAtRH(XMVectorSet(float(camera.position[0]), float(camera.position[1]), float(camera.position[2]), 1),
                                XMVectorSet(float(camera.target[0]), float(camera.target[1]), float(camera.target[2]), 1),
                                XMVectorSet(0, 1, 0, 0));
        camera_fov = float(camera.fov_degrees) * XM_PI / 180.f;
    }
#endif
#ifdef DAE_CHARACTER_SCENE
    if (isolated_preview) {
        // All authoring previews orbit their uploaded skin, never the active scene
        // player or the scene camera.  The radius keeps newly selected characters
        // framed without a per-asset camera hack.
        const float distance = std::max(shell.composer_distance, preview_radius * 2.8f);
        float horizontal = distance * std::cos(shell.composer_pitch);
        view = XMMatrixLookAtRH(XMVectorSet(horizontal * std::sin(shell.composer_yaw),
                                            preview_target[1] + distance * std::sin(shell.composer_pitch),
                                            horizontal * std::cos(shell.composer_yaw), 1),
                                XMVectorSet(preview_target[0], preview_target[1], preview_target[2], 1), XMVectorSet(0, 1, 0, 0));
    }
#endif
    if (shell.workspace == darkangel::editor_app::Workspace::Vfx) {
        const auto& preview = vfx_preview;
        view = XMMatrixLookAtRH(XMVectorSet(std::sin(preview.yaw) * preview.distance, preview.height + preview.distance * .25f,
                                            std::cos(preview.yaw) * preview.distance, 1),
                                XMVectorSet(0, preview.height, 0, 1), XMVectorSet(0, 1, 0, 0));
        camera_fov = XM_PIDIV4;
    }
    // R4 Prefabs workspace: the selected definition (or its instance in context), from an orbit camera around it.
    const bool prefab_render = shell.workspace == darkangel::editor_app::Workspace::Prefabs && shell.prefab_preview.visible && !editor.preview.active();
    if (prefab_render) {
        const auto& preview = shell.prefab_preview;
        const float distance = float(std::max(3.0, preview.radius * 2.6)), cx = float(preview.center[0]), cy = float(preview.center[1]), cz = float(preview.center[2]);
        view = XMMatrixLookAtRH(XMVectorSet(cx + std::sin(preview.yaw) * std::cos(preview.pitch) * distance, cy + std::sin(preview.pitch) * distance,
                                            cz + std::cos(preview.yaw) * std::cos(preview.pitch) * distance, 1),
                                XMVectorSet(cx, cy, cz, 1), XMVectorSet(0, 1, 0, 0));
        camera_fov = XM_PIDIV4;
    }
    const float camera_radius = isolated_preview ? preview_radius : gpu.radius;
    auto projection = XMMatrixPerspectiveFovRH(camera_fov, viewport.Width / viewport.Height, camera_radius * .01f, camera_radius * 40);
    if (scene_camera_render) {
        // Use the exact matrices used by this frame's UI picking/drop rays, also during viewport resize.
        view=XMLoadFloat4x4(&shell.scene_view);
        projection=XMLoadFloat4x4(&shell.scene_projection);
    }
    XMStoreFloat4x4(&view_matrix, view);
    XMStoreFloat4x4(&projection_matrix, projection);
    if(composer_render){shell.composer_view=view_matrix;shell.composer_projection=projection_matrix;}
    if (gameplay_camera_render) {
        shell.game_view = view_matrix;
        shell.game_projection = projection_matrix;
    }
    if (!args.hidden||args.exercise_scene_camera)
        shell.gizmo(editor, view_matrix, projection_matrix);
    // Placed models, then the characters of spawn points: at the spawn point while authoring, as the spawned actor in Play.
    std::vector<std::pair<StableId, AssetId>> drawn_objects(editor.document->plan().models.begin(), editor.document->plan().models.end());
    // Scatter: models along splines, derived each frame from the points as they stand. They are not scene objects.
    std::map<StableId, darkangel::Transform> scatter_poses;
    scatter_drawn = 0;
    for (const auto& built : darkangel::editor_app::build_splines(editor, shell))
        if (built.curve && built.definition.scatter) {
            const auto model = built.definition.scatter->model;
            if (model != id && !scene_models.contains(model) && editor.prepare_model) {
                try {
                    editor.prepare_model(model);
                } catch (const std::exception&) {
                }
            }
            if (model != id && !scene_models.contains(model))
                continue;
            unsigned index = 0;
            for (const auto& instance : darkangel::scatter_along(*built.curve, *built.definition.scatter, built.definition.closed)) {
                darkangel::Transform pose;
                pose.x = instance.position.x;
                pose.y = instance.position.y;
                pose.z = instance.position.z;
                pose.yaw = instance.yaw;
                pose.scale = 1;
                const auto derived = darkangel::scatter_instance(built.id, index++);
                scatter_poses.emplace(derived, pose);
                drawn_objects.emplace_back(derived, model);
                ++scatter_drawn;
            }
        }
#ifdef DAE_CHARACTER_SCENE
    if (character_resources && scene_skins.contains(character_resources->skin)) {
        for (const auto& [point, npc] : editor.document->plan().spawns)
            drawn_objects.emplace_back(point, character_resources->skin);
        for (const auto& spawn : character_resources->spawns)
            drawn_objects.emplace_back(spawn.actor, spawn.skin);
        if (character)
            for (auto actor : character->actors())
                if (character->actor_encounter(actor))
                    drawn_objects.emplace_back(actor, character_resources->skin);
        if (character)
            for (const auto member : character->players())
                if (member != character_resources->player)
                    drawn_objects.emplace_back(member, character_resources->skin);
    }
#endif
    if (isolated_preview) {
        // A preview actor is synthetic and lives only in this frame.  Keeping the
        // display target shared is fine; sharing scene-owned content is not.
        drawn_objects.clear();
        scatter_poses.clear();
#ifdef DAE_CHARACTER_SCENE
        if (character_resources && character_resources->player && scene_skins.contains(character_resources->skin)) {
            drawn_objects.emplace_back(character_resources->player, character_resources->skin);
            darkangel::Transform pose;
            pose.scale = 1;
            scatter_poses.emplace(character_resources->player, pose);
            if(shell.workspace==Workspace::Ability&&previews.ability_target_skin){drawn_objects.emplace_back(ability_target_id,character_resources->skin);pose.z=shell.ability_target_distance;pose.yaw=DirectX::XM_PI;scatter_poses.emplace(ability_target_id,pose);}
        }
#endif
    }
    // The Prefabs preview draws its own models (and context models) at their resolved poses instead of the scene's objects.
    if (prefab_render) {
        drawn_objects.clear();
        scatter_poses.clear();
        for (const auto& shown : shell.prefab_preview.models) {
            drawn_objects.emplace_back(shown.object, shown.model);
            scatter_poses.emplace(shown.object, shown.pose);
        }
    }
#ifdef DAE_CHARACTER_SCENE
    using SceneViewer = darkangel::SceneClient;
#else
    struct SceneViewer;
#endif
    struct FrameLight { darkangel::rendering::SceneLight light; std::array<float,3> world_position{}; };
    render::ProjectRenderingSettings project_rendering;
    if (editor.assets) {
        try { project_rendering = render::load_project_rendering_settings(editor.assets->source_root()); }
        catch (const std::exception&) { /* The Lighting tab reports malformed project defaults; keep the safe built-in defaults here. */ }
    }
    render::SceneRenderingSettings frame_settings = project_rendering.scene;
    rendering::ToonLighting material_toon_defaults = project_rendering.material_toon;
    bool scene_toon_override = false;
    std::vector<FrameLight> frame_lights;
    if (!isolated_preview) {
        // Play's presentation world omits static scene entities. Their rendering
        // profile and lights still belong to this scene, just like its scenery.
        for (const auto& origin : editor.document->plan().origins) {
            const auto* settings_world = &editor.displayed();
            auto handle = settings_world->find(origin.object);
            if (!settings_world->valid(handle)) { settings_world = &editor.document->world(); handle = settings_world->find(origin.object); }
            if (!settings_world->valid(handle)) continue;
            const auto object = settings_world->read(handle);
            if (object.optional_json.empty()) continue;
            try {
                const auto source = nlohmann::json::parse(object.optional_json);
                if (source.contains("rendering")) {
                    frame_settings = render::merge_rendering_settings(project_rendering.scene, source.at("rendering"));
                    scene_toon_override = source.at("rendering").value("toon", nlohmann::json{}).is_object();
                    if (scene_toon_override) material_toon_defaults = frame_settings.toon;
                }
                if (source.contains("light")) {
                    const auto authored = render::decode_light(source.at("light"));
                    FrameLight made;
                    made.light.id = object.id; made.light.type = authored.type; made.light.enabled = authored.enabled;
                    made.light.cast_shadows = authored.cast_shadows; made.light.color = authored.color; made.light.intensity = authored.intensity;
                    made.light.range = authored.range; made.light.inner_cone_degrees = authored.inner_cone_degrees;
                    made.light.outer_cone_degrees = authored.outer_cone_degrees; made.light.falloff = authored.falloff;
                    made.world_position = {float(object.transform.x), float(object.transform.y), float(object.transform.z)};
                    const auto rotation = XMMatrixRotationRollPitchYaw(float(object.transform.pitch), float(object.transform.yaw), float(object.transform.roll));
                    // Authored directional transforms point along the rays the light emits.
                    // Shading and the cascade camera both consume a vector toward the sun.
                    auto direction = XMVector3TransformNormal(XMVectorSet(0, -1, 0, 0), rotation);
                    if (authored.type == rendering::LightType::Directional) direction = XMVectorNegate(direction);
                    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(made.light.direction_view.data()), XMVector3Normalize(direction));
                    frame_lights.push_back(made);
                }
            } catch (const std::exception&) {}
        }
    }
    if(isolated_preview){
        // A neutral authoring rig, independent of the scene's sky, exposure and
        // light placements. This changes only this isolated render route.
        frame_settings.sky_enabled=false;frame_settings.sky_color={.08f,.11f,.16f};frame_settings.fog.enabled=false;
        frame_settings.shadows.cascades_enabled=false;frame_settings.post=rendering::PostProcessSettings{};
        frame_settings.sky.sun_color={1,1,1};frame_settings.sky.sun_intensity=2.5f;frame_settings.sky.sun_direction={-.35f,.7f,.6f};
        frame_settings.toon.ambient_color={.42f,.46f,.55f};frame_settings.toon.ambient_strength=.8f;
        material_toon_defaults.ambient_color=frame_settings.toon.ambient_color;material_toon_defaults.ambient_strength=frame_settings.toon.ambient_strength;
    }
    const FrameLight* main_light = nullptr;
    for (const auto& prepared : frame_lights)
        if (prepared.light.id == frame_settings.main_light && prepared.light.type == rendering::LightType::Directional && prepared.light.enabled) { main_light = &prepared; break; }
    if (!main_light)
        for (const auto& prepared : frame_lights)
            if (prepared.light.type == rendering::LightType::Directional && prepared.light.enabled) { main_light = &prepared; break; }
    if (!main_light) {
        FrameLight sun;
        sun.light.type = rendering::LightType::Directional;
        sun.light.enabled = true;
        sun.light.cast_shadows = true;
        sun.light.color = frame_settings.sky.sun_color;
        sun.light.intensity = frame_settings.sky.sun_intensity;
        sun.light.direction_view = frame_settings.sky.sun_direction;
        frame_lights.push_back(sun);
        main_light = &frame_lights.back();
    }
    std::vector<darkangel::rendering::SceneLight> cluster_lights;
    cluster_lights.reserve(frame_lights.size());
    const auto frame_view = XMLoadFloat4x4(&view_matrix);
    for (const auto& prepared : frame_lights) {
        auto light = prepared.light;
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(light.position_view.data()),
                      XMVector3TransformCoord(XMVectorSet(prepared.world_position[0], prepared.world_position[1], prepared.world_position[2], 1), frame_view));
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(light.direction_view.data()),
                      XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(prepared.light.direction_view.data())), frame_view)));
        cluster_lights.push_back(light);
    }
    const unsigned render_width = scene_color->GetDesc().Width, render_height = scene_color->GetDesc().Height;
    const rendering::ClusterView cluster_view{render_width, render_height, camera_radius * .01f, camera_radius * 40.f,
                                               std::tan(camera_fov * .5f), float(render_width) / std::max(1u, render_height)};
    const rendering::ClusterConfig cluster_config = frame_settings.clusters;
    // GPU shading consumes GPU-selected indices. Only the reference shading
    // route needs the CPU's per-cell vectors and sorting.
    const auto cluster_assignment = frame_settings.gpu_clustered ? rendering::ClusterAssignment{} :
        rendering::assign_lights(cluster_config, cluster_view, cluster_lights);
    cluster_stats = cluster_assignment.stats;
    context_.shell.cluster_stats = cluster_stats;
    // Feed the GPU in the same deterministic influence/StableId order as the CPU reference. The graphics shader reads
    // the resulting selected indices directly; CPU assignment remains available as an independent test/reference path.
    std::vector<ClusterGpuLight> gpu_cluster_lights;
    gpu_cluster_lights.reserve(cluster_lights.size());
    std::vector<std::size_t> gpu_light_order(cluster_lights.size());
    std::iota(gpu_light_order.begin(), gpu_light_order.end(), 0);
    const auto gpu_influence = [&](const rendering::SceneLight& light) {
        return std::max(0.f, light.intensity) * (light.color.r * .2126f + light.color.g * .7152f + light.color.b * .0722f);
    };
    std::stable_sort(gpu_light_order.begin(), gpu_light_order.end(), [&](std::size_t a, std::size_t b) {
        const auto sa = gpu_influence(cluster_lights[a]), sb = gpu_influence(cluster_lights[b]);
        return sa != sb ? sa > sb : cluster_lights[a].id < cluster_lights[b].id;
    });
    for (const auto index : gpu_light_order) {
        const auto& light = cluster_lights[index];
        if (light.type == rendering::LightType::Directional)
            continue;
        ClusterGpuLight packed;
        packed.position_range = {light.position_view[0], light.position_view[1], light.position_view[2], light.range};
        packed.color_intensity = {light.color.r, light.color.g, light.color.b, light.intensity};
        packed.direction_type = {light.direction_view[0], light.direction_view[1], light.direction_view[2], light.type == rendering::LightType::Spot ? 1.f : 0.f};
        packed.cone_falloff = {std::cos(light.outer_cone_degrees * XM_PI / 180.f), std::cos(light.inner_cone_degrees * XM_PI / 180.f), light.falloff, light.enabled ? 1.f : 0.f};
        packed.metadata = {static_cast<float>(static_cast<unsigned>(light.type)), light.enabled ? 1.f : 0.f, 0, 0};
        gpu_cluster_lights.push_back(packed);
    }
    const auto gpu_layout = rendering::cluster_layout(cluster_config, cluster_view);
    ensure_gpu_clusters(device.RawPtr(), context.RawPtr(), gpu_clusters, gpu_layout.tiles_x, gpu_layout.tiles_y, gpu_layout.slices,
                        cluster_config.max_candidates, cluster_config.shading_budget, static_cast<unsigned>(gpu_cluster_lights.size()));
    if (frame_settings.gpu_clustered) read_gpu_cluster_stats(context.RawPtr(), gpu_clusters, cluster_gpu_stats);
    else cluster_gpu_stats = {};
    context_.shell.cluster_gpu_stats = cluster_gpu_stats;
    ClusterGpuFrame gpu_frame;
    gpu_frame.dimensions = {gpu_layout.tiles_x, gpu_layout.tiles_y, gpu_layout.slices, static_cast<unsigned>(gpu_cluster_lights.size())};
    gpu_frame.limits = {std::min(64u, std::max(1u, cluster_config.max_candidates)),
                        std::min(4u, std::max(1u, cluster_config.shading_budget)), static_cast<unsigned>(gpu_layout.clusters), 0};
    gpu_frame.camera = {cluster_view.near_plane, cluster_view.far_plane, cluster_view.tan_half_fov_y, cluster_view.aspect};
    if (frame_settings.gpu_clustered) dispatch_gpu_clusters(context.RawPtr(), gpu_clusters, gpu_cluster_lights, gpu_frame);
    const auto cluster_for_center = [&](FXMVECTOR center) -> const rendering::ClusterCell* {
        if (frame_settings.gpu_clustered || cluster_assignment.cells.empty()) return nullptr;
        const auto view_position = XMVector3TransformCoord(center, frame_view);
        const float depth = -XMVectorGetZ(view_position);
        if (depth <= cluster_view.near_plane || depth > cluster_view.far_plane)
            return nullptr;
        const auto clip = XMVector3TransformCoord(view_position, XMLoadFloat4x4(&projection_matrix));
        const float ndc_x = XMVectorGetX(clip), ndc_y = XMVectorGetY(clip);
        if (ndc_x < -1.f || ndc_x > 1.f || ndc_y < -1.f || ndc_y > 1.f)
            return nullptr;
        const unsigned x = std::min(cluster_assignment.tiles_x - 1, unsigned(std::clamp((ndc_x * .5f + .5f) * cluster_assignment.tiles_x, 0.f, float(cluster_assignment.tiles_x - 1))));
        const unsigned y = std::min(cluster_assignment.tiles_y - 1, unsigned(std::clamp((.5f - ndc_y * .5f) * cluster_assignment.tiles_y, 0.f, float(cluster_assignment.tiles_y - 1))));
        const float log_range = std::log(cluster_view.far_plane / cluster_view.near_plane);
        const unsigned z = std::min(cluster_assignment.slices - 1, unsigned(std::max(0.f, std::log(depth / cluster_view.near_plane) / std::max(.001f, log_range) * cluster_assignment.slices)));
        return &cluster_assignment.at(x, y, z);
    };
    const float sky[4]={frame_settings.sky_enabled?frame_settings.sky_color.r:background[0],frame_settings.sky_enabled?frame_settings.sky_color.g:background[1],frame_settings.sky_enabled?frame_settings.sky_color.b:background[2],1};context->ClearRenderTarget(rtv,sky,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ensure_shadow_targets(frame_settings.shadows.resolution);
    std::array<XMFLOAT4X4,4> shadow_matrices{};std::array<float,4> shadow_splits{1,1,1,1};const unsigned shadow_cascades=std::clamp(frame_settings.shadows.cascade_count,1u,4u);const float shadow_near=std::max(.05f,gpu.radius*.01f),shadow_far=std::max(shadow_near+1.f,frame_settings.shadows.distance);for(unsigned split=0;split<shadow_cascades;++split){const float p=float(split+1)/shadow_cascades;const float logarithmic=std::pow(p,.65f);const float fraction=std::clamp(p*(1-frame_settings.shadows.split_distribution)+logarithmic*frame_settings.shadows.split_distribution,0.f,1.f);shadow_splits[split]=shadow_near+(shadow_far-shadow_near)*fraction;}bool shadow_enabled=false;
    // Resolve each placement once per presentation world, shared by all this
    // frame's cascades/color/outline passes. No state survives into the next frame.
    std::map<std::pair<const World*, StableId>, std::optional<Transform>> frame_poses;
    // One pass over the scene's objects for a view. viewer: the client whose world and poses are shown; null for the editor's own.
    auto draw_objects = [&](const XMMATRIX& view, const XMMATRIX& projection, const SceneViewer* viewer, bool shadow_pass = false, bool outline_pass = false, bool object_mask_pass = false, const ShadowCamera* caster_volume = nullptr) {
        struct PassTimer {
            Shell& shell; unsigned index, draws;
            std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
            ~PassTimer() {
                shell.render_sections[index + 1] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                shell.render_draws[index] += shell.scene_draws - draws;
            }
        } timer{shell, shadow_pass ? 0u : (outline_pass || object_mask_pass ? 2u : 1u), shell.scene_draws};
        std::map<StableId, float> outline_object_ids;
        if (outline_pass || object_mask_pass) {
            for (const auto& [object_key, model] : drawn_objects) {
                (void)model;
                outline_object_ids.try_emplace(object_key, 0.f);
            }
            float next_object_id = 1;
            for (auto& [object_key, object_value] : outline_object_ids) {
                (void)object_key;
                object_value = next_object_id++;
            }
        }
        const darkangel::World* shown_world = &editor.displayed();
        (void)viewer;
#ifdef DAE_CHARACTER_SCENE
        if (viewer)
            shown_world = &viewer->presentation();
#endif
        std::vector<std::pair<StableId, darkangel::MotorVec>> mover_centers;
        const darkangel::WorldStateView* world_view = nullptr;
        const darkangel::CharacterSceneCombat* world_content = nullptr;
#ifdef DAE_CHARACTER_SCENE
        if (character && editor.preview.active())
            if (const auto* viewer = character->client(character_player)) {
                mover_centers = viewer->movers();
                world_view = viewer->world_state();
                world_content = viewer->combat();
            }
#endif
        // View frustum planes (clip space 0 <= z <= w), each normalized, for culling placements by their bounding sphere.
        XMFLOAT4X4 clip_matrix;
        XMStoreFloat4x4(&clip_matrix, view * projection);
        const auto& c = clip_matrix;
        std::array<XMVECTOR, 6> planes{XMVectorSet(c._14 + c._11, c._24 + c._21, c._34 + c._31, c._44 + c._41),
                                       XMVectorSet(c._14 - c._11, c._24 - c._21, c._34 - c._31, c._44 - c._41),
                                       XMVectorSet(c._14 + c._12, c._24 + c._22, c._34 + c._32, c._44 + c._42),
                                       XMVectorSet(c._14 - c._12, c._24 - c._22, c._34 - c._32, c._44 - c._42),
                                       XMVectorSet(c._13, c._23, c._33, c._43),
                                       XMVectorSet(c._14 - c._13, c._24 - c._23, c._34 - c._33, c._44 - c._43)};
        for (auto& plane : planes)
            plane = XMPlaneNormalize(plane);
        // Transparent static surfaces collected while drawing, drawn last and sorted back to front (R5 A3).
        struct TransparentDraw {
            float depth;
            Constants constants;
            IPipelineState* pipeline;
            IShaderResourceBinding* binding;
            IBuffer *vertices, *indices;
            std::uint32_t first, count;
        };
        std::vector<TransparentDraw> transparent;
        struct BatchKey {
            AssetId model;
            std::map<AssetId, AssetId> overrides;
            auto operator<=>(const BatchKey&) const = default;
        };
        std::map<BatchKey,std::vector<XMFLOAT4X4>> shadow_instances;
        std::map<BatchKey,std::vector<XMFLOAT4X4>> color_instances;
        std::map<BatchKey,std::vector<float>> instance_object_ids;
        // Pipeline, material binding and buffers carry over between consecutive draws that share them.
        IPipelineState* bound_pipeline = nullptr;
        IShaderResourceBinding* bound_material = nullptr;
        IBuffer* bound_vertices = nullptr;
        for (const auto& [object_id, model_id] : drawn_objects) {
            const auto key = std::pair{shown_world, object_id};
            auto cached = frame_poses.find(key);
            if (cached == frame_poses.end()) {
            const darkangel::World* object_world = shown_world;
            auto entity = shown_world->find(object_id);
            const auto scattered = scatter_poses.find(object_id);
#ifdef DAE_CHARACTER_SCENE
            // Static scenery is not a session object: in Play it is drawn where the scene places it.
            if (!shown_world->valid(entity) && scattered == scatter_poses.end() && shown_world != &editor.document->world() &&
                darkangel::static_scenery(editor.document->plan(), object_id,
                                          character_resources ? character_resources->skin : darkangel::AssetId{})) {
                object_world = &editor.document->world();
                entity = object_world->find(object_id);
            }
#endif
            std::optional<Transform> resolved;
            if (scattered != scatter_poses.end()) resolved = scattered->second;
            else if (object_world->valid(entity)) resolved = object_world->read(entity).transform;
            cached = frame_poses.emplace(key, resolved).first;
            }
            if (!cached->second) continue;
#ifdef DAE_CHARACTER_SCENE
            if (isolated_preview && object_id!=ability_target_id &&
                (model_id != character_resources->skin || (character_resources->player && object_id != character_resources->player)))
                continue;
#endif
            auto pose = *cached->second;
            if (world_view &&
                std::find(world_view->collected.begin(), world_view->collected.end(), object_id) != world_view->collected.end())
                continue;
            if (world_content)
                for (const auto& [mover, center] : mover_centers)
                    if (mover == object_id)
                        for (const auto& placed : world_content->movers)
                            if (placed.id == mover) {
                                pose.x += center.x - placed.start.x;
                                pose.y += center.y - placed.start.y;
                                pose.z += center.z - placed.start.z;
                            }
#ifdef DAE_CHARACTER_SCENE
            if (isolated_preview && object_id!=ability_target_id) {
                pose = {};
                pose.scale = 1;
            }
#endif
            if (shell.draft && editor.selected == object_id)
                pose = *shell.draft;

#ifdef DAE_ANIMATION
            auto skin = scene_skins.find(model_id);
            auto* selected_skin = skin == scene_skins.end() ? nullptr : &skin->second;
#ifdef DAE_CHARACTER_SCENE
            if (isolated_preview && authoring_skin)
                selected_skin = &*authoring_skin;
            if(isolated_preview&&object_id==ability_target_id&&previews.ability_target_skin)selected_skin=&*previews.ability_target_skin;
#endif
            auto* drawn_ptr = selected_skin ? &selected_skin->gpu : nullptr;
            if (drawn_ptr && !args.skin_reference) {
                const std::vector<JointMatrix>* gameplay_pose = nullptr;
#ifdef DAE_CHARACTER_SCENE
                if (composer_render || graph_render)
                    gameplay_pose = &composer_matrices;
                else if (character && selected_skin->model.rig.definition.id == character_resources->rig.definition.id &&
                         selected_skin->model.rig.definition.signature == character_resources->rig.definition.signature)
                    gameplay_pose = viewer ? viewer->pose(object_id) : character->pose(object_id);
#endif
                if(isolated_preview&&object_id==ability_target_id)upload_palette(context,bone_buffer,*selected_skin,nullptr,0,nullptr);
                else upload_palette(context, bone_buffer, *selected_skin, diagnostic_clip.get(), args.pose_tick, gameplay_pose);
            }
#else
            UploadedModel* drawn_ptr = nullptr;
#endif
            // A placed model whose upload failed is not drawn rather than ending the frame.
            if (!drawn_ptr && model_id != id && !scene_models.contains(model_id))
                continue;
            auto& drawn = drawn_ptr ? *drawn_ptr : (model_id == id ? gpu : scene_models.at(model_id));
            auto rotation = XMMatrixRotationRollPitchYaw(pose.pitch, pose.yaw, pose.roll);
            auto world =
                (args.scene.empty() ? XMMatrixTranslation(-drawn.center[0], -drawn.center[1], -drawn.center[2]) : XMMatrixIdentity()) *
                XMMatrixScaling(pose.scale, pose.scale, pose.scale) * rotation * XMMatrixTranslation(pose.x, pose.y, pose.z);
            // Static models outside the view are not submitted. Skinned models animate past their rest bounds, so they always draw.
            if (!drawn_ptr) {
                const auto& half = drawn.half;
                const auto center = XMVector3TransformCoord(XMVectorSet(drawn.center[0], drawn.center[1], drawn.center[2], 1), world);
                // Project the transformed box onto each plane. Long walls and
                // terrain tiles have much tighter bounds than their spheres.
                const auto axis_x = XMVectorScale(world.r[0], half[0]);
                const auto axis_y = XMVectorScale(world.r[1], half[1]);
                const auto axis_z = XMVectorScale(world.r[2], half[2]);
                const bool in_frustum = std::all_of(planes.begin(), planes.end(), [&](FXMVECTOR plane) {
                    const float extent = std::abs(XMVectorGetX(XMVector3Dot(plane, axis_x))) +
                        std::abs(XMVectorGetX(XMVector3Dot(plane, axis_y))) + std::abs(XMVectorGetX(XMVector3Dot(plane, axis_z)));
                    return XMVectorGetX(XMPlaneDotCoord(plane, center)) >= -extent;
                });
                if (!in_frustum) {
                    ++shell.scene_culled;
                    continue;
                }
                if (caster_volume && !args.render_reference && !shadow_caster_visible(*caster_volume, world, drawn.center, drawn.half)) {
                    ++shell.scene_culled;
                    ++shell.render_optimizations[2];
                    continue;
                }
            }
            ++shell.scene_visible;
            const auto& batch_material_plan=editor.preview.active()?editor.preview.active()->plan:editor.document->plan();
            const bool assigned_material=batch_material_plan.materials.contains(object_id);
            BatchKey batch_key{model_id};
            if (assigned_material) batch_key.overrides = batch_material_plan.materials.at(object_id);
            const auto batch_material = [&](AssetId slot) -> const Material& {
                const auto assignment = batch_key.overrides.find(slot);
                return assignment == batch_key.overrides.end() ? drawn.materials.at(slot) : scene.native_materials.at(assignment->second).surface;
            };
            bool has_transparency=false;
            if(!drawn_ptr)for(const auto& mesh:drawn.meshes)for(const auto& part:mesh.parts)if(batch_material(part.material).value.alpha_mode==2)has_transparency=true;
            const auto* cluster_cell = shadow_pass ? nullptr : cluster_for_center(XMVector3TransformCoord(XMVectorSet(drawn.center[0], drawn.center[1], drawn.center[2], 1), world));
            const bool can_batch = !drawn_ptr && !prefab_render && (!assigned_material || !args.render_reference);
            if(shadow_pass&&can_batch&&!has_transparency){XMFLOAT4X4 instance;XMStoreFloat4x4(&instance,world);shadow_instances[batch_key].push_back(instance);continue;}
            const bool has_object_local_lights = cluster_cell && std::any_of(frame_lights.begin(), frame_lights.end(), [&](const auto& prepared) {
                return prepared.light.id != frame_settings.main_light && prepared.light.type != darkangel::rendering::LightType::Directional &&
                       prepared.light.enabled && std::find(cluster_cell->selected.begin(), cluster_cell->selected.end(), prepared.light.id) != cluster_cell->selected.end();
            });
            if (!shadow_pass && !object_mask_pass && can_batch && !has_transparency && (outline_pass || !has_object_local_lights)) {
                XMFLOAT4X4 instance; XMStoreFloat4x4(&instance, world); color_instances[batch_key].push_back(instance);
                if (outline_pass) instance_object_ids[batch_key].push_back(outline_object_ids.at(object_id));
                continue;
            }
            for (const auto& mesh : drawn.meshes) {
                IBuffer* vb = mesh.vertices;
                if (vb != bound_vertices) {
                    Uint64 offset{};
                    context->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
                    context->SetIndexBuffer(mesh.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    bound_vertices = vb;
                }
                for (const auto& part : mesh.parts) {
                    const Material* effective=&drawn.materials.at(part.material);
                    bool material_override=false;
                    const auto& material_plan=editor.preview.active()?editor.preview.active()->plan:editor.document->plan();
                    if(!prefab_render)if(const auto object=material_plan.materials.find(object_id);object!=material_plan.materials.end())if(const auto assignment=object->second.find(part.material);assignment!=object->second.end()){
                        const auto& native=scene.native_materials.at(assignment->second);
#ifdef DAE_ANIMATION
                        effective=drawn_ptr&&!args.skin_reference?&native.skinned:&native.surface;
                        material_override=true;
#else
                        effective=&native.surface;
#endif
                    }
                    if(prefab_render)for(const auto& shown:shell.prefab_preview.models)if(shown.object==object_id)if(const auto assignment=shown.materials.find(part.material);assignment!=shown.materials.end()){effective=&scene.native_materials.at(assignment->second).surface;material_override=true;}
                    const auto& material=*effective;
                    CookedMaterial shading_value=material.value;
                    const bool material_toon=material.value.family==material_family_toon_standard||material.value.family==material_family_toon_character||material.value.family==material_family_toon_terrain;
                    if(material_toon&&!material_override){
                        if(project_rendering.material_properties_override){for(unsigned channel=0;channel<4;++channel) shading_value.color[channel]*=project_rendering.material_color[channel];shading_value.roughness=project_rendering.material_roughness;shading_value.metallic=project_rendering.material_metallic;}
                        shading_value.toon_thresholds=material_toon_defaults.band_thresholds;
                        shading_value.toon_softness=material_toon_defaults.transition_softness;
                        shading_value.shadow_tint={material_toon_defaults.shadow_tint.r,material_toon_defaults.shadow_tint.g,material_toon_defaults.shadow_tint.b};
                        shading_value.ambient_color={material_toon_defaults.ambient_color.r,material_toon_defaults.ambient_color.g,material_toon_defaults.ambient_color.b};
                        shading_value.ambient_strength=material_toon_defaults.ambient_strength;
                        shading_value.local_light_strength=material_toon_defaults.local_light_strength;
                    }
                    // Transparent surfaces do not cast into the opaque shadow map. Cutouts use the same authored Albedo and
                    // alpha cutoff as the forward material through a dedicated depth-only pixel shader.
                    if (shadow_pass && material.value.alpha_mode == 2)
                        continue;
                    if(shadow_pass){Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);XMStoreFloat4x4(&constants.world,world);
#ifdef DAE_ANIMATION
                        IPipelineState* shadow_pipeline=(drawn_ptr&&!args.skin_reference)?(material.value.alpha_mode==1?(material.value.double_sided?scene.skin_shadow_alpha_pso.RawPtr():scene.skin_shadow_alpha_cull.RawPtr()):(material.value.double_sided?scene.skin_shadow_pso.RawPtr():scene.skin_shadow_cull.RawPtr())):(material.value.alpha_mode==1?(material.value.double_sided?context_.render.shadow_alpha_pso.RawPtr():context_.render.shadow_alpha_pso_cull.RawPtr()):(material.value.double_sided?context_.render.shadow_pso.RawPtr():context_.render.shadow_pso_cull.RawPtr()));
#else
                        IPipelineState* shadow_pipeline=material.value.alpha_mode==1?(material.value.double_sided?context_.render.shadow_alpha_pso.RawPtr():context_.render.shadow_alpha_pso_cull.RawPtr()):(material.value.double_sided?context_.render.shadow_pso.RawPtr():context_.render.shadow_pso_cull.RawPtr());
#endif
                        void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Shadow constants mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                        const bool alpha_shadow = material.value.alpha_mode == 1;
                        IShaderResourceBinding* empty_shadow_binding = nullptr;
#ifdef DAE_ANIMATION
                        if (!alpha_shadow)
                            empty_shadow_binding = drawn_ptr && !args.skin_reference
                                                       ? (material.value.double_sided ? scene.skin_shadow_binding.RawPtr() : scene.skin_shadow_cull_binding.RawPtr())
                                                       : (material.value.double_sided ? context_.render.shadow_binding.RawPtr() : context_.render.shadow_cull_binding.RawPtr());
#else
                        if (!alpha_shadow)
                            empty_shadow_binding = material.value.double_sided ? context_.render.shadow_binding.RawPtr() : context_.render.shadow_cull_binding.RawPtr();
#endif
                        if(shadow_pipeline!=bound_pipeline){context->SetPipelineState(shadow_pipeline);bound_pipeline=shadow_pipeline;if(!alpha_shadow){require(empty_shadow_binding,"Missing opaque shadow resource binding");context->CommitShaderResources(empty_shadow_binding,RESOURCE_STATE_TRANSITION_MODE_NONE);}}
                        if(material.value.alpha_mode==1){
                            apply_material(constants,shading_value);
                            constants.color = {material.value.color[0] * mesh.color[0], material.value.color[1] * mesh.color[1],
                                material.value.color[2] * mesh.color[2], material.value.color[3] * mesh.color[3]};
                            constants.outline = {1, 1, 0, 0}; // alpha test and normal only; dummy color is not shaded
                            context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Alpha shadow constants remapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
                            require(material.shadow_binding,"Missing alpha shadow material binding");
                            auto* shadow=material.shadow_binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap");if(shadow)shadow->Set(shadow_srv);bind_gpu_clusters(material.shadow_binding,gpu_clusters);context->CommitShaderResources(material.shadow_binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        }
                        DrawIndexedAttribs shadow_draw;shadow_draw.NumIndices=part.count;shadow_draw.IndexType=VT_UINT32;shadow_draw.FirstIndexLocation=part.first;shadow_draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(shadow_draw);++shell.scene_draws;continue;}
                    Constants constants;
                    XMStoreFloat4x4(&constants.mvp, world * view * projection);
                    XMStoreFloat4x4(&constants.rotation, rotation);
                    XMStoreFloat4x4(&constants.world, world);
                    XMStoreFloat4x4(&constants.view, view);
                    constants.cluster_dimensions = {float(gpu_layout.tiles_x), float(gpu_layout.tiles_y), float(gpu_layout.slices), frame_settings.gpu_clustered ? 1.f : 0.f};
                    constants.cluster_camera = {cluster_view.near_plane, cluster_view.far_plane, cluster_view.tan_half_fov_y, cluster_view.aspect};
                    constants.cluster_viewport = {float(render_width), float(render_height), material_toon ? shading_value.local_light_strength : frame_settings.toon.local_light_strength, context_.render.device->GetDeviceInfo().NDC.YtoVScale};
                    constants.color = {material.value.color[0] * mesh.color[0], material.value.color[1] * mesh.color[1],
                                       material.value.color[2] * mesh.color[2], material.value.color[3] * mesh.color[3]};
                    apply_material(constants, shading_value);
                    if (outline_pass || object_mask_pass) {
                        if (!frame_settings.post.outline.enabled || material.value.alpha_mode == 2)
                            continue;
                        auto [outline_id, inserted] = outline_object_ids.try_emplace(object_id, static_cast<float>(outline_object_ids.size() + 1));
                        (void)inserted;
                        constants.outline = {1.f, outline_id->second, 0, 0};
                        void* mapped{};
                        context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                        require(mapped, "Outline constants mapping failed");
                        std::memcpy(mapped, &constants, sizeof(constants));
                        context->UnmapBuffer(constant_buffer, MAP_WRITE);
#ifdef DAE_ANIMATION
                        IPipelineState* outline_pipeline = (drawn_ptr && !args.skin_reference)
                                                               ? (material.value.double_sided ? outline_skin_pso.RawPtr() : outline_skin_cull.RawPtr())
                                                               : (material.value.double_sided ? outline_pso.RawPtr() : outline_cull.RawPtr());
#else
                        IPipelineState* outline_pipeline = material.value.double_sided ? outline_pso.RawPtr() : outline_cull.RawPtr();
#endif
                        if (outline_pipeline != bound_pipeline) { context->SetPipelineState(outline_pipeline); bound_pipeline = outline_pipeline; }
                        if (auto* shadow = material.binding->GetVariableByName(SHADER_TYPE_PIXEL, "ShadowMap")) shadow->Set(shadow_srv);
                        bind_gpu_clusters(material.binding, gpu_clusters);
                        context->CommitShaderResources(material.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        DrawIndexedAttribs outline_draw; outline_draw.NumIndices = part.count; outline_draw.IndexType = VT_UINT32; outline_draw.FirstIndexLocation = part.first; outline_draw.Flags = DRAW_FLAG_VERIFY_ALL;
                        context->DrawIndexed(outline_draw);
                        ++shell.scene_draws;
                        continue;
                    }
                    if (main_light) {
                        constants.main_light = {main_light->light.direction_view[0], main_light->light.direction_view[1], main_light->light.direction_view[2], 1};
                        constants.main_color = {main_light->light.color.r, main_light->light.color.g, main_light->light.color.b, main_light->light.intensity};
                    }
                    if(!material_toon){constants.ambient={frame_settings.toon.ambient_color.r*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.g*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.b*frame_settings.toon.ambient_strength,1};constants.toon_bands={frame_settings.toon.band_thresholds[0],frame_settings.toon.band_thresholds[1],frame_settings.toon.band_thresholds[2],frame_settings.toon.transition_softness};constants.shadow_tint={frame_settings.toon.shadow_tint.r,frame_settings.toon.shadow_tint.g,frame_settings.toon.shadow_tint.b,1};}
                    constants.fog_color={frame_settings.fog.color.r,frame_settings.fog.color.g,frame_settings.fog.color.b,frame_settings.fog.enabled?1.f:0.f};constants.fog_params={frame_settings.fog.start,frame_settings.fog.end,frame_settings.fog.density,frame_settings.fog.exponential?1.f:0.f};
                    constants.shadow_splits={shadow_splits[0],shadow_splits[1],shadow_splits[2],shadow_splits[3]};constants.shadow_params={frame_settings.shadows.depth_bias,frame_settings.shadows.normal_bias,shadow_enabled?float(shadow_cascades):0.f,1.f/std::max(1u,shadow_resolution)};for(unsigned cascade=0;cascade<4;++cascade)constants.shadow_matrices[cascade]=shadow_matrices[cascade];if(auto* shadow=material.binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap"))shadow->Set(shadow_srv);
                    const float local_strength=material_toon?shading_value.local_light_strength:frame_settings.toon.local_light_strength;unsigned local_index=0;const auto inverse_rotation=XMMatrixInverse(nullptr,rotation);for(const auto&prepared:frame_lights)if(prepared.light.id!=frame_settings.main_light&&prepared.light.type!=darkangel::rendering::LightType::Directional&&prepared.light.enabled&&local_index<4&&cluster_cell&&std::find(cluster_cell->selected.begin(),cluster_cell->selected.end(),prepared.light.id)!=cluster_cell->selected.end()){const auto p=XMVector3TransformCoord(XMVectorSet(prepared.world_position[0]-float(pose.x),prepared.world_position[1]-float(pose.y),prepared.world_position[2]-float(pose.z),1),inverse_rotation);const auto direction=XMVector3TransformNormal(XMVectorSet(prepared.light.direction_view[0],prepared.light.direction_view[1],prepared.light.direction_view[2],0),inverse_rotation);XMFLOAT3 local,local_direction;XMStoreFloat3(&local,p);XMStoreFloat3(&local_direction,XMVector3Normalize(direction));constants.local_positions[local_index]={local.x,local.y,local.z,prepared.light.range};constants.local_colors[local_index]={prepared.light.color.r,prepared.light.color.g,prepared.light.color.b,prepared.light.intensity*local_strength};constants.local_directions[local_index]={local_direction.x,local_direction.y,local_direction.z,prepared.light.type==darkangel::rendering::LightType::Spot?1.f:0.f};constants.local_cones[local_index]={std::cos(prepared.light.outer_cone_degrees*XM_PI/360.f*2.f),std::cos(prepared.light.inner_cone_degrees*XM_PI/360.f*2.f),prepared.light.falloff,0};++local_index;}
                    void* mapped{};
                    context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                    require(mapped, "Uniform buffer mapping failed");
                    std::memcpy(mapped, &constants, sizeof(constants));
                    context->UnmapBuffer(constant_buffer, MAP_WRITE);

#ifdef DAE_ANIMATION
                    IPipelineState* pipeline = (drawn_ptr && !args.skin_reference)
                                                   ? (material.value.double_sided ? skin_pso.RawPtr() : skin_cull.RawPtr())
                                                   : (material.value.double_sided ? pso.RawPtr() : pso_cull.RawPtr());
#else
                    IPipelineState* pipeline = material.value.double_sided ? pso.RawPtr() : pso_cull.RawPtr();
#endif
                    // Transparent surfaces (R5 A3) use the blending pipelines. Static ones are drawn after every opaque surface,
                    // farthest first; a skinned one is drawn in place, because its pose palette belongs to this object only.
                    if (material.value.alpha_mode == 2) {
                        const bool skinned_draw = drawn_ptr && !args.skin_reference;
#ifdef DAE_ANIMATION
                        if (skinned_draw)
                            pipeline = material.value.double_sided ? scene.skin_blend.RawPtr() : scene.skin_blend_cull.RawPtr();
                        else
#endif
                            pipeline = material.value.double_sided ? context_.render.pso_blend.RawPtr() : context_.render.pso_blend_cull.RawPtr();
                        if (!skinned_draw) {
                            transparent.push_back({XMVectorGetZ(XMVector3TransformCoord(world.r[3], view)), constants, pipeline, material.binding.RawPtr(),
                                                   mesh.vertices.RawPtr(), mesh.indices.RawPtr(), part.first, part.count});
                            continue;
                        }
                    }
                    // The Frame buffer is dynamic: each draw's mapped constants reach the GPU without committing the binding again.
                    if (pipeline != bound_pipeline) {
                        context->SetPipelineState(pipeline);
                        bound_pipeline = pipeline;
                        bound_material = nullptr;
                    }
                    if (material.binding.RawPtr() != bound_material) {
                        bind_gpu_clusters(material.binding, gpu_clusters);
                        context->CommitShaderResources(material.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        bound_material = material.binding.RawPtr();
                    }
                    DrawIndexedAttribs draw;
                    draw.NumIndices = part.count;
                    draw.IndexType = VT_UINT32;
                    draw.FirstIndexLocation = part.first;
#ifdef _DEBUG
                    draw.Flags = DRAW_FLAG_VERIFY_ALL;
#endif
                    context->DrawIndexed(draw);
                    ++shell.scene_draws;
                }
            }
#ifdef DAE_CHARACTER_SCENE
            const darkangel::editor_app::WornMask* wearing = nullptr;
            if (character && character_resources) {
                if (object_id == character_player || (!args.mask_tune.empty() && character_resources->combat && object_id == first_actor()))
                    wearing = darkangel::editor_app::RuntimeInventory::worn(*character_resources, character->ability());
                else if (auto observed = character->observer_if_ready(object_id, double(character->motor().tick)))
                    wearing = darkangel::editor_app::RuntimeInventory::worn(*character_resources, observed->frame.ability);
            }
            if (wearing && shell.workspace == darkangel::editor_app::Workspace::Game) {
                auto worn = *wearing;
                const auto* worn_pose = viewer ? viewer->pose(object_id) : character->pose(object_id);
                if (!args.mask_tune.empty()) {
                    std::array<float, 7> tune{};
                    std::stringstream stream(args.mask_tune);
                    std::string item;
                    for (unsigned index = 0; index < 7 && std::getline(stream, item, ','); ++index)
                        tune[index] = std::stof(item);
                    worn.position = {tune[0], tune[1], tune[2]};
                    worn.rotation = {tune[3], tune[4], tune[5]};
                    worn.scale = tune[6];
                }
                if (!worn_models.contains(worn.model) && !worn_failed.contains(worn.model)) {
                    try {
                        worn_models.emplace(worn.model,
                                            upload(device, load_cooked_model(args.registry, args.cas, worn.model), pso, pso_cull,
                                                   context_.render.shadow_alpha_pso, context_.render.shadow_alpha_pso_cull));
                    } catch (const std::exception& error) {
                        worn_failed.insert(worn.model);
                        editor.log(std::string("Mask visual unavailable: ") + error.what(), darkangel::editor_app::ConsoleSeverity::Error);
                    }
                }
                auto uploaded = worn_models.find(worn.model);
                if (uploaded != worn_models.end() && worn_pose && worn.joint < worn_pose->size()) {
                    XMFLOAT4X4 joint;
                    std::memcpy(&joint, (*worn_pose)[worn.joint].values.data(), sizeof(joint));
                    const float radians = XM_PI / 180.f;
                    auto mask_world =
                        XMMatrixScaling(worn.scale, worn.scale, worn.scale) *
                        XMMatrixRotationRollPitchYaw(worn.rotation[0] * radians, worn.rotation[1] * radians, worn.rotation[2] * radians) *
                        XMMatrixTranslation(worn.position[0], worn.position[1], worn.position[2]) * XMLoadFloat4x4(&joint) * world;
                    auto mask_rotation = mask_world;
                    mask_rotation.r[3] = XMVectorSet(0, 0, 0, 1);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        mask_rotation.r[axis] = XMVector3Normalize(mask_rotation.r[axis]);
                    // The mask binds its own state: the next placement binds everything again.
                    bound_pipeline = nullptr;
                    bound_material = nullptr;
                    bound_vertices = nullptr;
                    for (const auto& mesh : uploaded->second.meshes) {
                        IBuffer* vb = mesh.vertices;
                        Uint64 offset{};
                        context->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                                  SET_VERTEX_BUFFERS_FLAG_RESET);
                        context->SetIndexBuffer(mesh.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        for (const auto& part : mesh.parts) {
                            const auto& material = uploaded->second.materials.at(part.material);
                            Constants constants;
                            XMStoreFloat4x4(&constants.mvp, mask_world * view * projection);
                            XMStoreFloat4x4(&constants.rotation, mask_rotation);
                             XMStoreFloat4x4(&constants.world, mask_world);
                             XMStoreFloat4x4(&constants.view, view);
                             constants.cluster_dimensions = {float(gpu_layout.tiles_x), float(gpu_layout.tiles_y), float(gpu_layout.slices), frame_settings.gpu_clustered ? 1.f : 0.f};
                             constants.cluster_camera = {cluster_view.near_plane, cluster_view.far_plane, cluster_view.tan_half_fov_y, cluster_view.aspect};
                             constants.cluster_viewport = {float(render_width), float(render_height), frame_settings.toon.local_light_strength, args.backend == "vulkan" ? 1.f : 0.f};
                            constants.color = {material.value.color[0] * mesh.color[0] * worn.tint[0],
                                               material.value.color[1] * mesh.color[1] * worn.tint[1],
                                               material.value.color[2] * mesh.color[2] * worn.tint[2],
                                               material.value.color[3] * mesh.color[3] * worn.tint[3]};
                            apply_material(constants, material.value);
                            void* mapped{};
                            context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                            require(mapped, "Uniform buffer mapping failed");
                            std::memcpy(mapped, &constants, sizeof(constants));
                            context->UnmapBuffer(constant_buffer, MAP_WRITE);
                            context->SetPipelineState(pso.RawPtr());
                            bind_gpu_clusters(material.binding, gpu_clusters);
                            context->CommitShaderResources(material.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            DrawIndexedAttribs draw;
                            draw.NumIndices = part.count;
                            draw.IndexType = VT_UINT32;
                            draw.FirstIndexLocation = part.first;
                            draw.Flags = DRAW_FLAG_VERIFY_ALL;
                            context->DrawIndexed(draw);
                        }
                    }
                }
            } else if (!character && !worn_models.empty()) {
                worn_models.clear();
                worn_failed.clear();
            }
#endif
        }
        if(!color_instances.empty()) {
            std::vector<XMFLOAT4X4> packed_instances; std::map<BatchKey,Uint32> instance_starts;
            for(const auto& [batch, instances] : color_instances) { instance_starts.emplace(batch, static_cast<Uint32>(packed_instances.size())); packed_instances.insert(packed_instances.end(), instances.begin(), instances.end()); }
            require(packed_instances.size() <= 4096, "Static scene color instance count exceeds the viewport buffer");
            auto* instance_buffer = outline_pass ? outline_instance_buffer.RawPtr() : shadow_instance_buffer.RawPtr();
            const auto instance_stride = outline_pass ? sizeof(OutlineInstance) : sizeof(XMFLOAT4X4);
            void* mapped{}; context->MapBuffer(instance_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped); require(mapped, "Scene color instance mapping failed");
            if (outline_pass) {
                auto* destination = static_cast<OutlineInstance*>(mapped);
                for (const auto& [batch, instances] : color_instances)
                    for (std::size_t i = 0; i < instances.size(); ++i)
                        *destination++ = {instances[i], instance_object_ids.at(batch)[i]};
            } else std::memcpy(mapped, packed_instances.data(), packed_instances.size() * sizeof(XMFLOAT4X4));
            context->UnmapBuffer(instance_buffer, MAP_WRITE);
            for(const auto& [batch, instances] : color_instances) {
                const auto model_id = batch.model;
                auto* uploaded = model_id == id ? &gpu : scene_models.contains(model_id) ? &scene_models.at(model_id) : nullptr;
                if(!uploaded) continue;
                const Uint64 offsets[]{0, Uint64(instance_starts.at(batch)) * instance_stride}; IBuffer* buffers[]{nullptr, instance_buffer};
                for(const auto& mesh : uploaded->meshes) {
                    buffers[0] = mesh.vertices; context->SetVertexBuffers(0, 2, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
                    context->SetIndexBuffer(mesh.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    for(const auto& part : mesh.parts) {
                        const auto assignment = batch.overrides.find(part.material);
                        const bool material_override = assignment != batch.overrides.end();
                        const auto& material = material_override ? scene.native_materials.at(assignment->second).surface : uploaded->materials.at(part.material);
                        if (outline_pass && material.value.alpha_mode == 2) continue;
                        if(material.value.alpha_mode == 2) continue;
                        CookedMaterial shading = material.value;
                        const bool toon = shading.family == material_family_toon_standard || shading.family == material_family_toon_character || shading.family == material_family_toon_terrain;
                        if(toon && !material_override) {
                            if(project_rendering.material_properties_override) { for(unsigned channel=0;channel<4;++channel) shading.color[channel] *= project_rendering.material_color[channel]; shading.roughness=project_rendering.material_roughness; shading.metallic=project_rendering.material_metallic; }
                            shading.toon_thresholds=material_toon_defaults.band_thresholds; shading.toon_softness=material_toon_defaults.transition_softness;
                            shading.shadow_tint={material_toon_defaults.shadow_tint.r,material_toon_defaults.shadow_tint.g,material_toon_defaults.shadow_tint.b};
                            shading.ambient_color={material_toon_defaults.ambient_color.r,material_toon_defaults.ambient_color.g,material_toon_defaults.ambient_color.b};
                            shading.ambient_strength=material_toon_defaults.ambient_strength; shading.local_light_strength=material_toon_defaults.local_light_strength;
                        }
                        Constants constants; XMStoreFloat4x4(&constants.mvp, view * projection); XMStoreFloat4x4(&constants.view, view);
                        constants.cluster_dimensions={float(gpu_layout.tiles_x),float(gpu_layout.tiles_y),float(gpu_layout.slices),frame_settings.gpu_clustered?1.f:0.f};
                        constants.cluster_camera={cluster_view.near_plane,cluster_view.far_plane,cluster_view.tan_half_fov_y,cluster_view.aspect};
                        constants.cluster_viewport={float(render_width),float(render_height),toon?shading.local_light_strength:frame_settings.toon.local_light_strength,context_.render.device->GetDeviceInfo().NDC.YtoVScale};
                        constants.color={material.value.color[0]*mesh.color[0],material.value.color[1]*mesh.color[1],material.value.color[2]*mesh.color[2],material.value.color[3]*mesh.color[3]};
                        apply_material(constants,shading);
                        if (outline_pass) constants.outline = {1, 0, 0, 0};
                        if(main_light) { constants.main_light={main_light->light.direction_view[0],main_light->light.direction_view[1],main_light->light.direction_view[2],1}; constants.main_color={main_light->light.color.r,main_light->light.color.g,main_light->light.color.b,main_light->light.intensity}; }
                        if(!toon) { constants.ambient={frame_settings.toon.ambient_color.r*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.g*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.b*frame_settings.toon.ambient_strength,1}; constants.toon_bands={frame_settings.toon.band_thresholds[0],frame_settings.toon.band_thresholds[1],frame_settings.toon.band_thresholds[2],frame_settings.toon.transition_softness}; constants.shadow_tint={frame_settings.toon.shadow_tint.r,frame_settings.toon.shadow_tint.g,frame_settings.toon.shadow_tint.b,1}; }
                        constants.fog_color={frame_settings.fog.color.r,frame_settings.fog.color.g,frame_settings.fog.color.b,frame_settings.fog.enabled?1.f:0.f}; constants.fog_params={frame_settings.fog.start,frame_settings.fog.end,frame_settings.fog.density,frame_settings.fog.exponential?1.f:0.f};
                        constants.shadow_splits={shadow_splits[0],shadow_splits[1],shadow_splits[2],shadow_splits[3]}; constants.shadow_params={frame_settings.shadows.depth_bias,frame_settings.shadows.normal_bias,shadow_enabled?float(shadow_cascades):0.f,1.f/std::max(1u,shadow_resolution)}; for(unsigned cascade=0;cascade<4;++cascade) constants.shadow_matrices[cascade]=shadow_matrices[cascade];
                        if(auto* shadow=material.binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap")) shadow->Set(shadow_srv);
                        void* constant_mapped{}; context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,constant_mapped); require(constant_mapped,"Instanced color constants mapping failed"); std::memcpy(constant_mapped,&constants,sizeof(constants)); context->UnmapBuffer(constant_buffer,MAP_WRITE);
                        context->SetPipelineState(outline_pass ? (material.value.double_sided ? outline_instanced_pso.RawPtr() : outline_instanced_cull.RawPtr()) : (material.value.double_sided?scene.instanced_color_pso.RawPtr():scene.instanced_color_cull.RawPtr()));
                        bind_gpu_clusters(material.binding,gpu_clusters); context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        DrawIndexedAttribs draw; draw.NumIndices=part.count; draw.NumInstances=static_cast<Uint32>(instances.size()); draw.IndexType=VT_UINT32; draw.FirstIndexLocation=part.first; context->DrawIndexed(draw); ++shell.scene_draws;
                    }
                }
            }
        }
        if (shadow_pass && !shadow_instances.empty()) {
            std::vector<XMFLOAT4X4> packed_instances;
            std::map<BatchKey, Uint32> instance_starts;
            for (const auto& [batch, instances] : shadow_instances) {
                instance_starts.emplace(batch, static_cast<Uint32>(packed_instances.size()));
                packed_instances.insert(packed_instances.end(), instances.begin(), instances.end());
            }
            require(packed_instances.size() <= 4096, "Static shadow instance count exceeds the viewport buffer");
            void* mapped{};
            context->MapBuffer(shadow_instance_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
            require(mapped, "Shadow instance mapping failed");
            std::memcpy(mapped, packed_instances.data(), packed_instances.size() * sizeof(XMFLOAT4X4));
            context->UnmapBuffer(shadow_instance_buffer, MAP_WRITE);
            struct Submission {
                const GpuMesh* mesh;
                const Material* material;
                StaticIndexRange range;
                Uint32 start, instances;
            };
            std::vector<Submission> submissions;
            for (const auto& [batch, instances] : shadow_instances) {
                const auto model_id = batch.model;
                const auto* uploaded = model_id == id ? &gpu : scene_models.contains(model_id) ? &scene_models.at(model_id) : nullptr;
                if (!uploaded) continue;
                for (const auto& mesh : uploaded->meshes) {
                    std::vector<StaticIndexRange> ranges;
                    const auto material_for = [&](AssetId slot) -> const Material& {
                        const auto assignment = batch.overrides.find(slot);
                        return assignment == batch.overrides.end() ? uploaded->materials.at(slot) : scene.native_materials.at(assignment->second).surface;
                    };
                    for (std::size_t part = 0; part < mesh.parts.size(); ++part) {
                        const auto& value = mesh.parts[part];
                        const auto& material = material_for(value.material).value;
                        if (material.alpha_mode == 2) continue;
                        ranges.push_back({value.first, value.count, part, material.alpha_mode == 1, material.double_sided});
                    }
                    const auto joined = args.render_reference ? ranges : shadow_index_batches(ranges);
                    shell.render_optimizations[3] += static_cast<unsigned>(ranges.size() - joined.size());
                    for (const auto& range : joined)
                        submissions.push_back({&mesh, &material_for(mesh.parts[range.part].material), range,
                            instance_starts.at(batch), static_cast<Uint32>(instances.size())});
                }
            }
            // Depth-only opaque draws share constants and bindings across models.
            // Alpha-tested ranges keep their own material and never merge across it.
            std::stable_sort(submissions.begin(), submissions.end(), [](const Submission& a, const Submission& b) {
                return std::pair{a.range.cutout, a.range.double_sided} < std::pair{b.range.cutout, b.range.double_sided};
            });
            IPipelineState* last_pipeline = nullptr;
            bool opaque_constants_uploaded = false;
            for (const auto& submission : submissions) {
                const auto& material = *submission.material;
                const auto& range = submission.range;
                auto* pipeline = range.cutout ? (range.double_sided ? scene.instanced_shadow_alpha_pso.RawPtr() : scene.instanced_shadow_alpha_cull.RawPtr())
                    : (range.double_sided ? scene.instanced_shadow_pso.RawPtr() : scene.instanced_shadow_cull.RawPtr());
                if (range.cutout || !opaque_constants_uploaded || args.render_reference) {
                    Constants constants{};
                    XMStoreFloat4x4(&constants.mvp, view * projection);
                    if (range.cutout) {
                        apply_material(constants, material.value);
                        constants.color = {material.value.color[0] * submission.mesh->color[0], material.value.color[1] * submission.mesh->color[1],
                            material.value.color[2] * submission.mesh->color[2], material.value.color[3] * submission.mesh->color[3]};
                        constants.outline = {1, 1, 0, 0};
                    } else opaque_constants_uploaded = true;
                    context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                    require(mapped, "Instanced shadow constants mapping failed");
                    std::memcpy(mapped, &constants, sizeof(constants)); context->UnmapBuffer(constant_buffer, MAP_WRITE);
                }
                const bool pipeline_changed = pipeline != last_pipeline;
                if (pipeline_changed) { context->SetPipelineState(pipeline); last_pipeline = pipeline; }
                if (range.cutout) {
                    if (auto* shadow = material.shadow_binding->GetVariableByName(SHADER_TYPE_PIXEL, "ShadowMap")) shadow->Set(shadow_srv);
                    bind_gpu_clusters(material.shadow_binding, gpu_clusters);
                    context->CommitShaderResources(material.shadow_binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                } else if (pipeline_changed || args.render_reference)
                    context->CommitShaderResources(context_.render.shadow_binding, RESOURCE_STATE_TRANSITION_MODE_NONE);
                const Uint64 offsets[]{0, Uint64(submission.start) * sizeof(XMFLOAT4X4)};
                IBuffer* buffers[]{submission.mesh->vertices, shadow_instance_buffer};
                context->SetVertexBuffers(0, 2, buffers, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
                context->SetIndexBuffer(submission.mesh->indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                DrawIndexedAttribs draw;
                draw.NumIndices = range.count; draw.NumInstances = submission.instances;
                draw.IndexType = VT_UINT32; draw.FirstIndexLocation = range.first;
                context->DrawIndexed(draw); ++shell.scene_draws;
            }
        }
        std::sort(transparent.begin(), transparent.end(), [](const TransparentDraw& a, const TransparentDraw& b) { return a.depth < b.depth; });
        for (const auto& item : transparent) {
            void* mapped{};
            context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
            require(mapped, "Uniform buffer mapping failed");
            std::memcpy(mapped, &item.constants, sizeof(item.constants));
            context->UnmapBuffer(constant_buffer, MAP_WRITE);
            context->SetPipelineState(item.pipeline);
            IBuffer* vb = item.vertices;
            Uint64 offset{};
            context->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
            context->SetIndexBuffer(item.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->CommitShaderResources(item.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            DrawIndexedAttribs draw;
            draw.NumIndices = item.count;
            draw.IndexType = VT_UINT32;
            draw.FirstIndexLocation = item.first;
            context->DrawIndexed(draw);
            ++shell.scene_draws;
        }
    };
#ifdef DAE_CHARACTER_SCENE
    // While the party travels the editor's document and resources may already be the destination's; the loading panel covers the view.
    const bool travelling = travel && travel->busy();
#else
    const bool travelling = false;
#endif
    section(0);
    if (main_light && frame_settings.shadows.cascades_enabled) {
        if (main_light->light.enabled && main_light->light.cast_shadows) {
                // Surface lighting uses a vector TO the sun; the shadow camera
                // looks FROM the sun along the opposite ray direction.
                const auto direction = XMVectorNegate(XMVector3Normalize(XMVectorSet(main_light->light.direction_view[0], main_light->light.direction_view[1],
                                                                       main_light->light.direction_view[2], 0)));
                for (unsigned cascade = 0; cascade < shadow_cascades; ++cascade) {
                    const auto camera = shadow_camera(view, projection, direction,
                        cascade ? shadow_splits[cascade - 1] : shadow_near, shadow_splits[cascade],
                        frame_settings.shadows.distance, shadow_resolution, !args.render_reference);
                    const auto& light_view = camera.view;
                    const auto& light_projection = camera.projection;
                    XMStoreFloat4x4(&shadow_matrices[cascade], light_view * light_projection);
                    auto* dsv_shadow = shadow_dsv[cascade].RawPtr();
                    auto* dummy_rtv = shadow_dummy_rtv[cascade].RawPtr();
                    context->SetRenderTargets(1, &dummy_rtv, dsv_shadow, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    const float dummy_clear[4]{0, 0, 0, 0};
                    context->ClearRenderTarget(dummy_rtv, dummy_clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->ClearDepthStencil(dsv_shadow, CLEAR_DEPTH_FLAG, 1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    Viewport shadow_viewport;
                    shadow_viewport.Width = shadow_viewport.Height = float(shadow_resolution);
                    context->SetViewports(1, &shadow_viewport, shadow_resolution, shadow_resolution);
                    draw_objects(light_view, light_projection, nullptr, true, false, false, &camera);
                }
                shadow_enabled = true;
        }
    }
    context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);
    if (!travelling)
        draw_objects(view, projection, nullptr);
    if (!travelling && outline_info && frame_settings.post.outline.enabled) {
        auto* outline_rtv = outline_info->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* object_id_rtv = outline_object_id->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        ITextureView* targets[]{outline_rtv, object_id_rtv};
        context->SetRenderTargets(2, targets, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear_outline[4]{0, 0, 0, 0};
        context->ClearRenderTarget(outline_rtv, clear_outline, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(object_id_rtv, clear_outline, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetViewports(1, &viewport, outline_info->GetDesc().Width, outline_info->GetDesc().Height);
        draw_objects(view, projection, nullptr, false, true);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetViewports(1, &viewport, scene_color->GetDesc().Width, scene_color->GetDesc().Height);
    }
#ifdef DAE_CHARACTER_SCENE
    // A second local player's view: the same objects from its own client's world and poses, through its own camera.
    shell.second_view = character && character->players().size() > 1 && character->client(character->players()[1]) &&
                        shell.workspace == darkangel::editor_app::Workspace::Game;
    if (shell.second_view && scene_color_second) {
        const auto* second = character->client(character->players()[1]);
        const auto& eye = play.second_camera(elapsed_seconds);
        auto* rtv_second = scene_color_second->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv_second = scene_depth_second->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv_second, dsv_second, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv_second, sky, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv_second, CLEAR_DEPTH_FLAG, 1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport_second;
        viewport_second.Width = static_cast<float>(scene_color_second->GetDesc().Width);
        viewport_second.Height = static_cast<float>(scene_color_second->GetDesc().Height);
        context->SetViewports(1, &viewport_second, scene_color_second->GetDesc().Width, scene_color_second->GetDesc().Height);
        const auto view_second =
            XMMatrixLookAtRH(XMVectorSet(float(eye.position[0]), float(eye.position[1]), float(eye.position[2]), 1),
                             XMVectorSet(float(eye.target[0]), float(eye.target[1]), float(eye.target[2]), 1), XMVectorSet(0, 1, 0, 0));
        const auto projection_second = XMMatrixPerspectiveFovRH(
            float(eye.fov_degrees) * XM_PI / 180.f, viewport_second.Width / viewport_second.Height, gpu.radius * .01f, gpu.radius * 40);
        XMStoreFloat4x4(&shell.game_view_second, view_second);
        XMStoreFloat4x4(&shell.game_projection_second, projection_second);
        if (!travelling)
            draw_objects(view_second, projection_second, second);
        if (!travelling && outline_info_second && frame_settings.post.outline.enabled) {
            auto* outline_rtv_second = outline_info_second->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            auto* object_id_rtv_second = outline_object_id_second->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            ITextureView* targets[]{outline_rtv_second, object_id_rtv_second};
            context->SetRenderTargets(2, targets, dsv_second, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear_outline[4]{0, 0, 0, 0};
            context->ClearRenderTarget(outline_rtv_second, clear_outline, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearRenderTarget(object_id_rtv_second, clear_outline, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->SetViewports(1, &viewport_second, outline_info_second->GetDesc().Width, outline_info_second->GetDesc().Height);
            draw_objects(view_second, projection_second, second, false, true);
        }
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetViewports(1, &viewport, scene_color->GetDesc().Width, scene_color->GetDesc().Height);
    }
#endif
#ifdef DAE_CHARACTER_SCENE
    // Projectiles in flight, from the client's replicated list, pointing along their velocity.
    if (character && shell.workspace == darkangel::editor_app::Workspace::Game) {
        projectiles_seen = std::max(projectiles_seen, character->projectiles().size());
        for (const auto& shot : character->projectiles()) {
            const double speed =
                std::sqrt(shot.velocity.x * shot.velocity.x + shot.velocity.y * shot.velocity.y + shot.velocity.z * shot.velocity.z);
            if (speed < 1e-6)
                continue;
            auto rotation = XMMatrixRotationRollPitchYaw(float(-std::asin(std::clamp(shot.velocity.y / speed, -1., 1.))),
                                                         float(std::atan2(shot.velocity.x, shot.velocity.z)), 0);
            auto world = rotation * XMMatrixTranslation(float(shot.position.x), float(shot.position.y), float(shot.position.z));
            const UploadedModel* projectile_model=&projectile_gpu;
            if(shot.visual_model!=AssetId{})if(auto found=scene_models.find(shot.visual_model);found!=scene_models.end())projectile_model=&found->second;
            for (const auto& mesh : projectile_model->meshes) {
                IBuffer* vb = mesh.vertices;
                Uint64 offset{};
                context->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
                context->SetIndexBuffer(mesh.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for (const auto& part : mesh.parts) {
                    const auto& material = projectile_model->materials.at(part.material);
                    Constants constants;
                    XMStoreFloat4x4(&constants.mvp, world * view * projection);
                    XMStoreFloat4x4(&constants.rotation, rotation);
                    XMStoreFloat4x4(&constants.world, world);
                    constants.color = {material.value.color[0], material.value.color[1], material.value.color[2], material.value.color[3]};
                    apply_material(constants, material.value);
                    void* mapped{};
                    context->MapBuffer(constant_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                    require(mapped, "Uniform buffer mapping failed");
                    std::memcpy(mapped, &constants, sizeof(constants));
                    context->UnmapBuffer(constant_buffer, MAP_WRITE);
                    context->SetPipelineState(pso.RawPtr());
                    if(auto* shadow=material.binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap"))shadow->Set(shadow_srv);
                    bind_gpu_clusters(material.binding, gpu_clusters);
                    context->CommitShaderResources(material.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    DrawIndexedAttribs draw;
                    draw.NumIndices = part.count;
                    draw.IndexType = VT_UINT32;
                    draw.FirstIndexLocation = part.first;
                    draw.Flags = DRAW_FLAG_VERIFY_ALL;
                    context->DrawIndexed(draw);
                }
            }
        }
        // A prepared projectile is a presentation-only object evaluated from the owner's authored action clock.
        // Its VFX blocks define the ground-emergence and socket-travel phases; release hands ownership to the
        // authoritative replicated projectile above. Interrupting the action removes this derived object immediately.
        if (const auto* owner = character->ability(); owner && owner->active && owner->action && character_player) {
            const auto* client = character->client(character_player);
            const auto* combat = client ? client->combat() : nullptr;
            const auto definition = combat ? std::find_if(combat->abilities.begin(), combat->abilities.end(), [&](const auto& value) { return value->id == owner->ability; }) : std::vector<std::shared_ptr<const AbilityDefinition>>::const_iterator{};
            if (combat && definition != combat->abilities.end()) {
                const ActionDefinition* action = nullptr;
                if ((*definition)->action->id == owner->action_definition) action = (*definition)->action.get();
                for (const auto& step : (*definition)->combo_steps) if (step.action->id == owner->action_definition) action = step.action.get();
                std::optional<AbilityProjectile> prepared_shot;
                if(action)for(const auto& block:action->blocks)if(block.projectile&&block.projectile->preparation){const auto& shot=*block.projectile;prepared_shot=AbilityProjectile{shot.speed,shot.gravity,shot.lifetime,shot.visual_model,shot.preparation,true};break;}
                if(!prepared_shot&&(*definition)->aim&&(*definition)->aim->projectile&&!(*definition)->aim->projectile->composer_owned)prepared_shot=(*definition)->aim->projectile;
                if(prepared_shot&&prepared_shot->preparation){
                const auto& projectile=*prepared_shot;const auto& preparation=*projectile.preparation;
                const auto find_block = [&](unsigned id) -> const ActionBlock* { if (!action) return nullptr; const auto found = std::find_if(action->blocks.begin(), action->blocks.end(), [&](const auto& block) { return block.id == id; }); return found == action->blocks.end() ? nullptr : &*found; };
                const auto* emergence = find_block(preparation.emergence_block);
                const auto* movement = find_block(preparation.move_block);
                const auto* release = find_block(preparation.release_block);
                const auto model = scene_models.find(projectile.visual_model);
                const auto root_handle = character->presentation().find(character_player);
                if (emergence && movement && release && model != scene_models.end() && character->presentation().valid(root_handle) &&
                    owner->action->clock >= emergence->begin && owner->action->clock < release->begin) {
                    const auto& root = character->presentation().read(root_handle).transform;
                    const auto yaw = root.yaw, cosine = std::cos(yaw), sine = std::sin(yaw);
                    const std::array<double,3> offset{preparation.offset[0],preparation.offset[1],preparation.offset[2]};
                    const XMFLOAT3 ground{float(root.x + cosine*offset[0] + sine*offset[2]),float(root.y + offset[1]),float(root.z - sine*offset[0] + cosine*offset[2])};
                    XMFLOAT3 hand{ground.x,ground.y+float(preparation.rise),ground.z};
                    const auto& rig = character_resources->rig.definition;
                    auto socket_name = preparation.socket;
                    if (const auto socket = rig.sockets.find(socket_name); socket != rig.sockets.end()) socket_name = socket->second;
                    const auto joint = std::find_if(rig.joints.begin(),rig.joints.end(),[&](const auto& candidate){return candidate.key==socket_name;});
                    const auto* pose = character->pose(character_player);
                    if (joint != rig.joints.end() && pose && std::size_t(joint-rig.joints.begin()) < pose->size()) {
                        XMFLOAT4X4 joint_matrix{};std::memcpy(&joint_matrix,(*pose)[std::size_t(joint-rig.joints.begin())].values.data(),sizeof(joint_matrix));
                        const auto joint_world=XMLoadFloat4x4(&joint_matrix)*XMMatrixScaling(float(root.scale),float(root.scale),float(root.scale))*XMMatrixRotationRollPitchYaw(float(root.pitch),float(root.yaw),float(root.roll))*XMMatrixTranslation(float(root.x),float(root.y),float(root.z));
                        XMStoreFloat3(&hand,XMVector3TransformCoord(XMVectorSet(0,0,0,1),joint_world));
                    }
                    const auto prepared_pose=prepared_projectile_pose(preparation,*action,double(owner->action->clock),
                        {ground.x,ground.y,ground.z},{hand.x,hand.y,hand.z});
                    if(prepared_pose){
                    const XMFLOAT3 position{float(prepared_pose->position[0]),float(prepared_pose->position[1]),float(prepared_pose->position[2])};
                    const auto pitch=-XM_PIDIV2+XMConvertToRadians(float(preparation.ready_pitch_degrees+90.)*float(prepared_pose->travel));const auto rotation=XMMatrixRotationRollPitchYaw(pitch,float(yaw),0);const auto world=rotation*XMMatrixTranslation(position.x,position.y,position.z);const auto& prepared=model->second;
                    for(const auto& mesh:prepared.meshes){IBuffer* vb=mesh.vertices;Uint64 vertex_offset{};context->SetVertexBuffers(0,1,&vb,&vertex_offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);context->SetIndexBuffer(mesh.indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);for(const auto& part:mesh.parts){const auto& material=prepared.materials.at(part.material);Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);XMStoreFloat4x4(&constants.world,world);constants.color={material.value.color[0],material.value.color[1],material.value.color[2],material.value.color[3]};apply_material(constants,material.value);void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Uniform buffer mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);context->SetPipelineState(pso.RawPtr());if(auto* shadow=material.binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap"))shadow->Set(shadow_srv);bind_gpu_clusters(material.binding,gpu_clusters);context->CommitShaderResources(material.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);DrawIndexedAttribs draw;draw.NumIndices=part.count;draw.IndexType=VT_UINT32;draw.FirstIndexLocation=part.first;draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(draw);}}
                    }
                }
                }
            }
        }
    }
#endif
    if (frame_settings.sky.hdr_texture != sky_asset) {
        sky_texture.Release();
        sky_texture_cube = false;
        const auto requested_sky = frame_settings.sky.hdr_texture;
        if (requested_sky != AssetId{}) try {
            const auto registry = context_.editor.assets ? context_.editor.package_model(requested_sky) : context_.options.registry;
            const auto& cas = context_.editor.assets ? context_.editor.assets->cas_path() : context_.options.cas;
            auto cooked = load_cooked_texture(registry, cas, requested_sky);
            sky_texture_cube = cooked.layers == 6;
            sky_texture = texture(device.RawPtr(), cooked);
            sky_asset = requested_sky;
        } catch (const std::exception& error) {
            context_.editor.log(std::string("HDR sky load failed: ") + error.what(), ConsoleSeverity::Error);
        } else if (requested_sky == AssetId{}) sky_asset = {};
    }
    section_start = std::chrono::steady_clock::now();
    auto present_post = [&](ITexture* source, ITexture* destination, ITexture* info, ITexture* object_id, ITexture* depth, IShaderResourceBinding* binding, FXMMATRIX camera_view, FXMMATRIX camera_projection, unsigned cache_index) {
        PostConstants constants;
        constants.exposure = {frame_settings.post.exposure, frame_settings.post.exposure_compensation, frame_settings.post.contrast, frame_settings.post.saturation};
        constants.bloom = {frame_settings.post.bloom_threshold, frame_settings.post.bloom_soft_threshold, frame_settings.post.bloom ? frame_settings.post.bloom_intensity : 0.f, frame_settings.post.bloom_radius};
        constants.tint_fxaa = {frame_settings.post.tint.r, frame_settings.post.tint.g, frame_settings.post.tint.b, frame_settings.post.fxaa ? 1.f : 0.f};
        constants.grade_lift = {frame_settings.post.grade_lift.r, frame_settings.post.grade_lift.g, frame_settings.post.grade_lift.b, 0};
        constants.grade_gamma = {frame_settings.post.grade_gamma.r, frame_settings.post.grade_gamma.g, frame_settings.post.grade_gamma.b, 0};
        constants.grade_gain = {frame_settings.post.grade_gain.r, frame_settings.post.grade_gain.g, frame_settings.post.grade_gain.b, 0};
        constants.grade_params = {frame_settings.post.grade_temperature, 0, 0, 0};
        constants.texel = {1.f / std::max(1u, destination->GetDesc().Width), 1.f / std::max(1u, destination->GetDesc().Height), 0, 0};
        constants.outline = {frame_settings.post.outline.color.r, frame_settings.post.outline.color.g, frame_settings.post.outline.color.b, frame_settings.post.outline.enabled ? 1.f : 0.f};
        constants.outline_params = {frame_settings.post.outline.thickness, frame_settings.post.outline.depth_sensitivity, frame_settings.post.outline.normal_sensitivity, frame_settings.post.outline.group_sensitivity};
        constants.outline_fade = {frame_settings.post.outline.fade_start, frame_settings.post.outline.fade_end, device->GetDeviceInfo().NDC.YtoVScale, 0};
        XMStoreFloat4x4(&constants.inverse_projection, XMMatrixInverse(nullptr, camera_projection));
        XMStoreFloat4x4(&constants.inverse_view, XMMatrixInverse(nullptr, camera_view));
        constants.sky_zenith = {frame_settings.sky.zenith.r, frame_settings.sky.zenith.g, frame_settings.sky.zenith.b, 0};
        constants.sky_horizon = {frame_settings.sky.horizon.r, frame_settings.sky.horizon.g, frame_settings.sky.horizon.b, 0};
        constants.sky_ground = {frame_settings.sky.ground.r, frame_settings.sky.ground.g, frame_settings.sky.ground.b, 0};
        constants.sky_sun = {frame_settings.sky.sun_direction[0], frame_settings.sky.sun_direction[1], frame_settings.sky.sun_direction[2], frame_settings.sky.procedural && frame_settings.sky_enabled ? frame_settings.sky.sun_size : 0.f};
        constants.sky_sun_color = {frame_settings.sky.sun_color.r, frame_settings.sky.sun_color.g, frame_settings.sky.sun_color.b, frame_settings.sky.procedural && frame_settings.sky_enabled ? frame_settings.sky.sun_intensity : 0.f};
        constants.sky_mode = {frame_settings.sky_enabled && sky_texture ? 1.f : 0.f, sky_texture_cube ? 1.f : 0.f, frame_settings.sky.hdr_intensity, frame_settings.sky.hdr_rotation_degrees * DirectX::XM_PI / 180.f};
        constants.sky_cloud = {frame_settings.sky.procedural && frame_settings.sky_enabled ? frame_settings.sky.cloud_amount : 0.f,
                               static_cast<float>(frame_settings.sky.seed), 0.f, 0.f};
        void* mapped{};
        context->MapBuffer(post_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
        require(mapped, "Post-process constants mapping failed");
        std::memcpy(mapped, &constants, sizeof(constants));
        context->UnmapBuffer(post_buffer, MAP_WRITE);
        auto* source_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SceneColor");
        require(source_variable, "Post-process source contract changed");
        source_variable->Set(source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        auto* info_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "OutlineInfo");
        auto* object_id_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "OutlineObjectId");
        auto* depth_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SceneDepth");
        require(info_variable && object_id_variable && depth_variable, "Post-process outline contract changed");
        info_variable->Set(info->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        object_id_variable->Set(object_id->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        depth_variable->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        auto* sky_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SkyTexture");
        auto* sky_cube_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SkyCube");
        require(sky_variable && sky_cube_variable, "Post-process sky texture contract changed");
        sky_variable->Set(sky_texture && !sky_texture_cube ? sky_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : sky_fallback->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        sky_cube_variable->Set(sky_texture && sky_texture_cube ? sky_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : sky_fallback_cube->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        if (!args.render_reference && frame_settings.sky_enabled && frame_settings.sky.procedural && !sky_texture) {
            auto& cached = sky_cache[cache_index];
            const auto& desc = destination->GetDesc();
            if (!cached.texture || cached.texture->GetDesc().Width != 1024 || cached.texture->GetDesc().Height != 512) {
                // Size changes follow the viewport's existing resize discipline;
                // a cache hit or settings change never waits for GPU idle.
                if (cached.texture) { context->Flush(); context->WaitForIdle(); }
                TextureDesc target_desc;
                target_desc.Name = "Cached HDR procedural sky and clouds";
                // Cache a camera-independent equirectangular environment, not
                // a screen-space image. Every view samples it by world ray.
                target_desc.Type = RESOURCE_DIM_TEX_2D; target_desc.Width = 1024; target_desc.Height = 512;
                target_desc.Format = TEX_FORMAT_RGBA16_FLOAT;
                target_desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
                cached.texture.Release();
                device->CreateTexture(target_desc, nullptr, &cached.texture);
                require(cached.texture, "Procedural sky cache allocation failed");
                cached.valid = false;
            }
            const std::array<XMFLOAT4, 7> key{constants.sky_zenith, constants.sky_horizon, constants.sky_ground,
                constants.sky_sun, constants.sky_sun_color, constants.sky_mode, constants.sky_cloud};
            if (!cached.valid || std::memcmp(key.data(), cached.settings.data(), sizeof(key)) != 0) {
                if (!sky_cache_pso) sky_cache_pso = post_pipeline(device, post_buffer, TEX_FORMAT_RGBA16_FLOAT);
                auto sky_constants = constants;
                sky_constants.sky_mode.x = -1; // HDR sky-only evaluation, before exposure, bloom and grading
                context->MapBuffer(post_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
                require(mapped, "Sky cache constants mapping failed");
                std::memcpy(mapped, &sky_constants, sizeof(sky_constants)); context->UnmapBuffer(post_buffer, MAP_WRITE);
                auto* sky_target = cached.texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                context->SetRenderTargets(1, &sky_target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                Viewport sky_view; sky_view.Width = float(desc.Width); sky_view.Height = float(desc.Height);
                context->SetViewports(1, &sky_view, desc.Width, desc.Height);
                context->SetPipelineState(sky_cache_pso);
                context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                DrawAttribs sky_draw; sky_draw.NumVertices = 3; sky_draw.Flags = DRAW_FLAG_VERIFY_ALL;
                context->Draw(sky_draw);
                cached.settings = key; cached.valid = true;
                ++shell.render_optimizations[0];
            } else ++shell.render_optimizations[1];
            sky_variable->Set(cached.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            // Cached sky is an equirectangular world-space environment.
            constants.sky_mode = {2, 0, 1, 0};
            context->MapBuffer(post_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
            require(mapped, "Cached-sky post constants mapping failed");
            std::memcpy(mapped, &constants, sizeof(constants)); context->UnmapBuffer(post_buffer, MAP_WRITE);
        }
        auto* target = destination->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport post_view;
        post_view.Width = static_cast<float>(destination->GetDesc().Width);
        post_view.Height = static_cast<float>(destination->GetDesc().Height);
        context->SetViewports(1, &post_view, destination->GetDesc().Width, destination->GetDesc().Height);
        context->SetPipelineState(post_pso);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
    };
    present_post(scene_color, scene_display, outline_info, outline_object_id, scene_depth, post_binding, view, projection, 0);
    if (shell.second_view && scene_color_second && scene_display_second)
        present_post(scene_color_second, scene_display_second, outline_info_second, outline_object_id_second, scene_depth_second, post_binding_second,
                     XMLoadFloat4x4(&shell.game_view_second), XMLoadFloat4x4(&shell.game_projection_second), 1);
    section(4);
}
VfxView SceneViewport::vfx_view() const {
    using namespace DirectX;
    const auto view = XMLoadFloat4x4(&view_matrix), projection = XMLoadFloat4x4(&projection_matrix);
    const auto& gpu = context_.scene.gpu;
    darkangel::VfxView shown;
    XMFLOAT4X4 stored;
    XMStoreFloat4x4(&stored, view);
    std::memcpy(shown.view.data(), &stored, sizeof(stored));
    XMStoreFloat4x4(&stored, projection);
    std::memcpy(shown.projection.data(), &stored, sizeof(stored));
    XMStoreFloat4x4(&stored, XMMatrixInverse(nullptr, view));
    shown.eye = {stored._41, stored._42, stored._43};
    shown.forward = {-stored._31, -stored._32, -stored._33};
    shown.near_plane = gpu.radius * .01f;
    shown.far_plane = gpu.radius * 40;
    return shown;
}
} // namespace darkangel::editor_app::render
