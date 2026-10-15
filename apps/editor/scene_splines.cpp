#include "scene_splines.hpp"
#include "editor_controller.hpp"
#include "editor_ui.hpp"
#include <algorithm>
#include <utility>

namespace darkangel::editor_app {
std::vector<BuiltSpline> build_splines(Controller& editor, const Shell& shell) {
    std::vector<BuiltSpline> result;
    if (!editor.document)
        return result;
    const auto& plan = editor.document->plan();
    for (const auto& [object, json] : plan.splines) {
        BuiltSpline built;
        built.id = object;
        try {
            built.definition = darkangel::decode_spline(json);
        } catch (const std::exception&) {
            continue;
        }
        std::vector<std::pair<unsigned, StableId>> order;
        for (const auto& [point, info] : plan.spline_points)
            if (info.spline == object)
                order.push_back({info.index, point});
        std::sort(order.begin(), order.end());
        for (const auto& [index, point] : order) {
            auto entity = editor.displayed().find(point);
            if (!editor.displayed().valid(entity))
                continue;
            auto pose = editor.displayed().read(entity).transform;
            if (shell.draft && editor.selected == point)
                pose = *shell.draft;
            built.points.push_back({pose.x, pose.y, pose.z});
        }
        if (built.points.size() >= 2 && built.points.size() <= darkangel::spline_point_limit) {
            try {
                built.curve.emplace(built.points, built.definition.closed);
            } catch (const std::exception&) {
            }
        }
        result.push_back(std::move(built));
    }
    return result;
}
} // namespace darkangel::editor_app
