#include "scene_viewport.hpp"
#include <darkangel/scene_client.hpp>
#include <darkangel/character_scene.hpp>
#include "render_system.hpp"
#include "scene_resources.hpp"
#include "render_settings.hpp"
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
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace darkangel::editor_app::render {
using namespace Diligent;
SceneViewport::SceneViewport(SceneViewportContext context) : context_(context) {}
void SceneViewport::prepare_targets() {
    auto& device = context_.render.device;
    auto& context = context_.render.context;
    auto& swap_desc = context_.render.swap_desc;
    auto& shell = context_.shell;
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
        color.Name = "Editor scene display";
        color.Format = swap_desc.ColorBufferFormat;
        device->CreateTexture(color, nullptr, &scene_display);
        color.Name = "Editor scene depth";
        color.Format = swap_desc.DepthBufferFormat;
        color.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(color, nullptr, &scene_depth);
        require(scene_color && scene_display && scene_depth, "Viewport resource creation failed");
    }
    // The second local player's view has its own target, sized to its viewport of the previous frame.
    if (shell.second_view) {
        const unsigned wide = std::max(1u, static_cast<unsigned>(shell.area_second.width)),
                       high = std::max(1u, static_cast<unsigned>(shell.area_second.height));
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
            color.Name = "Editor second viewport display";
            color.Format = swap_desc.ColorBufferFormat;
            device->CreateTexture(color, nullptr, &scene_display_second);
            color.Name = "Editor second player depth";
            color.Format = swap_desc.DepthBufferFormat;
            color.BindFlags = BIND_DEPTH_STENCIL;
            device->CreateTexture(color, nullptr, &scene_depth_second);
            require(scene_color_second && scene_display_second && scene_depth_second, "Second viewport resource creation failed");
        }
    }
    if (!shadow_map) {
        TextureDesc shadow;
        shadow.Name = "DarkAngel cascaded main-light shadows";
        shadow.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        shadow.Width = shadow.Height = 2048;
        shadow.ArraySize = 4;
        shadow.Format = TEX_FORMAT_D32_FLOAT;
        shadow.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        device->CreateTexture(shadow, nullptr, &shadow_map);
        require(shadow_map, "Shadow map creation failed");
        shadow_srv = shadow_map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        for (unsigned cascade = 0; cascade < 4; ++cascade) {
            TextureViewDesc view;
            view.ViewType = TEXTURE_VIEW_DEPTH_STENCIL;
            view.Format = TEX_FORMAT_D32_FLOAT;
            view.FirstArraySlice = cascade;
            view.NumArraySlices = 1;
            shadow_map->CreateView(view, &shadow_dsv[cascade]);
            require(shadow_dsv[cascade], "Shadow cascade view creation failed");
        }
    }
    if (!post_buffer) { BufferDesc desc;desc.Name="Editor post-process constants";desc.Usage=USAGE_DYNAMIC;desc.BindFlags=BIND_UNIFORM_BUFFER;desc.CPUAccessFlags=CPU_ACCESS_WRITE;desc.Size=sizeof(PostConstants);device->CreateBuffer(desc,nullptr,&post_buffer);require(post_buffer,"Post-process constants buffer creation failed"); }
    if (!post_pso) { post_pso=post_pipeline(device.RawPtr(),post_buffer.RawPtr(),swap_desc.ColorBufferFormat);post_pso->CreateShaderResourceBinding(&post_binding,true);post_pso->CreateShaderResourceBinding(&post_binding_second,true);require(post_binding&&post_binding_second,"Post-process binding creation failed"); }
    shell.game_image_second = shell.second_view && scene_display_second
                                  ? reinterpret_cast<ImTextureID>(scene_display_second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
                                  : ImTextureID{};
}
ImTextureID SceneViewport::scene_texture() const {
    return reinterpret_cast<ImTextureID>(scene_display->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}
void SceneViewport::resize(const ViewArea& area) {
    scene_width = std::max(1u, static_cast<unsigned>(area.width));
    scene_height = std::max(1u, static_cast<unsigned>(area.height));
}
void SceneViewport::render(double elapsed_seconds) {
    context_.shell.scene_visible = context_.shell.scene_culled = context_.shell.scene_draws = 0;
    auto& device = context_.render.device;
    auto& context = context_.render.context;
    auto& constant_buffer = context_.render.constant_buffer;
    auto& pso = context_.render.pso;
    auto& pso_cull = context_.render.pso_cull;
    auto& scene = context_.scene;
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
    const auto& composer_matrices = previews.composer_matrices;
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
    if (const auto* play_camera = play.game_camera(elapsed_seconds)) {
        view = XMMatrixLookAtRH(
            XMVectorSet(float(play_camera->position[0]), float(play_camera->position[1]), float(play_camera->position[2]), 1),
            XMVectorSet(float(play_camera->target[0]), float(play_camera->target[1]), float(play_camera->target[2]), 1),
            XMVectorSet(0, 1, 0, 0));
        camera_fov = float(play_camera->fov_degrees) * XM_PI / 180.f;
    } else if (shell.workspace == darkangel::editor_app::Workspace::Player && shell.camera_preview_visible && shell.camera_preview_rig &&
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
    const bool character_render = shell.workspace == darkangel::editor_app::Workspace::Character && authoring_skin &&
                                  shell.character_preview_asset == shell.character_selection;
    const bool graph_render = shell.workspace == darkangel::editor_app::Workspace::Animation && authoring_skin && graph_pose &&
                              shell.graph_preview_asset == shell.authored_selection;
    const bool composer_render = shell.composer_visible && (composer_pose || graph_pose) && !editor.preview.active() &&
                                 (shell.workspace == darkangel::editor_app::Workspace::Ability ||
                                  shell.workspace == darkangel::editor_app::Workspace::Animation || character_render);
    if (composer_render) {
        float horizontal = shell.composer_distance * std::cos(shell.composer_pitch);
        view = XMMatrixLookAtRH(XMVectorSet(horizontal * std::sin(shell.composer_yaw),
                                            1 + shell.composer_distance * std::sin(shell.composer_pitch),
                                            horizontal * std::cos(shell.composer_yaw), 1),
                                XMVectorSet(0, 1.0f, 0, 1), XMVectorSet(0, 1, 0, 0));
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
    auto projection = XMMatrixPerspectiveFovRH(camera_fov, viewport.Width / viewport.Height, gpu.radius * .01f, gpu.radius * 40);
    if (shell.workspace==darkangel::editor_app::Workspace::Scene&&!editor.preview.active()) {
        // Use the exact matrices used by this frame's UI picking/drop rays, also during viewport resize.
        view=XMLoadFloat4x4(&shell.scene_view);
        projection=XMLoadFloat4x4(&shell.scene_projection);
    }
    XMStoreFloat4x4(&view_matrix, view);
    XMStoreFloat4x4(&projection_matrix, projection);
    shell.scene_view = view_matrix;
    shell.scene_projection = projection_matrix;
    shell.scene_camera = true;
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
    render::SceneRenderingSettings frame_settings;
    std::vector<FrameLight> frame_lights;
    for(const auto& origin:editor.document->plan().origins){auto handle=editor.displayed().find(origin.object);if(!editor.displayed().valid(handle))continue;auto object=editor.displayed().read(handle);if(object.optional_json.empty())continue;try{auto source=nlohmann::json::parse(object.optional_json);if(source.contains("rendering"))frame_settings=render::decode_rendering_settings(source.at("rendering"));if(source.contains("light")){auto authored=render::decode_light(source.at("light"));FrameLight made;made.light.id=object.id;made.light.type=authored.type;made.light.enabled=authored.enabled;made.light.cast_shadows=authored.cast_shadows;made.light.color=authored.color;made.light.intensity=authored.intensity;made.light.range=authored.range;made.light.inner_cone_degrees=authored.inner_cone_degrees;made.light.outer_cone_degrees=authored.outer_cone_degrees;made.light.falloff=authored.falloff;made.world_position={float(object.transform.x),float(object.transform.y),float(object.transform.z)};const auto rotation=XMMatrixRotationRollPitchYaw(float(object.transform.pitch),float(object.transform.yaw),float(object.transform.roll));const auto direction=XMVector3TransformNormal(XMVectorSet(0,-1,0,0),rotation);XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(made.light.direction_view.data()),XMVector3Normalize(direction));frame_lights.push_back(made);}}catch(const std::exception&) {}}
    const float sky[4]={frame_settings.sky_enabled?frame_settings.sky_color.r:background[0],frame_settings.sky_enabled?frame_settings.sky_color.g:background[1],frame_settings.sky_enabled?frame_settings.sky_color.b:background[2],1};context->ClearRenderTarget(rtv,sky,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    std::array<XMFLOAT4X4,4> shadow_matrices{};std::array<float,4> shadow_splits{.2f,.45f,.7f,1.f};bool shadow_enabled=false;
    // One pass over the scene's objects for a view. viewer: the client whose world and poses are shown; null for the editor's own.
    auto draw_objects = [&](const XMMATRIX& view, const XMMATRIX& projection, const SceneViewer* viewer, bool shadow_pass = false) {
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
        const auto visible = [&](FXMVECTOR center, float radius) {
            for (const auto& plane : planes)
                if (XMVectorGetX(XMPlaneDotCoord(plane, center)) < -radius)
                    return false;
            return true;
        };
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
        // Pipeline, material binding and buffers carry over between consecutive draws that share them.
        IPipelineState* bound_pipeline = nullptr;
        IShaderResourceBinding* bound_material = nullptr;
        IBuffer* bound_vertices = nullptr;
        for (const auto& [object_id, model_id] : drawn_objects) {
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
            if (!object_world->valid(entity) && scattered == scatter_poses.end())
                continue;
#ifdef DAE_CHARACTER_SCENE
            if (composer_render &&
                (model_id != character_resources->skin || (character_resources->player && object_id != character_resources->player)))
                continue;
#endif
            auto pose = scattered != scatter_poses.end() ? scattered->second : object_world->read(entity).transform;
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
            if (composer_render) {
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
            if (character_render || graph_render)
                selected_skin = &*authoring_skin;
#endif
            auto* drawn_ptr = selected_skin ? &selected_skin->gpu : nullptr;
            if (drawn_ptr && !args.skin_reference) {
                const std::vector<JointMatrix>* gameplay_pose = nullptr;
#ifdef DAE_CHARACTER_SCENE
                if (composer_render)
                    gameplay_pose = &composer_matrices;
                else if (character && selected_skin->model.rig.definition.id == character_resources->rig.definition.id &&
                         selected_skin->model.rig.definition.signature == character_resources->rig.definition.signature)
                    gameplay_pose = viewer ? viewer->pose(object_id) : character->pose(object_id);
#endif
                upload_palette(context, bone_buffer, *selected_skin, diagnostic_clip.get(), args.pose_tick, gameplay_pose);
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
                const float radius = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]) * float(pose.scale);
                if (!visible(XMVector3TransformCoord(XMVectorSet(drawn.center[0], drawn.center[1], drawn.center[2], 1), world), radius)) {
                    ++shell.scene_culled;
                    continue;
                }
            }
            ++shell.scene_visible;
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
                    const auto& material_plan=editor.preview.active()?editor.preview.active()->plan:editor.document->plan();
                    if(!prefab_render)if(const auto object=material_plan.materials.find(object_id);object!=material_plan.materials.end())if(const auto assignment=object->second.find(part.material);assignment!=object->second.end()){
                        const auto& native=scene.native_materials.at(assignment->second);
#ifdef DAE_ANIMATION
                        effective=drawn_ptr&&!args.skin_reference?&native.skinned:&native.surface;
#else
                        effective=&native.surface;
#endif
                    }
                    if(prefab_render)for(const auto& shown:shell.prefab_preview.models)if(shown.object==object_id)if(const auto assignment=shown.materials.find(part.material);assignment!=shown.materials.end())effective=&scene.native_materials.at(assignment->second).surface;
                    const auto& material=*effective;
                    if(shadow_pass){Constants constants;XMStoreFloat4x4(&constants.mvp,world*view*projection);XMStoreFloat4x4(&constants.rotation,rotation);XMStoreFloat4x4(&constants.world,world);void* mapped{};context->MapBuffer(constant_buffer,MAP_WRITE,MAP_FLAG_DISCARD,mapped);require(mapped,"Shadow constants mapping failed");std::memcpy(mapped,&constants,sizeof(constants));context->UnmapBuffer(constant_buffer,MAP_WRITE);
#ifdef DAE_ANIMATION
                        IPipelineState* shadow_pipeline=(drawn_ptr&&!args.skin_reference)?(material.value.double_sided?scene.skin_shadow_pso.RawPtr():scene.skin_shadow_cull.RawPtr()):(material.value.double_sided?context_.render.shadow_pso.RawPtr():context_.render.shadow_pso_cull.RawPtr());
#else
                        IPipelineState* shadow_pipeline=material.value.double_sided?context_.render.shadow_pso.RawPtr():context_.render.shadow_pso_cull.RawPtr();
#endif
                        if(shadow_pipeline!=bound_pipeline){context->SetPipelineState(shadow_pipeline);bound_pipeline=shadow_pipeline;}DrawIndexedAttribs shadow_draw;shadow_draw.NumIndices=part.count;shadow_draw.IndexType=VT_UINT32;shadow_draw.FirstIndexLocation=part.first;shadow_draw.Flags=DRAW_FLAG_VERIFY_ALL;context->DrawIndexed(shadow_draw);continue;}
                    Constants constants;
                    XMStoreFloat4x4(&constants.mvp, world * view * projection);
                    XMStoreFloat4x4(&constants.rotation, rotation);
                    XMStoreFloat4x4(&constants.world, world);
                    constants.color = {material.value.color[0] * mesh.color[0], material.value.color[1] * mesh.color[1],
                                       material.value.color[2] * mesh.color[2], material.value.color[3] * mesh.color[3]};
                    apply_material(constants, material.value);
                    if(frame_settings.main_light){for(const auto&prepared:frame_lights)if(prepared.light.id==frame_settings.main_light&&prepared.light.type==darkangel::rendering::LightType::Directional){constants.main_light={prepared.light.direction_view[0],prepared.light.direction_view[1],prepared.light.direction_view[2],1};constants.main_color={prepared.light.color.r,prepared.light.color.g,prepared.light.color.b,prepared.light.intensity};break;}}
                    const bool material_toon=material.value.family==material_family_toon_standard||material.value.family==material_family_toon_character||material.value.family==material_family_toon_terrain;
                    if(!material_toon){constants.ambient={frame_settings.toon.ambient_color.r*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.g*frame_settings.toon.ambient_strength,frame_settings.toon.ambient_color.b*frame_settings.toon.ambient_strength,1};constants.toon_bands={frame_settings.toon.band_thresholds[0],frame_settings.toon.band_thresholds[1],frame_settings.toon.band_thresholds[2],frame_settings.toon.transition_softness};constants.shadow_tint={frame_settings.toon.shadow_tint.r,frame_settings.toon.shadow_tint.g,frame_settings.toon.shadow_tint.b,1};}
                    constants.fog_color={frame_settings.fog.color.r,frame_settings.fog.color.g,frame_settings.fog.color.b,frame_settings.fog.enabled?1.f:0.f};constants.fog_params={frame_settings.fog.start,frame_settings.fog.end,frame_settings.fog.density,frame_settings.fog.exponential?1.f:0.f};
                    constants.shadow_splits={shadow_splits[0],shadow_splits[1],shadow_splits[2],shadow_splits[3]};constants.shadow_params={.001f,.002f,shadow_enabled?1.f:0.f,1.f/2048.f};for(unsigned cascade=0;cascade<4;++cascade)constants.shadow_matrices[cascade]=shadow_matrices[cascade];if(auto* shadow=material.binding->GetVariableByName(SHADER_TYPE_PIXEL,"ShadowMap"))shadow->Set(shadow_srv);
                    const float local_strength=material_toon?material.value.local_light_strength:frame_settings.toon.local_light_strength;unsigned local_index=0;const auto inverse_rotation=XMMatrixInverse(nullptr,rotation);for(const auto&prepared:frame_lights)if(prepared.light.id!=frame_settings.main_light&&prepared.light.type!=darkangel::rendering::LightType::Directional&&prepared.light.enabled&&local_index<4){const auto p=XMVector3TransformCoord(XMVectorSet(prepared.world_position[0]-float(pose.x),prepared.world_position[1]-float(pose.y),prepared.world_position[2]-float(pose.z),1),inverse_rotation);XMFLOAT3 local;XMStoreFloat3(&local,p);constants.local_positions[local_index]={local.x,local.y,local.z,prepared.light.range};constants.local_colors[local_index]={prepared.light.color.r,prepared.light.color.g,prepared.light.color.b,prepared.light.intensity*local_strength};++local_index;}
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
                                            upload(device, load_cooked_model(args.registry, args.cas, worn.model), pso, pso_cull));
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
    if(frame_settings.main_light){for(const auto& prepared:frame_lights)if(prepared.light.id==frame_settings.main_light&&prepared.light.type==darkangel::rendering::LightType::Directional&&prepared.light.enabled&&prepared.light.cast_shadows){const auto direction=XMVector3Normalize(XMVectorSet(prepared.light.direction_view[0],prepared.light.direction_view[1],prepared.light.direction_view[2],0));const auto target=XMVectorZero();const auto light_position=XMVectorSubtract(target,XMVectorScale(direction,200));const auto light_view=XMMatrixLookAtRH(light_position,target,XMVectorSet(0,1,0,0));for(unsigned cascade=0;cascade<4;++cascade){const float extent=std::max(8.f,gpu.radius*(.8f+float(cascade+1)*1.5f));const auto light_projection=XMMatrixOrthographicOffCenterRH(-extent,extent,-extent,extent,.1f,extent*4);XMStoreFloat4x4(&shadow_matrices[cascade],light_view*light_projection);auto* dsv_shadow=shadow_dsv[cascade].RawPtr();context->SetRenderTargets(0,nullptr,dsv_shadow,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->ClearDepthStencil(dsv_shadow,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);Viewport shadow_viewport;shadow_viewport.Width=shadow_viewport.Height=2048;context->SetViewports(1,&shadow_viewport,2048,2048);draw_objects(light_view,light_projection,nullptr,true);}shadow_enabled=true;}}
    context->SetRenderTargets(1,&rtv,dsv,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);context->SetViewports(1,&viewport,scene_color->GetDesc().Width,scene_color->GetDesc().Height);
    if (!travelling)
        draw_objects(view, projection, nullptr);
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
            for (const auto& mesh : projectile_gpu.meshes) {
                IBuffer* vb = mesh.vertices;
                Uint64 offset{};
                context->SetVertexBuffers(0, 1, &vb, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
                context->SetIndexBuffer(mesh.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                for (const auto& part : mesh.parts) {
                    const auto& material = projectile_gpu.materials.at(part.material);
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
    }
#endif
    auto present_post = [&](ITexture* source, ITexture* destination, IShaderResourceBinding* binding) {
        PostConstants constants;
        constants.exposure = {frame_settings.post.exposure, frame_settings.post.exposure_compensation, frame_settings.post.contrast, frame_settings.post.saturation};
        constants.bloom = {frame_settings.post.bloom_threshold, frame_settings.post.bloom_soft_threshold, frame_settings.post.bloom ? frame_settings.post.bloom_intensity : 0.f, frame_settings.post.bloom_radius};
        constants.tint_fxaa = {frame_settings.post.tint.r, frame_settings.post.tint.g, frame_settings.post.tint.b, frame_settings.post.fxaa ? 1.f : 0.f};
        constants.texel = {1.f / std::max(1u, destination->GetDesc().Width), 1.f / std::max(1u, destination->GetDesc().Height), 0, 0};
        void* mapped{};
        context->MapBuffer(post_buffer, MAP_WRITE, MAP_FLAG_DISCARD, mapped);
        require(mapped, "Post-process constants mapping failed");
        std::memcpy(mapped, &constants, sizeof(constants));
        context->UnmapBuffer(post_buffer, MAP_WRITE);
        auto* source_variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "SceneColor");
        require(source_variable, "Post-process source contract changed");
        source_variable->Set(source->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
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
    present_post(scene_color, scene_display, post_binding);
    if (shell.second_view && scene_color_second && scene_display_second)
        present_post(scene_color_second, scene_display_second, post_binding_second);
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
