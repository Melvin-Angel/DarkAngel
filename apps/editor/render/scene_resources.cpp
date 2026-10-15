#include "scene_resources.hpp"
#include "render_system.hpp"
#include "../editor_options.hpp"
#include <darkangel/check.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <fstream>

namespace darkangel::editor_app::render {
SceneResources::SceneResources(RenderSystem& render, const EditorOptions& options, AssetId model, const ModelGeneration& lease)
    : render_(render), args(options), id(model) {
    auto& device = render_.device;
    auto& swap_desc = render_.swap_desc;
    auto& constant_buffer = render_.constant_buffer;
    auto& pso = render_.pso;
    auto& pso_cull = render_.pso_cull;
    {
        RuntimeModel shape;
        shape.id = AssetId::random();
        CookedMaterial material;
        material.id = shape.id;
        material.color = {3.2f, 2.3f, .6f, 1.f};
        material.roughness = .4f;
        material.double_sided = true;
        shape.materials.push_back(material);
        CookedMesh mesh;
        const float half = .45f, girth = .085f;
        const std::array<std::array<float, 3>, 6> points{
            {{0, 0, half}, {0, 0, -half}, {girth, 0, 0}, {0, girth, 0}, {-girth, 0, 0}, {0, -girth, 0}}};
        for (const auto& point : points) {
            const float length = std::sqrt(point[0] * point[0] + point[1] * point[1] + point[2] * point[2]);
            mesh.vertices.push_back({point, {point[0] / length, point[1] / length, point[2] / length}, {0, 0}});
        }
        mesh.indices = {0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2, 1, 3, 2, 1, 4, 3, 1, 5, 4, 1, 2, 5};
        mesh.minimum = {-girth, -girth, -half};
        mesh.maximum = {girth, girth, half};
        mesh.parts.push_back({shape.id, 0, static_cast<std::uint32_t>(mesh.indices.size())});
        shape.meshes.push_back(std::move(mesh));
        projectile_gpu = upload(device, shape, pso, pso_cull);
    }
    gpu = upload(device, *lease.model, pso, pso_cull);
#ifdef DAE_ANIMATION
    BufferDesc cb = render_.constants_desc;
    cb.Name = "Canonical human skin palette";
    cb.Size = 256 * 16 * sizeof(float);
    device->CreateBuffer(cb, nullptr, &bone_buffer);
    require(bone_buffer, "Skin palette buffer creation failed");
    auto skin_source = skin_shader();
    skin_pso = pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, false, skin_source.c_str(),
                        bone_buffer);
    skin_cull =
        pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, true, skin_source.c_str(), bone_buffer);
    if (!args.pose_clip.empty()) {
        auto cooked = load_cooked_clip(args.registry, args.cas, AssetId::parse(args.pose_clip));
        diagnostic_clip = std::make_unique<AnimationClip>(std::move(cooked.definition), cooked.archive);
    }
#else
    (void)swap_desc;
    (void)constant_buffer;
#endif
}
bool SceneResources::is_skin(AssetId asset, const std::filesystem::path& package) {
    std::ifstream input(package.empty() ? args.registry : package);
    auto registry = nlohmann::json::parse(input);
    for (const auto& record : registry.at("assets"))
        if (record.at("id") == asset.text())
            return record.at("kind") == "human-binding";
    return false;
}
void SceneResources::prepare_model(AssetId requested, const std::filesystem::path& registry, const std::filesystem::path& cas) {
    const auto& source = registry.empty() ? args.registry : registry;
    const auto& store = cas.empty() ? args.cas : cas;
    auto& device = render_.device;
    auto& pso = render_.pso;
    auto& pso_cull = render_.pso_cull;
    if (requested == id || scene_models.contains(requested))
        return;
    // Distinct uploaded models; a scene places each as often as it likes.
    require(scene_models.size() < 4096, "Editor model resource limit (4096 distinct models)");
#ifdef DAE_ANIMATION
    if (scene_skins.contains(requested))
        return;
    if (is_skin(requested, source)) {
        require(scene_skins.size() < 4, "Editor skin resource limit");
        scene_skins.emplace(requested, upload_skin(device, load_cooked_skinned_model(source, store, requested),
                                                   args.skin_reference ? pso.RawPtr() : skin_pso.RawPtr(),
                                                   args.skin_reference ? pso_cull.RawPtr() : skin_cull.RawPtr(), diagnostic_clip.get(),
                                                   args.pose_tick, args.skin_reference));
        return;
    }
#endif
    auto cooked = load_cooked_model(source, store, requested);
    auto resources = upload(device, cooked, pso, pso_cull);
    scene_models.emplace(requested, std::move(resources));
}
void SceneResources::reload_shader(const std::string& text, const ModelGeneration& lease) {
    auto& device = render_.device;
    auto& context = render_.context;
    auto& swap_desc = render_.swap_desc;
    auto& constant_buffer = render_.constant_buffer;
    auto& pso = render_.pso;
    auto& pso_cull = render_.pso_cull;
    auto next = pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, false, text.c_str());
    auto next_cull = pipeline(device, constant_buffer, swap_desc.ColorBufferFormat, swap_desc.DepthBufferFormat, true, text.c_str());
    auto resources = upload(device, *lease.model, next, next_cull);
    std::map<AssetId, UploadedModel> other;
    for (const auto& [asset, unused] : scene_models)
        other.emplace(asset, upload(device, load_cooked_model(args.registry, args.cas, asset), next, next_cull));
    context->Flush();
    context->WaitForIdle();
    scene_models = std::move(other);
    gpu = std::move(resources);
    pso = std::move(next);
    pso_cull = std::move(next_cull);
}
SceneResources::Reload SceneResources::prepare_reload(const ModelStore& store) {
    auto& device = render_.device;
    auto& pso = render_.pso;
    auto& pso_cull = render_.pso_cull;
    Reload reload;
    auto& candidate = reload.candidate;
    auto& resources = reload.resources;
    auto& other = reload.other;
    candidate = store.prepare(args.registry, args.cas, id);
    resources = upload(device, *candidate.model, pso, pso_cull);
    for (const auto& [asset, unused] : scene_models)
        other.emplace(asset, upload(device, load_cooked_model(args.registry, args.cas, asset), pso, pso_cull));
#ifdef DAE_ANIMATION
    auto& skins = reload.skins;
    for (const auto& [asset, unused] : scene_skins)
        skins.emplace(asset, upload_skin(device, load_cooked_skinned_model(args.registry, args.cas, asset),
                                         args.skin_reference ? pso.RawPtr() : skin_pso.RawPtr(),
                                         args.skin_reference ? pso_cull.RawPtr() : skin_cull.RawPtr(), diagnostic_clip.get(),
                                         args.pose_tick, args.skin_reference));
#endif
    return reload;
}
void SceneResources::commit_models(Reload& reload) {
    auto& context = render_.context;
    context->Flush();
    context->WaitForIdle();
    scene_models = std::move(reload.other);
#ifdef DAE_ANIMATION
    scene_skins = std::move(reload.skins);
#endif
}
void SceneResources::commit_model(Reload& reload, ModelStore& store, ModelGeneration& lease) {
    store.publish(reload.candidate);
    gpu = std::move(reload.resources);
    lease = store.acquire();
    store.collect();
}
} // namespace darkangel::editor_app::render
