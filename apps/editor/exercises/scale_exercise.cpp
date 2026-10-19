#include "scale_exercise.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <darkangel/editor_service.hpp>
#include <nlohmann/json.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace darkangel::editor_app::exercises {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
nlohmann::json memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        return nullptr;
    return {{"working_set_mb", counters.WorkingSetSize / 1048576.0}, {"private_mb", counters.PrivateUsage / 1048576.0}};
}
nlohmann::json frame_summary(std::vector<double> frames) {
    if (frames.empty())
        return nullptr;
    std::sort(frames.begin(), frames.end());
    double total = 0;
    for (auto value : frames)
        total += value;
    return {{"frames", frames.size()},
            {"mean_ms", total / frames.size()},
            {"p50_ms", frames[frames.size() / 2]},
            {"p95_ms", frames[std::min(frames.size() - 1, frames.size() * 95 / 100)]},
            {"max_ms", frames.back()}};
}
class Scale final : public Exercise {
    ScaleContext context_;
    nlohmann::json config_, report_;
    unsigned authoring_frames_{120}, play_frames_{300}, warmup_{10};
    Clock::time_point last_{};
    bool timing_{};
    std::vector<double> authoring_, play_;
    std::array<double, 4> authoring_draws_{}, play_draws_{}; // visible, culled, draw calls, frames
    std::array<double, 6> authoring_sections_{}, play_sections_{}; // summed ms per frame section
    std::array<double, 5> authoring_render_{}, play_render_{};
    std::array<double, 3> authoring_pass_draws_{}, play_pass_draws_{};
    std::array<double, 4> optimization_counts_{};
    StableId moved_;
    bool prepared_{}, playing_{}, done_{};
    unsigned play_start_{};
    std::string result_;

    // One timed editor operation; its error is the result rather than ending the exercise.
    template <class F> bool step(const char* name, F&& action) {
        const auto start = Clock::now();
        nlohmann::json entry;
        try {
            action();
            entry["ok"] = true;
        } catch (const std::exception& error) {
            entry["ok"] = false;
            entry["error"] = error.what();
        }
        entry["ms"] = milliseconds(Clock::now() - start);
        entry["memory"] = memory();
        // The console line the operation left, for its own breakdown (Play preparation reports its phases).
        if (!context_.editor.console.empty())
            entry["log"] = context_.editor.console.back().message;
        report_["steps"][name] = entry;
        std::cout << "Editor scale " << name << " " << entry.dump() << std::endl;
        return entry["ok"].get<bool>();
    }
    void write() {
        report_["authoring_frames"] = frame_summary(authoring_);
        report_["play_frames"] = frame_summary(play_);
        const auto draws = [](const std::array<double, 4>& totals) -> nlohmann::json {
            if (!totals[3])
                return nullptr;
            return {{"visible", totals[0] / totals[3]}, {"culled", totals[1] / totals[3]}, {"draws", totals[2] / totals[3]}};
        };
        report_["authoring_draws"] = draws(authoring_draws_);
        report_["play_draws"] = draws(play_draws_);
        const auto sections = [](const std::array<double, 6>& summed, double frames) -> nlohmann::json {
            if (!frames)
                return nullptr;
            const char* names[] = {"exercises_update", "ui", "play", "scene_render", "effects", "present"};
            nlohmann::json out;
            for (std::size_t i = 0; i < summed.size(); ++i)
                out[names[i]] = summed[i] / frames;
            return out;
        };
        report_["authoring_sections_ms"] = sections(authoring_sections_, authoring_draws_[3]);
        report_["play_sections_ms"] = sections(play_sections_, play_draws_[3]);
        const auto render = [](const auto& sums, double frames, const auto& names) {
            nlohmann::json out = nlohmann::json::object();
            if (frames) for (std::size_t i = 0; i < sums.size(); ++i) out[names[i]] = sums[i] / frames;
            return out;
        };
        const std::array<const char*, 5> render_names{"prepare", "shadows", "color", "outlines", "post"};
        const std::array<const char*, 3> draw_names{"shadows", "color", "outlines"};
        report_["timing_contract"] = "CPU wall milliseconds including driver backpressure; not GPU timestamps";
        report_["authoring_render_cpu_ms"] = render(authoring_render_, authoring_draws_[3], render_names);
        report_["play_render_cpu_ms"] = render(play_render_, play_draws_[3], render_names);
        report_["authoring_pass_draws"] = render(authoring_pass_draws_, authoring_draws_[3], draw_names);
        report_["play_pass_draws"] = render(play_pass_draws_, play_draws_[3], draw_names);
        const std::array<const char*, 4> optimization_names{"sky_builds", "sky_hits", "shadow_rejected", "shadow_ranges_merged"};
        report_["optimizations_per_frame"] = render(optimization_counts_, authoring_draws_[3] + play_draws_[3], optimization_names);
        report_["memory_end"] = memory();
        std::ofstream output(config_.at("report").get<std::string>());
        output << report_.dump(2) << "\n";
        require(output.good(), "Scale report write failed");
        done_ = true;
        result_ = "Editor scale report written";
        std::cout << "Editor scale report=" << config_.at("report").get<std::string>() << std::endl;
    }

