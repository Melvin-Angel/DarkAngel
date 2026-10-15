// Scene-scale baseline (docs/implementation/current/SCENE_SCALE_PLAN.md, phase 0). Not a pass/fail test: it measures what static
// mesh count costs the physics world today. A source collision's ground and one hut are taken from a native collision source; the
// hut is copied across a grid until the world holds the requested number of static meshes. For each count it times the scene
// load, the per-tick capture into history, the live tick and owner replay (15-command batches, as prediction reconciles), and
// records the error when a count is over a limit. One JSON object per count is printed, and written to --report when given.
#include <darkangel/character_motor.hpp>
#include <darkangel/collision_asset.hpp>
#include <nlohmann/json.hpp>
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
// The source's smallest mesh is the ground, its largest the hut that is copied.
CollisionDefinition scene_with(const CollisionDefinition& source, unsigned meshes) {
    require(source.meshes.size() >= 2, "Bench source needs a ground and a hut mesh");
    const CollisionMeshDefinition *ground = &source.meshes.front(), *hut = &source.meshes.front();
    for (const auto& mesh : source.meshes) {
        if (mesh.data->triangles.size() < ground->data->triangles.size())
            ground = &mesh;
        if (mesh.data->triangles.size() > hut->data->triangles.size())
            hut = &mesh;
    }
    CollisionDefinition scene;
    scene.id = AssetId::random();
    scene.schema = 3;
    scene.meshes.push_back({1000, ground->data});
    for (unsigned copy = 1; copy < meshes; ++copy) {
        auto data = std::make_shared<CollisionMeshData>(*hut->data);
        // A grid of 12 m cells around the ground, away from the walking character.
        const double dx = -60.0 + 12.0 * (copy % 10), dz = 30.0 + 12.0 * (copy / 10);
        for (auto& vertex : data->vertices) {
            vertex.x += dx;
            vertex.z += dz;
        }
        data->signature = collision_mesh_signature(*data);
        scene.meshes.push_back({1000 + copy, std::move(data)});
    }
    return scene;
}
nlohmann::json measure(const CollisionDefinition& source, unsigned meshes, unsigned ticks) {
    nlohmann::json out{{"static_meshes", meshes}, {"triangles", 0}};
    std::size_t triangles = 0;
    try {
        const auto scene = scene_with(source, meshes);
        for (const auto& mesh : scene.meshes)
            triangles += mesh.data->triangles.size();
        out["triangles"] = triangles;
        PhysicsWorld world;
        auto start = Clock::now();
        world.load_scene(scene);
        out["load_ms"] = milliseconds(Clock::now() - start);
        CharacterMotor motor(world, {0, .51, 0}, 10);
        CollisionHistory history;
        std::vector<MotorInput> commands;
        auto baseline = motor.state();
        double capture{}, tick{}, replay{}, replay_worst{};
        unsigned replays = 0;
        for (unsigned frame = 0; frame < ticks; ++frame) {
            const auto next = world.tick() + 1;
            start = Clock::now();
            history.retain(world.capture());
            capture += milliseconds(Clock::now() - start);
            // Walk back and forth across the ground so the motor keeps colliding with it.
            const double x = (frame / 120) % 2 ? -.6 : .6;
            MotorInput command{next, next, 1, x, .2, 0, frame % 40 == 0, false};
            commands.push_back(command);
            start = Clock::now();
            motor.step(command);
            world.step();
            motor.post_physics();
            tick += milliseconds(Clock::now() - start);
            if (commands.size() == 15) {
                MotorState replayed;
                start = Clock::now();
                const auto result = replay_motor(baseline, commands, history, replayed, 10);
                const auto took = milliseconds(Clock::now() - start);
                require(result == ReplayResult::Applied, "Bench replay failed");
                replay += took;
                replay_worst = std::max(replay_worst, took);
                ++replays;
                baseline = motor.state();
                commands.clear();
            }
        }
        out["ticks"] = ticks;
        out["capture_us_per_tick"] = capture * 1000 / ticks;
        out["tick_us"] = tick * 1000 / ticks;
        out["replays"] = replays;
        out["replay_ms_per_15_ticks"] = replays ? replay / replays : 0.0;
        out["replay_ms_worst"] = replay_worst;
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
        std::vector<unsigned> counts{1, 2, 4, 8, 12, 16, 17, 32, 64, 128};
        unsigned ticks = 600;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--source" && i + 1 < argc)
                source_path = argv[++i];
            else if (arg == "--report" && i + 1 < argc)
                report = argv[++i];
            else if (arg == "--ticks" && i + 1 < argc)
                ticks = static_cast<unsigned>(std::stoul(argv[++i]));
            else if (arg == "--counts" && i + 1 < argc) {
                counts.clear();
                std::string list = argv[++i];
                for (std::size_t at = 0; at < list.size();) {
                    auto comma = list.find(',', at);
                    counts.push_back(static_cast<unsigned>(std::stoul(list.substr(at, comma - at))));
                    at = comma == std::string::npos ? list.size() : comma + 1;
                }
            } else
                throw std::runtime_error("Usage: SceneScaleBench --source <file.dacollision> [--counts 1,16,64] [--ticks 600] [--report out.json]");
        }
        require(!source_path.empty() && ticks >= 15, "SceneScaleBench needs --source and at least 15 ticks");
        std::ifstream input(source_path, std::ios::binary);
        require(bool(input), "Bench collision source missing");
        const std::string bytes{std::istreambuf_iterator<char>(input), {}};
        const auto source = decode_collision_source(bytes);
        nlohmann::json results = nlohmann::json::array();
        for (auto count : counts) {
            auto row = measure(source, count, ticks);
            std::cout << row.dump() << std::endl;
            results.push_back(std::move(row));
        }
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
