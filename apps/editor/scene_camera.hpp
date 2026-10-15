#pragma once
#include <array>
#include <optional>

namespace darkangel::editor_app {
// Editor presentation only. No scene serialization, gameplay camera or runtime motor dependencies.
using CameraPoint = std::array<double, 3>;
struct SceneBounds {
    CameraPoint center{};
    double radius{1};
};
class SceneCamera {
  public:
    enum class Mode { Orbit, Fly };
    Mode mode{Mode::Orbit};
    double speed{5};
    static constexpr double min_speed = .1, max_speed = 2000;
    CameraPoint eye{26, 24, 34}, pivot{0, 1, 0};
    double yaw{}, pitch{};
    SceneCamera();
    CameraPoint forward() const;
    CameraPoint right() const;
    CameraPoint up() const;
    void look_from(CameraPoint position, CameraPoint target);
    void set_mode(Mode, std::optional<SceneBounds> selection);
    void rotate(double dx, double dy);
    void pan(double dx, double dy, double height);
    void wheel(double steps, bool change_speed);
    void move(double lateral, double vertical, double forward, double seconds, bool fast);
    void focus(const SceneBounds&, double aspect);
};
struct SceneNavigationInput {
    bool enabled{}, hovered{}, focused{}, text{}, popup{}, blocked{}, over_gizmo{}, focus_lost{};
    bool left{}, middle{}, right{}, left_pressed{}, middle_pressed{}, right_pressed{};
    bool alt{}, shift{}, escape{}, toggle_mode{}, focus_selection{};
    double dx{}, dy{}, wheel{}, lateral{}, vertical{}, forward{}, seconds{}, height{500}, aspect{1};
};
class SceneNavigation {
  public:
    SceneCamera camera;
    enum class Drag { None, Orbit, Look, Pan };
    Drag drag{Drag::None};
    void cancel() { drag = Drag::None; }
    void update(const SceneNavigationInput&, std::optional<SceneBounds> selection);
    bool active() const { return drag != Drag::None; }
};
} // namespace darkangel::editor_app
