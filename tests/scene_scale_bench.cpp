// Scene-scale physics measurement (docs/implementation/current/SCENE_SCALE_PLAN.md). Not a pass/fail test, apart from the
// invariants it checks: it measures what static placements cost the physics world. A native collision source gives a ground mesh
// and a hut mesh; scenes place the hut N times, either as one shared model ("repeated") or spread over many distinct models
// ("unique": each a slightly different copy, so each is its own shape). For each scene it times building the static layer and
// installing it in a world, counts the distinct shapes built and the Jolt bodies inserted, then runs a walking character for 600
// ticks with owner replay in 15-command batches, both in a persistent replay world (which must insert no static body) and in a
// fresh world per replay (what every reconcile paid before the static layer). It also moves one placement through the layer diff.
// One JSON object per scene is printed, and written to --report when given.
#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/static_collision.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace darkangel;
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
void require(bool b, const char* e) {
    if (!b)
        throw std::runtime_error(e);
}
double private_mb() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    return counters.PrivateUsage / 1048576.0;
}
struct Source {
    std::shared_ptr<const CollisionMeshData> ground, hut;
};
// The source's smallest mesh is the ground, its largest the hut that is placed.
Source source_meshes(const CollisionDefinition& source) {
    require(source.meshes.size() >= 2, "Bench source needs a ground and a hut mesh");
    Source out{source.meshes.front().data, source.meshes.front().data};
    for (const auto& mesh : source.meshes) {
        if (mesh.data->triangles.size() < out.ground->triangles.size())
            out.ground = mesh.data;
        if (mesh.data->triangles.size() > out.hut->triangles.size())
            out.hut = mesh.data;
    }
    return out;
}
// A distinct model: the hut stretched by a per-variant factor, so its signature (and shape) differs.
std::shared_ptr<const CollisionMeshData> variant(const CollisionMeshData& hut, unsigned index) {
    auto data = std::make_shared<CollisionMeshData>(hut);
    data->runtime = AssetId::random();
    const double stretch = 1.0 + 0.001 * (index + 1);
    for (auto& vertex : data->vertices)
        vertex.y = double(float(vertex.y * stretch));
    data->signature = collision_mesh_signature(*data);
    return data;
}
StableId object(std::uint64_t index) { return StableId{0x5ca1e, index + 1}; }
struct Scene {
    std::shared_ptr<const StaticCollisionLayer> layer;
    std::vector<StaticCollisionBody> bodies;
    double build_ms{};
    std::size_t shape_builds{};
};
Scene scene(const Source& source, unsigned placements, unsigned models) {
    Scene out;
    const auto start = Clock::now();
    CollisionGeometryCache cache;
    std::vector<std::shared_ptr<const CollisionMeshData>> meshes{source.hut};
    for (unsigned m = 1; m < models; ++m)
        meshes.push_back(variant(*source.hut, m));
    StaticCollisionBody ground;
    ground.id = 1000;
    ground.geometry = cache.get(source.ground);
    out.bodies.push_back(ground);
    // A square grid of 12 m cells north of the ground, clear of the walking character.
    const unsigned columns = std::max(1u, unsigned(std::ceil(std::sqrt(double(placements)))));
    for (unsigned i = 0; i < placements; ++i) {
        StaticCollisionBody body;
        body.object = object(i);
        body.id = static_body_id(body.object);
        body.geometry = cache.get(meshes[i % meshes.size()]);
        body.position = {-6.0 * columns + 12.0 * (i % columns), 0, 40.0 + 12.0 * (i / columns)};
        body.rotation = placement_rotation(0, 0.37 * i, 0);
        out.bodies.push_back(body);
    }
    out.layer = std::make_shared<const StaticCollisionLayer>(out.bodies);
    out.build_ms = milliseconds(Clock::now() - start);
    out.shape_builds = cache.builds();
    return out;
}
nlohmann::json measure(const Source& source, unsigned placements, unsigned models, unsigned ticks) {
    nlohmann::json out{{"placements", placements}, {"models", models}};
    try {
        const double memory_before = private_mb();
        auto built = scene(source, placements, models);
        out["layer_build_ms"] = built.build_ms;
        out["shape_builds"] = built.shape_builds;
        out["unique_geometry"] = built.layer->unique_geometry();
        PhysicsWorld world;
        auto start = Clock::now();
        world.set_static_layer(built.layer);
        out["install_ms"] = milliseconds(Clock::now() - start);
        out["static_insertions"] = world.static_insertions();
        out["private_mb_delta"] = private_mb() - memory_before;

        // A placement moved through the diff: one body changes, none is inserted.
        auto moved = built.bodies;
        moved.back().position.x += 1;
        auto next = std::make_shared<const StaticCollisionLayer>(moved);
        const auto insertions = world.static_insertions();
        start = Clock::now();
        world.set_static_layer(next);
        out["move_one_ms"] = milliseconds(Clock::now() - start);
        require(world.static_insertions() == insertions, "Moving one placement inserted a static body");
        world.set_static_layer(built.layer);

        CharacterMotor motor(world, {0, .51, 0}, 10);
        CollisionHistory history;
        std::vector<MotorInput> commands;
        auto baseline = motor.state();
        PhysicsWorld replay_world(PhysicsWorld::Mode::Replay);
        double capture{}, tick{}, replay{}, replay_worst{}, fresh{};
        unsigned replays = 0;
        std::uint64_t replay_insertions_after_first = 0;
        for (unsigned frame = 0; frame < ticks; ++frame) {
            const auto next_tick = world.tick() + 1;
            start = Clock::now();
            history.retain(world.capture());
            capture += milliseconds(Clock::now() - start);
            const double x = (frame / 120) % 2 ? -.6 : .6;
            MotorInput command{next_tick, next_tick, 1, x, .2, 0, frame % 40 == 0, false};
            commands.push_back(command);
            start = Clock::now();
            motor.step(command);
            world.step();
            motor.post_physics();
            tick += milliseconds(Clock::now() - start);
            if (commands.size() == 15) {
                MotorState replayed;
                start = Clock::now();
                require(replay_motor(baseline, std::span<const MotorInput>(commands), history, replayed, 10) == ReplayResult::Applied,
                        "Bench fresh-world replay failed");
                fresh += milliseconds(Clock::now() - start);
                std::vector<MotorCommand> batch;
                for (const auto& input : commands)
                    batch.push_back({input, {}});
                MotorState persistent;
                start = Clock::now();
                require(replay_motor(baseline, batch, history, persistent, 10, replay_world) == ReplayResult::Applied,
                        "Bench persistent replay failed");
                const auto took = milliseconds(Clock::now() - start);
                require(persistent.position == replayed.position, "Persistent and fresh replay disagree");
                if (!replays)
                    replay_insertions_after_first = replay_world.static_insertions();
                replay += took;
                replay_worst = std::max(replay_worst, took);
                ++replays;
                baseline = motor.state();
                commands.clear();
            }
        }
        require(replay_world.static_insertions() == replay_insertions_after_first,
                "A persistent replay world inserted static bodies after its first replay");
        out["ticks"] = ticks;
        out["capture_us_per_tick"] = capture * 1000 / ticks;
        out["tick_us"] = tick * 1000 / ticks;
        out["replays"] = replays;
        out["replay_ms_per_15_ticks"] = replays ? replay / replays : 0.0;
        out["replay_ms_worst"] = replay_worst;
        out["fresh_world_replay_ms"] = replays ? fresh / replays : 0.0;
        out["replay_static_insertions_after_first"] = replay_world.static_insertions() - replay_insertions_after_first;
        out["ok"] = true;
    } catch (const std::exception& error) {
        out["ok"] = false;
        out["error"] = error.what();
    }
    return out;
}
} // namespace
int main(int argc, char** argv) {
    try {
        std::string source_path, report;
        std::vector<unsigned> counts{100, 500, 2000, 16383};
        unsigned ticks = 600, unique = 200;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--source" && i + 1 < argc)
                source_path = argv[++i];
            else if (arg == "--report" && i + 1 < argc)
                report = argv[++i];
            else if (arg == "--ticks" && i + 1 < argc)
                ticks = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == "--unique" && i + 1 < argc)
                unique = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == "--counts" && i + 1 < argc) {
                counts.clear();
                std::string list = argv[++i];
                for (std::size_t at = 0; at < list.size();) {
                    auto comma = list.find(',', at);
                    counts.push_back(static_cast<unsigned>(std::stoul(list.substr(at, comma - at))));
                    at = comma == std::string::npos ? list.size() : comma + 1;
                }
            } else
                throw std::runtime_error(
                    "Usage: SceneScaleBench --source <file.dacollision> [--counts 100,2000] [--unique 200] [--ticks 600] [--report out.json]");
        }
        require(!source_path.empty() && ticks >= 15, "SceneScaleBench needs --source and at least 15 ticks");
        std::ifstream input(source_path, std::ios::binary);
        require(bool(input), "Bench collision source missing");
        const std::string bytes{std::istreambuf_iterator<char>(input), {}};
        const auto source = source_meshes(decode_collision_source(bytes));
        {
            // What an empty world costs to create: prediction rebases and one-off replays create worlds.
            const auto start = Clock::now();
            for (unsigned n = 0; n < 50; ++n)
                PhysicsWorld world(PhysicsWorld::Mode::Replay);
            std::cout << nlohmann::json{{"empty_world_construct_ms", milliseconds(Clock::now() - start) / 50}}.dump() << std::endl;
        }
        nlohmann::json results = nlohmann::json::array();
        for (auto count : counts)
            for (const unsigned models : {1u, std::min(count, unique)}) {
                auto row = measure(source, count, models, ticks);
                std::cout << row.dump() << std::endl;
                results.push_back(std::move(row));
                if (models == 1 && std::min(count, unique) == 1)
                    break;
            }
        // The budget: the ground plus 16384 placements is one body too many.
        auto over = measure(source, unsigned(static_collision_body_limit), 1, 15);
        std::cout << over.dump() << std::endl;
        require(!over["ok"].get<bool>(), "A layer over the static body budget was accepted");
        results.push_back(std::move(over));
        if (!report.empty()) {
            std::ofstream output(report);
            output << nlohmann::json{{"bench", "physics"}, {"source", source_path}, {"results", results}}.dump(2) << "\n";
            require(output.good(), "Bench report write failed");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
