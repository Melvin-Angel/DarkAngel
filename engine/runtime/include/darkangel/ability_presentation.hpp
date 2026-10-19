#pragma once

#include <darkangel/ability.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace darkangel {

// World-space presentation of a projectile while its authored ability timeline
// prepares it. This is visual state only; collision and damage begin at release.
struct PreparedProjectilePose {
    std::array<double, 3> position{};
    double travel{};
};

inline std::optional<PreparedProjectilePose> prepared_projectile_pose(
    const AbilityProjectilePreparation& preparation, const ActionDefinition& action,
    double clock, const std::array<double, 3>& ground, const std::array<double, 3>& hand) {
    if (!std::isfinite(clock) || !std::all_of(ground.begin(), ground.end(), [](double v) { return std::isfinite(v); }) ||
        !std::all_of(hand.begin(), hand.end(), [](double v) { return std::isfinite(v); }))
        return std::nullopt;

    const auto block = [&](unsigned id, ActionBlockKind kind) -> const ActionBlock* {
        const auto found = std::find_if(action.blocks.begin(), action.blocks.end(), [&](const auto& candidate) {
            return candidate.id == id && candidate.kind == kind && candidate.begin < candidate.end;
        });
        return found == action.blocks.end() ? nullptr : &*found;
    };
    const auto* emergence = block(preparation.emergence_block, ActionBlockKind::Vfx);
    const auto* movement = block(preparation.move_block, ActionBlockKind::Vfx);
    const auto release = std::find_if(action.blocks.begin(), action.blocks.end(), [&](const auto& candidate) {
        return candidate.id == preparation.release_block && candidate.kind == ActionBlockKind::HitWindow;
    });
    if (!emergence || !movement || release == action.blocks.end() || emergence->end > movement->begin ||
        movement->end > release->begin || clock < emergence->begin || clock >= release->begin ||
        !std::isfinite(preparation.rise) || preparation.rise < 0)
        return std::nullopt;

    PreparedProjectilePose pose{ground, 0};
    if (clock < emergence->end) {
        const auto t = std::clamp((clock - emergence->begin) / double(emergence->end - emergence->begin), 0., 1.);
        pose.position[1] += preparation.rise * t;
        return pose;
    }

    const auto t = std::clamp((clock - movement->begin) / double(movement->end - movement->begin), 0., 1.);
    pose.travel = t * t * (3. - 2. * t);
    const std::array<double, 3> raised{ground[0], ground[1] + preparation.rise, ground[2]};
    for (std::size_t axis = 0; axis < pose.position.size(); ++axis)
        pose.position[axis] = raised[axis] + (hand[axis] - raised[axis]) * pose.travel;
    return pose;
}

} // namespace darkangel
