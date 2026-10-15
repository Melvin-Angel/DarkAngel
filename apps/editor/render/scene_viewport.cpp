#include "scene_viewport.hpp"
#include <darkangel/scene_client.hpp>
#include <darkangel/character_scene.hpp>
#include "render_system.hpp"
#include "scene_resources.hpp"
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
        color.Format = swap_desc.ColorBufferFormat;
        color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(color, nullptr, &scene_color);
        color.Name = "Editor scene depth";
        color.Format = swap_desc.DepthBufferFormat;
        color.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(color, nullptr, &scene_depth);
        require(scene_color && scene_depth, "Viewport resource creation failed");
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
            color.Format = swap_desc.ColorBufferFormat;
            color.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            device->CreateTexture(color, nullptr, &scene_color_second);
            color.Name = "Editor second player depth";
            color.Format = swap_desc.DepthBufferFormat;
            color.BindFlags = BIND_DEPTH_STENCIL;
            device->CreateTexture(color, nullptr, &scene_depth_second);
            require(scene_color_second && scene_depth_second, "Second viewport resource creation failed");
        }
    }
    shell.game_image_second = shell.second_view && scene_color_second
                                  ? reinterpret_cast<ImTextureID>(scene_color_second->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE))
                                  : ImTextureID{};
}
ImTextureID SceneViewport::scene_texture() const {
    return reinterpret_cast<ImTextureID>(scene_color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
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
#ifdef DAE_CHARACTER_SCENE
    using SceneViewer = darkangel::SceneClient;
#else
    struct SceneViewer;
#endif
    // One pass over the scene's objects for a view. viewer: the client whose world and poses are shown; null for the editor's own.
    auto draw_objects = [&](const XMMATRIX& view, const XMMATRIX& projection, const SceneViewer* viewer) {
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
                    const auto& material = drawn.materials.at(part.material);
                    Constants constants;
                    XMStoreFloat4x4(&constants.mvp, world * view * projection);
                    XMStoreFloat4x4(&constants.rotation, rotation);
                    constants.color = {material.value.color[0] * mesh.color[0], material.value.color[1] * mesh.color[1],
                                       material.value.color[2] * mesh.color[2], material.value.color[3] * mesh.color[3]};
                    constants.surface = {material.value.roughness, material.value.metallic, 0, 0};
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
                            constants.surface = {material.value.roughness, material.value.metallic, 0, 0};
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
    };
#ifdef DAE_CHARACTER_SCENE
    // While the party travels the editor's document and resources may already be the destination's; the loading panel covers the view.
    const bool travelling = travel && travel->busy();
#else
    const bool travelling = false;
#endif
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
        context->ClearRenderTarget(rtv_second, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
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
                    constants.color = {material.value.color[0], material.value.color[1], material.value.color[2], material.value.color[3]};
                    constants.surface = {material.value.roughness, material.value.metallic, 0, 0};
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
    }
#endif
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
