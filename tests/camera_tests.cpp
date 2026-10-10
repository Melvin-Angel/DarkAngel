#include <darkangel/camera.hpp>
#include <cmath>
#include <iostream>
using namespace darkangel;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <class F> void rejects(F operation) { bool failed = false; try { operation(); } catch (const std::exception&) { failed = true; } check(failed, "Expected camera rejection"); }
double distance(const CameraVec& a, const CameraVec& b) { return std::hypot(std::hypot(a[0] - b[0], a[1] - b[1]), a[2] - b[2]); }
const double pi = 3.141592653589793;
}
int main() { try {
    auto rig = default_camera_rig();
    validate_camera_rig(rig);
    check(rig.cameras.size() == 2 && rig.cameras[0].name == "FreeLook" && rig.cameras[1].name == "Aim" && rig.cameras[1].input_action == "combat.ranged" && rig.cameras[1].facing == CameraFacing::Camera, "Built-in rig is not FreeLook plus Aim");
    // Authoring validation.
    { auto bad = rig; bad.cameras[1].name = "FreeLook"; rejects([&] { validate_camera_rig(bad); }); }
    { auto bad = rig; bad.cameras[0].input_action = "jump"; rejects([&] { validate_camera_rig(bad); }); } // no default camera left
    { auto bad = rig; bad.cameras[0].fov_degrees = 170; rejects([&] { validate_camera_rig(bad); }); }
    { auto bad = rig; bad.cameras[0].pitch_min_degrees = 70; rejects([&] { validate_camera_rig(bad); }); }
    { auto bad = rig; bad.cameras[0].distance = std::nan(""); rejects([&] { validate_camera_rig(bad); }); }
    { CameraRigDefinition empty; rejects([&] { validate_camera_rig(empty); }); }

    // Selection: conditions gate eligibility, priority decides, the default is the fallback.
    CameraDirector director(rig, 0, 0);
    std::vector<std::string> none, ranged{"combat.ranged"}, other{"jump"};
    check(director.select(none, {}) == 0 && director.select(other, {}) == 0 && director.select(ranged, {}) == 1, "Held input did not select the aim camera");
    auto tagged = rig; tagged.cameras[1].input_action.clear(); tagged.cameras[1].tag = 7;
    VirtualCameraDefinition dialogue = rig.cameras[0]; dialogue.name = "Dialogue"; dialogue.priority = 50; dialogue.tag = 9; dialogue.track_look_target = true; tagged.cameras.push_back(dialogue);
    CameraDirector by_tag(tagged, 0, 0);
    std::vector<TagId> aim_tag{7}, both{7, 9}, unrelated{3};
    check(by_tag.select(none, unrelated) == 0 && by_tag.select(none, aim_tag) == 1 && by_tag.select(none, both) == 2, "Tag conditions or priority order are wrong");

    // Orbit geometry: the camera sits behind the pivot at its distance and looks at it.
    CameraTargets targets{{10, 2, -3}, std::nullopt};
    auto pose = director.solve(0, targets);
    const CameraVec pivot{10, 2 + rig.cameras[0].height, -3};
    check(std::abs(distance(pose.position, pivot) - rig.cameras[0].distance) < 1e-9 && distance(pose.target, pivot) < 1e-9 && pose.position[2] < pivot[2] && std::abs(pose.position[0] - pivot[0]) < 1e-9 && pose.fov_degrees == rig.cameras[0].fov_degrees, "Free-look pose is not behind the pivot at its distance");
    director.orbit(pi / 2, 0);
    pose = director.solve(0, targets);
    check(pose.position[0] < pivot[0] - 4 && std::abs(pose.position[2] - pivot[2]) < 1e-9, "Yaw did not orbit the camera around the pivot");
    director.orbit(-pi / 2, 0);
    director.orbit(0, 10);
    check(std::abs(director.pitch() - rig.cameras[0].pitch_max_degrees * pi / 180) < 1e-9, "Pitch was not clamped to the camera's range");
    pose = director.solve(0, targets);
    check(pose.position[1] > pivot[1] + 3, "Positive pitch did not raise the camera to look down");
    director.orbit(0, -20);
    check(std::abs(director.pitch() - rig.cameras[0].pitch_min_degrees * pi / 180) < 1e-9, "Pitch floor not applied");
    director.orbit(0, -director.pitch());
    // Shoulder offset moves camera and aim point to the actor's right (+x is left when facing +z).
    auto aim = director.solve(1, targets);
    check(aim.position[0] < pivot[0] - .5 && aim.target[0] < pivot[0] - .5 && std::abs(aim.position[0] - aim.target[0]) < 1e-9 && distance(aim.position, aim.target) < rig.cameras[0].distance, "Aim camera is not closer and over the right shoulder");
    rejects([&] { director.solve(5, targets); });
    rejects([&] { director.orbit(std::nan(""), 0); });

    // Blending: a change eases from the previous output and completes in the blend time.
    CameraDirector blending(rig, 0, 0);
    auto start = blending.update(1. / 60, targets, none, {});
    check(blending.live() == 0 && blending.blend() == 1 && distance(start.position, director.solve(0, targets).position) < 1e-6, "First update did not start on the default camera");
    auto first = blending.update(1. / 60, targets, ranged, {});
    check(blending.live() == 1 && blending.blend() > 0 && blending.blend() < 1 && distance(first.position, start.position) < distance(aim.position, start.position), "Camera change did not begin a blend from the previous pose");
    double previous = distance(first.position, aim.position), fov = first.fov_degrees;
    for (unsigned frame = 0; frame < 60; ++frame) {
        auto next = blending.update(1. / 60, targets, ranged, {});
        const double remaining = distance(next.position, aim.position);
        check(remaining <= previous + 1e-9 && next.fov_degrees <= fov + 1e-9, "Blend did not approach the live camera monotonically");
        previous = remaining; fov = next.fov_degrees;
    }
    check(blending.blend() == 1 && previous < 1e-3 && std::abs(fov - rig.cameras[1].fov_degrees) < 1e-9, "Blend did not finish on the aim camera");
    blending.update(1. / 60, targets, none, {});
    check(blending.live() == 0 && blending.blend() < 1, "Releasing the input did not blend back to free-look");

    // Follow smoothing converges; a teleport snaps instead of sweeping across the level.
    CameraDirector following(rig, 0, 0);
    following.update(1. / 60, targets, none, {});
    CameraTargets moved{{11, 2, -3}, std::nullopt};
    auto lagging = following.update(1. / 60, moved, none, {});
    check(lagging.target[0] > pivot[0] && lagging.target[0] < pivot[0] + 1, "Follow smoothing did not lag behind a moving target");
    for (unsigned frame = 0; frame < 120; ++frame) lagging = following.update(1. / 60, moved, none, {});
    check(std::abs(lagging.target[0] - (pivot[0] + 1)) < 1e-3, "Follow smoothing did not converge");
    CameraTargets far{{500, 2, -3}, std::nullopt};
    auto snapped = following.update(1. / 60, far, none, {});
    check(std::abs(snapped.target[0] - 500) < 1e-9, "A large jump was smoothed instead of snapped");

    // A supplied look target frames the follow target toward it on tracking cameras only.
    CameraTargets looking{{0, 0, 0}, CameraVec{0, 1.5, 8}};
    by_tag.update(1. / 60, looking, none, both);
    auto framed = by_tag.solve(2, looking), free = by_tag.solve(0, looking);
    check(framed.target == *looking.look && framed.position[2] < 0 && std::abs(framed.position[0]) < 1e-6 && free.target != *looking.look, "Look target tracking is wrong or leaked to a non-tracking camera");

    // Shake displaces the camera only, decays to nothing and is bounded.
    CameraDirector shaking(rig, 0, 0);
    auto steady = shaking.update(1. / 60, targets, none, {});
    shaking.shake({.2, 20, .3});
    double peak = 0;
    for (unsigned frame = 0; frame < 18; ++frame) { auto shaken = shaking.update(1. / 60, targets, none, {}); peak = std::max(peak, distance(shaken.position, steady.position)); check(distance(shaken.target, steady.target) < 1e-9, "Shake moved the look target"); }
    auto settled = shaking.update(1. / 60, targets, none, {});
    check(peak > .01 && peak < .5 && distance(settled.position, steady.position) < 1e-9, "Shake did not displace, stay bounded or decay");
    for (unsigned count = 0; count < 8; ++count) shaking.shake({.05, 10, 1});
    rejects([&] { shaking.shake({.05, 10, 1}); });
    rejects([&] { shaking.shake({-1, 10, 1}); });
    rejects([&] { shaking.update(-1, targets, none, {}); });
    std::cout << "Camera rig: validation, held-input and tag selection by priority, orbit geometry and pitch clamps, over-shoulder aim offset, monotonic blend in and back, follow smoothing with teleport snap, look-target framing, bounded decaying shake passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
