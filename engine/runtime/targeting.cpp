#include <darkangel/targeting.hpp>
#include <cmath>

namespace darkangel {
std::optional<TargetCandidate> select_soft_lock(MotionWarp warp,const MotorState& state,const MotorInput& input,std::span<const TargetCandidate> candidates){
    std::optional<TargetCandidate> best;double best_distance{},best_angle{};if(!warp.active)return best;
    const double centre=!warp.fresh?state.yaw:std::hypot(input.x,input.z)>.1?std::atan2(input.x,input.z):input.yaw;
    for(const auto& candidate:candidates){
        warp.point=candidate.position;auto resolved=resolve_motion_warp(warp,state,input);if(!resolved)continue;
        const double distance=std::hypot(candidate.position.x-state.position.x,candidate.position.z-state.position.z),angle=std::abs(std::remainder(resolved->yaw-centre,6.283185307179586));
        if(!best||distance<best_distance-1e-9||(std::abs(distance-best_distance)<=1e-9&&(angle<best_angle-1e-9||(std::abs(angle-best_angle)<=1e-9&&candidate.id<best->id)))){best=candidate;best_distance=distance;best_angle=angle;}
    }
    return best;
}
}
