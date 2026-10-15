// A static model's own collision shape (SCENE_SCALE_PLAN.md phase 1). A real Royal hut is cooked in a scratch project: its
// "model-collision" product is the model's meshes welded in model space; cooking again gives identical bytes; transformed by a
// placement it matches what bake_model_collision makes for that placement; "collision": "none" cooks no shape; and an unknown
// setting is rejected at cook time.
#include <darkangel/assets.hpp>
#include <darkangel/hash.hpp>
#include <darkangel/model_collision.hpp>
#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace darkangel;
using Json = nlohmann::json;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(out), "Fixture write failed");
}
const char* const hut = "SM_RC_MudHut_Square_4F_Setback";
// The hut's glTF, buffer, texture and sidecar in a fresh project, the sidecar optionally changed.
std::filesystem::path project(const std::filesystem::path& workspace, const std::filesystem::path& repository, const Json& sidecar_patch) {
    const auto from = repository / "content/royal_district/static/mud_huts", to = workspace / "project/static";
    std::filesystem::create_directories(to);
    for (const auto* name : {".gltf", ".bin"})
        std::filesystem::copy_file(from / (std::string(hut) + name), to / (std::string(hut) + name));
    std::filesystem::copy_file(from / "mudhut-trim.png", to / "mudhut-trim.png");
    auto sidecar = Json::parse(read(from / (std::string(hut) + ".gltf.daimport")));
    sidecar.merge_patch(sidecar_patch);
    write(to / (std::string(hut) + ".gltf.daimport"), sidecar.dump(2));
    return workspace / "project";
}
struct Cooked {
    RuntimeModel model;
    std::shared_ptr<const CollisionMeshData> collision;
    std::string product;
    std::size_t registry_products{};
};
Cooked cook(const std::filesystem::path& workspace, const std::filesystem::path& repository, const Json& sidecar_patch = Json::object()) {
    const auto root = project(workspace, repository, sidecar_patch);
    AssetService service(root, workspace / "cache");
    service.scan();
    const auto relative = std::string("static/") + hut + ".gltf";
    const auto result = service.cook(relative);
    check(!service.cook(relative).changed, "A warm cook with the collision product converted again");
    service.package(result.root, workspace / "registry.json");
    Cooked out;
    out.model = load_cooked_model(workspace / "registry.json", service.cas_path(), result.root);
    out.collision = load_cooked_model_collision(workspace / "registry.json", service.cas_path(), result.root);
    const auto registry = Json::parse(read(workspace / "registry.json"));
    out.registry_products = registry.at("assets").size();
    for (const auto& record : registry.at("assets"))
        if (record.at("kind") == "model-collision")
            out.product = record.at("sha256").get<std::string>();
    return out;
}
} // namespace
int main() {
    try {
        const auto repository = std::filesystem::path(DAE_SOURCE_DIR);
        const auto scratch = std::filesystem::path(DAE_BINARY_DIR) / ("model-collision-" + AssetId::random().text());

        // Cooked with the model, in model space, its identity derived from the model's.
        const auto first = cook(scratch / "first", repository);
        check(first.collision != nullptr && first.model.collision == model_collision_id(first.model.id) &&
                  first.collision->runtime == first.model.collision,
              "A static model cooks its own collision product");
        // 2248 render vertices weld to 673 positions (the bound that used to reject this hut counted render vertices).
        check(first.collision->vertices.size() == 673 && first.collision->triangles.size() > 1000,
              "The hut's collision is its welded positions and kept triangles");

        // Deterministic: a second project and cache give the same product bytes.
        const auto second = cook(scratch / "second", repository);
        check(!first.product.empty() && first.product == second.product && first.collision->signature == second.collision->signature,
              "Model collision cooking is deterministic");

        // The local shape placed by a transform is what the per-placement bake gives for that placement.
        auto pose = [](double x, double y, double z, double yaw, double scale) {
            Transform t;
            t.x = x;
            t.y = y;
            t.z = z;
            t.yaw = yaw;
            t.scale = scale;
            return t;
        };
        for (const Transform placed : {Transform{}, pose(12.5, .5, -4, 1.2, 1), pose(-3, 2, 7, -2.4, 2.5)}) {
            const auto baked = bake_model_collision(first.model, placed, first.model.collision);
            check(baked.data->vertices.size() == first.collision->vertices.size() &&
                      baked.data->triangles.size() == first.collision->triangles.size() && baked.data->materials == first.collision->materials,
                  "Placed bake and model shape weld the same vertices and triangles");
            using namespace DirectX;
            const auto transform = XMMatrixScaling(float(placed.scale), float(placed.scale), float(placed.scale)) *
                                   XMMatrixRotationRollPitchYaw(float(placed.pitch), float(placed.yaw), float(placed.roll)) *
                                   XMMatrixTranslation(float(placed.x), float(placed.y), float(placed.z));
            for (std::size_t i = 0; i < baked.data->vertices.size(); ++i) {
                const auto local = first.collision->vertices[i];
                XMFLOAT3 moved;
                XMStoreFloat3(&moved, XMVector3TransformCoord(XMVectorSet(float(local.x), float(local.y), float(local.z), 1), transform));
                const auto expected = baked.data->vertices[i];
                check(std::abs(moved.x - expected.x) < 1e-4 && std::abs(moved.y - expected.y) < 1e-4 && std::abs(moved.z - expected.z) < 1e-4,
                      "Placed model shape vertex matches the placement bake");
            }
            for (std::size_t i = 0; i < baked.data->triangles.size(); ++i)
                check(baked.data->triangles[i] == first.collision->triangles[i], "Placed model shape triangles match the placement bake");
        }

        // The encoding round-trips.
        const auto decoded = decode_model_collision(encode_model_collision(*first.collision), first.collision->runtime);
        check(decoded->vertices == first.collision->vertices && decoded->triangles == first.collision->triangles &&
                  decoded->signature == first.collision->signature,
              "Model collision encoding round trip");

        // "none": no shape, one product fewer in the package.
        const auto none = cook(scratch / "none", repository, Json{{"collision", "none"}});
        check(none.collision == nullptr && none.model.collision == AssetId{} && none.product.empty() &&
                  none.registry_products + 1 == first.registry_products,
              "A model imported with collision none cooks no collision product");

        // An unknown setting fails the cook.
        bool rejected = false;
        try {
            cook(scratch / "invalid", repository, Json{{"collision", "box"}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "An unknown model collision setting is rejected");

        std::error_code ignored;
        std::filesystem::remove_all(scratch, ignored);
        std::cout << "Model collision tests passed: vertices=" << first.collision->vertices.size()
                  << " triangles=" << first.collision->triangles.size() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
