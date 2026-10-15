#pragma once
#include <darkangel/spline.hpp>
#include <darkangel/world.hpp>
#include <optional>
#include <vector>

namespace darkangel::editor_app {
class Controller;
class Shell;
// A spline of the open scene as it stands now (a point being dragged included): the curve through its points in order.
struct BuiltSpline {
    StableId id;
    darkangel::SplineDefinition definition;
    std::vector<darkangel::MotorVec> points;
    std::optional<darkangel::Spline> curve;
};
std::vector<BuiltSpline> build_splines(Controller&, const Shell&);
} // namespace darkangel::editor_app