  public:
    explicit Scale(ScaleContext context) : context_(context) {
        if (context_.options.exercise_scale.empty()) {
            done_ = true;
            return;
        }
        std::ifstream input(context_.options.exercise_scale);
        require(bool(input), "--exercise-scale config missing");
        config_ = nlohmann::json::parse(input);
        require(config_.contains("report") && config_.contains("used_model") && config_.contains("new_model"),
                "--exercise-scale config needs report, used_model and new_model");
        authoring_frames_ = config_.value("authoring_frames", authoring_frames_);
        play_frames_ = config_.value("play_frames", play_frames_);
        report_ = {{"config", config_}, {"steps", nlohmann::json::object()}};
    }
    void frame(unsigned frame) override {
        if (done_)
            return;
        auto& editor = context_.editor;
        auto& shell = context_.shell;
        if (config_.value("resolution_probe", false)) {
            require(context_.options.hidden, "Resolution probe requires an isolated hidden run");
            if(frame==1)shell.switch_tab(Workspace::Game);
            if(frame==10)shell.game_resolution=1;
            if(frame==20)shell.game_resolution=2;
            if(frame==30)shell.game_resolution=3;
            if(frame==40)shell.game_resolution=4;
            if(frame==50)shell.game_resolution=0;
            if(frame==60){shell.game_resolution=1;shell.ui_scale=1.f;}
            if(frame==70)shell.switch_tab(Workspace::Scene);
            if(frame==80)shell.switch_tab(Workspace::Game);
            if(frame==90)shell.save_preferences();
            if(frame>=5&&frame%10==5){
                const auto actual=shell.viewport_render_sizes[0];
                const auto& panel=shell.viewport_area();
                report_["resolution_probe"].push_back({{"frame",frame},{"game",shell.workspace==Workspace::Game},
                    {"preset",shell.game_resolution},{"target",{actual.width,actual.height}},
                    {"panel",{panel.x,panel.y,panel.width,panel.height}}});
            }
        }
        if (config_.value("render_probe", false)) {
            require(context_.options.hidden, "Render cache probe requires an isolated hidden run");
            const std::map<unsigned, const char*> observations{{15,"steady"},{21,"seed"},{31,"post"},{42,"resize"},
                {51,"cloud"},{61,"disabled"},{71,"reenabled"},{81,"seed_again"}};
            if (const auto observation = observations.find(frame); observation != observations.end())
                report_["render_probe"][observation->second] = shell.render_optimizations;
            // Changing UI scale changes the actual scene target dimensions on
            // the following frame, including when the viewport is floating.
            if (frame == 40) shell.ui_scale = shell.ui_scale < 1 ? 1.f : .75f;
            if (frame == 20 || frame == 30 || frame == 50 || frame == 60 || frame == 70 || frame == 80) {
                const auto local = config_.value("render_local", std::string("00000000000000000000000000000001"));
                const auto entity = editor.document->entity(local);
                require(bool(entity), "Render cache probe settings entity missing");
                auto source = nlohmann::json::parse(*entity);
                auto& settings = source["optional"]["rendering"];
                if (frame == 20 || frame == 80) settings["sky"]["seed"] = settings["sky"].value("seed", 0u) + 1;
                if (frame == 30) settings["post"]["exposure"] = .25;
                if (frame == 50) settings["sky"]["cloud_amount"] = .25;
                if (frame == 60 || frame == 70) settings["sky_enabled"] = frame == 70;
                editor.document->commit(editor.document->prepare_entities(editor.document->revision(), {{local,source.dump()}}));
            }
        }
        const auto now = Clock::now();
        // The first frames after startup and after Play starts are warm-up, not steady state.
        if (timing_ && frame > warmup_ && (!playing_ || frame > play_start_ + warmup_)) {
            (playing_ ? play_ : authoring_).push_back(milliseconds(now - last_));
            // What the scene view submitted on the frame just drawn.
            auto& totals = playing_ ? play_draws_ : authoring_draws_;
            totals[0] += shell.scene_visible;
            totals[1] += shell.scene_culled;
            totals[2] += shell.scene_draws;
            ++totals[3];
            auto& sections = playing_ ? play_sections_ : authoring_sections_;
            for (std::size_t i = 0; i < sections.size(); ++i)
                sections[i] += shell.frame_sections[i];
            auto& render = playing_ ? play_render_ : authoring_render_;
            for (std::size_t i = 0; i < render.size(); ++i) render[i] += shell.render_sections[i];
            auto& pass_draws = playing_ ? play_pass_draws_ : authoring_pass_draws_;
            for (std::size_t i = 0; i < pass_draws.size(); ++i) pass_draws[i] += shell.render_draws[i];
            for (std::size_t i = 0; i < optimization_counts_.size(); ++i) optimization_counts_[i] += shell.render_optimizations[i];
        }
        last_ = now;
        timing_ = true;
        const unsigned at = authoring_frames_ + warmup_;
        if (frame == 1) {
            // Benchmark option only; hidden runs use isolated preferences and
            // end before authoring operations. This exposes the actual full view cost.
            if (config_.value("maximize_viewport", false)) shell.scene_maximized = true;
            const auto& plan = editor.document->plan();
            // Every model placement, the player's skin included; pickups, altars, doors and platforms are not static.
            std::size_t placements = 0;
            for (const auto& [object, asset] : plan.models)
                if (!plan.pickups.contains(object) && !plan.altars.contains(object) && !plan.movers.contains(object))
                    ++placements;
            report_["scene"] = {{"objects", plan.origins.size()}, {"models", plan.models.size()}, {"model_placements", placements}};
            report_["memory_start"] = memory();
        }
        // Each operation runs on its own frame, so a frame's time never includes two of them.
        if (frame == at + 1) {
            timing_ = false;
            step("place_used_model", [&] {
                moved_ = shell.place(editor, Shell::PlaceItem{AssetId::parse(config_.at("used_model").get<std::string>()), "Scale used", "Environment", false, 0},
                                     config_.value("place_x", 40.0), config_.value("place_z", 40.0));
            });
        }
        if (frame == at + 2) {
            timing_ = false;
            step("move_placement", [&] {
                require(bool(moved_), "Nothing was placed to move");
                PropertyChange change{moved_, 1, 2, config_.value("place_x", 40.0) + 2.0};
                auto prepared = editor.document->prepare(editor.document->revision(), {&change, 1}, EditScope::Placement);
                editor.document->commit(prepared);
            });
        }
        if (frame == at + 3) {
            timing_ = false;
            step("place_new_model", [&] {
                shell.place(editor, Shell::PlaceItem{AssetId::parse(config_.at("new_model").get<std::string>()), "Scale new", "Environment", false, 0},
                            config_.value("place_x", 40.0), config_.value("place_z", 40.0) + 8.0);
            });
        }
        if (frame == at + 4) {
            timing_ = false;
            prepared_ = step("prepare_play", [&] {
                require(bool(editor.prepare_play_resources), "This session cannot prepare Play");
                editor.prepare_play_resources();
            });
        }
        if (frame == at + 5) {
            timing_ = false;
            if (prepared_)
                playing_ = step("start_play", [&] { editor.play(); });
            play_start_ = frame;
            if (!playing_)
                write();
        }
        if (playing_ && frame >= at + 6 + warmup_ + play_frames_) {
            editor.stop();
            write();
        }
    }
    void finish() override {
        if (!done_ && !config_.is_null())
            write();
    }
    bool done() const override { return done_; }
    std::string_view result() const override { return result_; }
};
} // namespace
std::unique_ptr<Exercise> make_scale(ScaleContext context) { return std::make_unique<Scale>(context); }
} // namespace darkangel::editor_app::exercises
