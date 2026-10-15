// The static collision layer (SCENE_SCALE_PLAN.md phase 2): stable identities, deterministic revisions, diff-based installation
// that leaves unchanged bodies alone, shared shapes, scaled bodies that keep their triangle keys and materials, explicit revision
// mismatches, and the static body budget.
#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/static_collision.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace darkangel;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> bool throws(F call, const char* containing = nullptr) {
    try {
        call();
    } catch (const std::exception& error) {
        return !containing || std::string(error.what()).find(containing) != std::string::npos;
    }
    return false;
}
// A 2 x 2 m floor tile centred on the origin, its two triangles keyed 0 and 1 with one material.
std::shared_ptr<const CollisionMeshData> tile(const char* material = "material.stone") {
    auto data = std::make_shared<CollisionMeshData>();
    data->runtime = AssetId::random();
    data->vertices = {{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}};
    data->triangles = {{{0, 2, 1}, 0, 0}, {{0, 3, 2}, 1, 0}};
    data->materials = {material};
    data->signature = collision_mesh_signature(*data);
    return data;
}
StaticCollisionBody body(std::uint64_t index, std::shared_ptr<const CollisionGeometry> geometry, MotorVec at, double scale = 1) {
    StaticCollisionBody out;
    out.object = StableId{7, index};
    out.id = static_body_id(out.object);
    out.geometry = std::move(geometry);
    out.position = at;
    out.scale = scale;
    return out;
}
CollisionHit down(const PhysicsWorld& world, double x, double z) {
    auto result = world.query_ray({x, 5, z}, {0, -10, 0});
    check(!result.overflow && result.hits.size() == 1, "Ray finds the static body below");
    return result.hits.front();
}
} // namespace
int main() {
    try {
        CollisionGeometryCache cache;
        const auto data = tile();
        const auto shape = cache.get(data);
        check(cache.get(data) == shape && cache.builds() == 1, "One shape per distinct mesh, shared by every placement");

        // Identity: bit 62 marks scene static bodies; the id is a function of the object alone.
        const auto id = static_body_id(StableId{7, 1});
        check(is_scene_static_body(id) && id == static_body_id(StableId{7, 1}) && id != static_body_id(StableId{7, 2}),
              "Static body identity derives from the scene object");

        // Revisions: insertion order and quaternion sign do not matter; transforms and scale do.
        auto a = body(1, shape, {0, 0, 0}), b = body(2, shape, {10, 0, 0}), c = body(3, shape, {20, 0, 0});
        b.rotation = placement_rotation(0, 1.25, 0);
        auto flipped = b;
        for (auto& n : flipped.rotation)
            n = -n;
        const StaticCollisionLayer ordered({a, b, c}), shuffled({c, a, flipped});
        check(ordered.revision() == shuffled.revision(), "Revision is independent of insertion order and quaternion sign");
        auto moved = c;
        moved.position.x += 0.001;
        auto scaled = c;
        scaled.scale = 2;
        check(StaticCollisionLayer({a, b, moved}).revision() != ordered.revision() &&
                  StaticCollisionLayer({a, b, scaled}).revision() != ordered.revision(),
              "Moving or scaling a body changes the revision");
        check(StaticCollisionLayer::empty()->revision().size() == 64 && StaticCollisionLayer::empty()->bodies().empty(), "Empty layer");
        check(throws([&] { StaticCollisionLayer({a, a}); }, "Duplicate"), "Duplicate identities are rejected");
        check(throws([&] {
                  auto zero = a;
                  zero.scale = 0;
                  StaticCollisionLayer({zero});
              }),
              "Zero scale is rejected");

        // Installation by diff.
        PhysicsWorld world;
        auto first = std::make_shared<const StaticCollisionLayer>(std::vector{a, b, c});
        world.set_static_layer(first);
        check(world.static_insertions() == 3, "A full install inserts every body once");
        const auto topology = world.topology();
        const auto before_a = down(world, 0, 0), before_c = down(world, 20, 0);
        check(before_a.identity == a.id && before_c.identity == c.id && world.material_key(before_a) == "material.stone",
              "Hits name the stable body id and the authored material");
        world.set_static_layer(std::make_shared<const StaticCollisionLayer>(std::vector{a, b, moved}));
        const auto after_a = down(world, 0, 0), after_c = down(world, 20.5, 0);
        check(world.static_insertions() == 3 && world.topology() == topology + 1, "Moving a body inserts nothing and is a topology change");
        check(after_a.generation == before_a.generation && after_c.generation != before_c.generation,
              "Unchanged bodies keep their generation; moved bodies get a new one");
        world.set_static_layer(world.static_layer());
        check(world.topology() == topology + 1, "Reinstalling the same layer changes nothing");
        world.set_static_layer(std::make_shared<const StaticCollisionLayer>(std::vector{a, moved}));
        check(world.query_ray({10, 5, 0}, {0, -10, 0}).hits.empty() && world.static_insertions() == 3, "Removed bodies leave the world");
        world.remove(a.id);
        check(world.query_ray({0, 5, 0}, {0, -10, 0}).hits.empty() && world.static_layer()->bodies().size() == 1, "remove() drops a static body");

        // Scaled bodies: the wrapper passes sub-shape ids through, so triangle keys and materials survive.
        PhysicsWorld scaled_world;
        scaled_world.set_static_layer(std::make_shared<const StaticCollisionLayer>(std::vector{body(9, shape, {0, 1, 0}, 3)}));
        const auto far = down(scaled_world, 2.5, -2.5), other = down(scaled_world, -2.5, 2.5);
        check(std::abs(far.point.y - 1) < 1e-4 && far.subshape != other.subshape && (far.subshape | other.subshape) == 1 &&
                  scaled_world.material_key(far) == "material.stone",
              "A scaled body is hit beyond the unscaled extent with its own triangle keys and material");

        // A frame keeps its layer alive and carries only its revision on the wire.
        const auto frame = world.capture();
        const auto stream = collision_stream(frame);
        check(stream.static_revision == world.static_layer()->revision(), "The wire names the layer revision");
        check(prepare_collision_frame(stream, world.static_layer()).statics == world.static_layer(), "A matching layer attaches");
        check(throws([&] { prepare_collision_frame(stream, first); }, "Static collision revision mismatch"),
              "A different layer is an explicit revision mismatch");

        // Budget: 16384 static bodies fit, one more does not.
        std::vector<StaticCollisionBody> many;
        for (std::uint64_t n = 0; n < static_collision_body_limit; ++n)
            many.push_back(body(100 + n, shape, {double(n % 128) * 3, 0, double(n / 128) * 3}));
        PhysicsWorld full;
        full.set_static_layer(std::make_shared<const StaticCollisionLayer>(many));
        check(full.static_insertions() == static_collision_body_limit, "The full static budget installs");
        many.push_back(body(1, shape, {-10, 0, -10}));
        check(throws([&] { StaticCollisionLayer{many}; }, "budget"), "One body over the budget is rejected");

        std::cout << "Static collision layer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
