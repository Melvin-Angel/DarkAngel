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
        const auto now = Clock::now();
        // The first frames after startup and after Play starts are warm-up, not steady state.
        if (timing_ && frame > warmup_ && (!playing_ || frame > play_start_ + warmup_))
            (playing_ ? play_ : authoring_).push_back(milliseconds(now - last_));
        last_ = now;
        timing_ = true;
        const unsigned at = authoring_frames_ + warmup_;
        if (frame == 1) {
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
