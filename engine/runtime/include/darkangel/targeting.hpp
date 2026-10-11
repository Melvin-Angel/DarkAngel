#pragma once
#include <darkangel/character_motor.hpp>
#include <optional>
#include <span>

namespace darkangel {
// Targeting module. It proposes targets from the caller's view of the world (authority:
// server state; owner: confirmed public state); the motor gates and applies any warp, and
// authority alone decides hits. Presentation may read the same answers and decides nothing.
struct TargetCandidate {std::uint64_t id{};MotorVec position{};};
// Best soft-lock candidate for an actor about to step with this warp: among candidates the
// motor would accept (range and facing cone), the nearest wins, the smaller angle breaks
// ties and the lower identity breaks exact ties.
std::optional<TargetCandidate> select_soft_lock(MotionWarp,const MotorState&,const MotorInput&,std::span<const TargetCandidate>);
}
