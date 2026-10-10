#include <darkangel/camera.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace darkangel {
namespace {
constexpr double pi = 3.141592653589793;
void require(bool value, const char* message) { if (!value) throw std::invalid_argument(message); }
bool finite(const CameraVec& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
CameraVec lerp(const CameraVec& a, const CameraVec& b, double t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }
}

void validate_camera_rig(const CameraRigDefinition& rig) {
    require(!rig.cameras.empty() && rig.cameras.size() <= 8, "Camera rig needs one to eight virtual cameras");
    std::set<std::string> names;
    bool unconditional = false;
    for (const auto& camera : rig.cameras) {
        require(!camera.name.empty() && camera.name.size() <= 48 && names.insert(camera.name).second, "Virtual camera needs a unique name");
        require(camera.priority <= 1000 && camera.input_action.size() <= 64, "Virtual camera priority/input action bounds");
        for (auto value : {camera.distance, camera.height, camera.shoulder, camera.fov_degrees, camera.pitch_min_degrees, camera.pitch_max_degrees, camera.blend_seconds, camera.follow_damping_seconds})
            require(std::isfinite(value), "Virtual camera values must be finite");
        require(camera.distance >= .2 && camera.distance <= 50 && camera.height >= -5 && camera.height <= 10 && std::abs(camera.shoulder) <= 5, "Virtual camera distance/height/shoulder bounds");
        require(camera.fov_degrees >= 10 && camera.fov_degrees <= 120, "Virtual camera field of view must be 10 to 120 degrees");
        require(camera.pitch_min_degrees >= -89 && camera.pitch_max_degrees <= 89 && camera.pitch_min_degrees < camera.pitch_max_degrees, "Virtual camera pitch range");
        require(camera.blend_seconds >= 0 && camera.blend_seconds <= 10 && camera.follow_damping_seconds >= 0 && camera.follow_damping_seconds <= 5, "Virtual camera blend/damping bounds");
        require(static_cast<unsigned>(camera.facing) <= 1, "Virtual camera facing mode");
        unconditional |= camera.input_action.empty() && !camera.tag;
    }
    require(unconditional, "Camera rig needs one camera without activation conditions as its default");
}

CameraRigDefinition default_camera_rig() {
    CameraRigDefinition rig;
    VirtualCameraDefinition free;
    free.name = "FreeLook";
    VirtualCameraDefinition aim;
    aim.name = "Aim";
    aim.priority = 10;
    aim.input_action = "combat.ranged";
    aim.distance = 2.2;
    aim.height = 1.55;
    aim.shoulder = .55;
    aim.fov_degrees = 48;
    aim.pitch_min_degrees = -50;
    aim.blend_seconds = .2;
    aim.follow_damping_seconds = .03;
    aim.facing = CameraFacing::Camera;
    rig.cameras = {free, aim};
    return rig;
}

CameraDirector::CameraDirector(CameraRigDefinition rig, double yaw, double pitch_degrees) : rig_(std::move(rig)), yaw_(yaw), pitch_(pitch_degrees * pi / 180) {
    validate_camera_rig(rig_);
    require(std::isfinite(yaw) && std::isfinite(pitch_degrees), "Camera director start angles");
    std::span<const std::string> none;
    live_ = select(none, {});
    orbit(0, 0);
}

void CameraDirector::orbit(double yaw, double pitch) {
    require(std::isfinite(yaw) && std::isfinite(pitch) && std::abs(yaw) <= 64 && std::abs(pitch) <= 64, "Camera orbit input bounds");
    yaw_ = std::remainder(yaw_ + yaw, 2 * pi);
    const auto& camera = rig_.cameras[live_];
    pitch_ = std::clamp(pitch_ + pitch, camera.pitch_min_degrees * pi / 180, camera.pitch_max_degrees * pi / 180);
}

unsigned CameraDirector::select(std::span<const std::string> held, std::span<const TagId> tags) const {
    unsigned best = 0;
    bool found = false;
    for (unsigned index = 0; index < rig_.cameras.size(); ++index) {
        const auto& camera = rig_.cameras[index];
        if (!camera.input_action.empty() && std::find(held.begin(), held.end(), camera.input_action) == held.end()) continue;
        if (camera.tag && std::find(tags.begin(), tags.end(), camera.tag) == tags.end()) continue;
        if (!found || camera.priority > rig_.cameras[best].priority) { best = index; found = true; }
    }
    return best; // validation guarantees an unconditional camera, so found is always true
}

CameraPose CameraDirector::solve(unsigned index, const CameraTargets& targets) const {
    require(index < rig_.cameras.size() && finite(targets.follow) && (!targets.look || finite(*targets.look)), "Camera solve inputs");
    const auto& camera = rig_.cameras[index];
    double yaw = yaw_, pitch = std::clamp(pitch_, camera.pitch_min_degrees * pi / 180, camera.pitch_max_degrees * pi / 180);
    const CameraVec pivot{targets.follow[0], targets.follow[1] + camera.height, targets.follow[2]};
    if (camera.track_look_target && targets.look) {
        const double dx = (*targets.look)[0] - pivot[0], dy = (*targets.look)[1] - pivot[1], dz = (*targets.look)[2] - pivot[2], flat = std::hypot(dx, dz);
        if (flat > 1e-6 || std::abs(dy) > 1e-6) { yaw = std::atan2(dx, dz); pitch = std::clamp(std::atan2(-dy, flat) + 8 * pi / 180, camera.pitch_min_degrees * pi / 180, camera.pitch_max_degrees * pi / 180); }
    }
    // Forward looks from the camera through the pivot; positive pitch looks down.
    const CameraVec forward{std::sin(yaw) * std::cos(pitch), -std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const CameraVec right{-std::cos(yaw), 0, std::sin(yaw)};
    CameraPose pose;
    pose.fov_degrees = camera.fov_degrees;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const double offset = right[axis] * camera.shoulder;
        pose.position[axis] = pivot[axis] - forward[axis] * camera.distance + offset;
        pose.target[axis] = pivot[axis] + offset;
    }
    if (targets.look && camera.track_look_target) pose.target = *targets.look;
    return pose;
}

const CameraPose& CameraDirector::update(double seconds, const CameraTargets& targets, std::span<const std::string> held, std::span<const TagId> tags) {
    require(std::isfinite(seconds) && seconds >= 0 && seconds <= 10, "Camera update time step");
    const auto selected = select(held, tags);
    if (!started_) { follow_ = targets.follow; live_ = selected; blend_ = 1; started_ = true; orbit(0, 0); }
    else if (selected != live_) {
        from_ = pose_;
        live_ = selected;
        blend_ = rig_.cameras[live_].blend_seconds > 0 ? 0 : 1;
        orbit(0, 0); // clamp the shared pitch into the new camera's range
    }
    const auto& camera = rig_.cameras[live_];
    require(finite(targets.follow), "Camera follow target");
    // Exponential follow smoothing; a large jump (teleport/respawn) snaps instead of sweeping.
    const double jump = std::hypot(std::hypot(targets.follow[0] - follow_[0], targets.follow[1] - follow_[1]), targets.follow[2] - follow_[2]);
    const double alpha = camera.follow_damping_seconds <= 0 || jump > 20 ? 1 : 1 - std::exp(-seconds / camera.follow_damping_seconds);
    follow_ = lerp(follow_, targets.follow, alpha);
    CameraTargets smoothed{follow_, targets.look};
    auto live = solve(live_, smoothed);
    if (blend_ < 1) {
        blend_ = std::min(1., blend_ + seconds / camera.blend_seconds);
        const double eased = blend_ * blend_ * (3 - 2 * blend_);
        live.position = lerp(from_.position, live.position, eased);
        live.target = lerp(from_.target, live.target, eased);
        live.fov_degrees = from_.fov_degrees + (live.fov_degrees - from_.fov_degrees) * eased;
    }
    // Shakes displace the camera, never its target, so framing stays readable.
    CameraVec offset{};
    for (auto& active : shakes_) {
        active.age += seconds;
        const double life = 1 - active.age / active.shake.seconds;
        if (life <= 0) continue;
        const double phase = 2 * pi * active.shake.frequency * active.age, strength = active.shake.amplitude * life * life;
        offset[0] += std::sin(phase) * strength * -std::cos(yaw_);
        offset[2] += std::sin(phase) * strength * std::sin(yaw_);
        offset[1] += std::sin(phase * 1.37 + 1.1) * strength;
    }
    std::erase_if(shakes_, [](const auto& active) { return active.age >= active.shake.seconds; });
    pose_ = live;
    for (unsigned axis = 0; axis < 3; ++axis) pose_.position[axis] += offset[axis];
    return pose_;
}

void CameraDirector::shake(CameraShake value) {
    require(std::isfinite(value.amplitude) && value.amplitude >= 0 && value.amplitude <= 5 && std::isfinite(value.frequency) && value.frequency > 0 && value.frequency <= 120 && std::isfinite(value.seconds) && value.seconds > 0 && value.seconds <= 10, "Camera shake bounds");
    require(shakes_.size() < 8, "Camera shake budget");
    shakes_.push_back({value, 0});
}
}
