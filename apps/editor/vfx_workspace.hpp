#pragma once
#include <darkangel/assets.hpp>
#include <darkangel/vfx.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace darkangel::editor_app {
class Controller;
// The device's visual effects service and the VFX tab: the selected effect, cooked and looped in the preview and cooked
// again when its source changes on disk, plus the cue, trail and decal forms.
class VfxWorkspace {
  public:
    explicit VfxWorkspace(Controller&);
    // Makes the service on this device; a failure is logged and kept in `failed`, and the editor runs without effects.
    void start(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, int color_format, int depth_format);
    std::vector<AssetInfo> effects() const;
    void cook();
    void select(const AssetInfo&);
    void draw_cues();
    void draw_panel();

    struct State {
        AssetId selected;
        std::string path, error, generation;
        std::filesystem::file_time_type seen{};
        bool cooked{};
        std::uint64_t handle{};
        float yaw{.6f}, distance{4.5f}, height{1.f};
        char name[49]{"new_effect"};
        AssetId cue, primitive;
        char cue_name[49]{"new_cue"}, primitive_name[49]{"new_trail"};
        std::string cue_error;
        double idle{};
        std::size_t textures{};
        unsigned cooks{};
    };
    std::unique_ptr<VfxService> service;
    std::string failed;
    State state;
    // Called after a cue, trail or decal source is saved, so the next Play cooks its cues again.
    std::function<void()> on_cue_sources_saved;
    // Scrolls the VFX tab to the cue form (the cue form exercise captures it).
    bool reveal_cue_form{};

  private:
    Controller& editor_;
};
} // namespace darkangel::editor_app
