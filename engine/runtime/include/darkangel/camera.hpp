#pragma once
#include <darkangel/tags.hpp>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace darkangel {
// Presentation-only camera rig, inspired by virtual cameras and a blending brain.
// It never validates hits, moves actors or decides gameplay; it consumes already
// confirmed/predicted state (follow and look targets, held local input, owner tags).
using CameraVec = std::array<double, 3>;
struct CameraPose { CameraVec position{}, target{}; double fov_degrees{60}; };
// How the controlled actor faces while this camera is live: toward its movement
// (free-look) or with the camera (aim / strafe).
enum class CameraFacing { Movement, Camera };
// One virtual camera: a third-person orbit body around the follow target.
struct VirtualCameraDefinition {
    std::string name;
    unsigned priority{};
    // Activation. Empty conditions make the camera always eligible; otherwise every
    // present condition must hold. input_action is a held semantic input action
    // (local intent); tag is a registered gameplay tag present on the owner.
    std::string input_action;
    TagId tag{};
    double distance{4.5}, height{1.5}, shoulder{}, fov_degrees{60};
    double pitch_min_degrees{-35}, pitch_max_degrees{60};
    // Seconds to blend to this camera when it becomes live; follow smoothing time.
    double blend_seconds{.25}, follow_damping_seconds{.08};
    CameraFacing facing{CameraFacing::Movement};
    // With a supplied look target, orbit so the follow target stays framed toward it
    // (lock-on, dialogue). Without one the shared orbit angles are used.
    bool track_look_target{};
};
struct CameraRigDefinition { std::vector<VirtualCameraDefinition> cameras; };
// 1-8 cameras, unique names, finite bounded values and at least one unconditional camera.
void validate_camera_rig(const CameraRigDefinition&);
// Free-look orbit plus an over-shoulder aim camera on the ranged modifier.
CameraRigDefinition default_camera_rig();
struct CameraTargets { CameraVec follow{}; std::optional<CameraVec> look; };
// Additive, decaying positional shake. Deterministic; several may overlap (bounded).
struct CameraShake { double amplitude{.1}, frequency{18}, seconds{.3}; };

class CameraDirector {
public:
    explicit CameraDirector(CameraRigDefinition, double yaw = 0, double pitch_degrees = 12);
    // Shared orbit angles driven by mouse/stick. Pitch clamps to the live camera's range.
    void orbit(double yaw_radians, double pitch_radians);
    // Eligible camera with the highest priority (ties: earliest authored).
    unsigned select(std::span<const std::string> held_actions, std::span<const TagId> tags) const;
    // Advances damping, blending and shakes and returns the blended pose.
    const CameraPose& update(double seconds, const CameraTargets&, std::span<const std::string> held_actions, std::span<const TagId> tags);
    // Unblended pose of one virtual camera at the current orbit angles (authoring preview).
    CameraPose solve(unsigned camera, const CameraTargets&) const;
    void shake(CameraShake);
    unsigned live() const { return live_; }
    // 1 when the live camera fully owns the output, lower while blending in.
    double blend() const { return blend_; }
    double yaw() const { return yaw_; }
    double pitch() const { return pitch_; }
    const CameraPose& pose() const { return pose_; }
    const CameraRigDefinition& rig() const { return rig_; }
private:
    struct ActiveShake { CameraShake shake; double age{}; };
    CameraRigDefinition rig_;
    double yaw_{}, pitch_{}, blend_{1};
    unsigned live_{};
    bool started_{};
    CameraVec follow_{};
    CameraPose pose_, from_;
    std::vector<ActiveShake> shakes_;
};
}
