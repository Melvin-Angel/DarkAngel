#include "scene_camera.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace darkangel::editor_app {
namespace {
constexpr double pi = std::numbers::pi;
CameraPoint add(CameraPoint a, CameraPoint b) { return {a[0]+b[0], a[1]+b[1], a[2]+b[2]}; }
CameraPoint scale(CameraPoint a, double s) { return {a[0]*s, a[1]*s, a[2]*s}; }
CameraPoint subtract(CameraPoint a, CameraPoint b) { return add(a, scale(b, -1)); }
double length(CameraPoint a) { return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); }
CameraPoint cross(CameraPoint a, CameraPoint b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
CameraPoint rotate_axis(CameraPoint p, CameraPoint axis, double angle) {
    const double dot = p[0]*axis[0]+p[1]*axis[1]+p[2]*axis[2];
    return add(add(scale(p, std::cos(angle)), scale(cross(axis,p), std::sin(angle))), scale(axis,dot*(1-std::cos(angle))));
}
} // namespace
SceneCamera::SceneCamera() { look_from(eye,pivot); }
CameraPoint SceneCamera::forward() const { return {std::sin(yaw)*std::cos(pitch), std::sin(pitch), -std::cos(yaw)*std::cos(pitch)}; }
CameraPoint SceneCamera::right() const { return {std::cos(yaw),0,std::sin(yaw)}; }
CameraPoint SceneCamera::up() const { return cross(right(),forward()); }
void SceneCamera::look_from(CameraPoint position, CameraPoint target) {
    const auto delta = subtract(target,position);
    const double distance = length(delta);
    if (distance < .001) return;
    eye=position; pivot=target;
    yaw=std::atan2(delta[0],-delta[2]);
    pitch=std::clamp(std::asin(std::clamp(delta[1]/distance,-1.,1.)),-pi*.495,pi*.495);
}
void SceneCamera::set_mode(Mode next, std::optional<SceneBounds> selection) {
    mode=next;
    // Only the orbit anchor changes: switching mode never rotates or translates the view.
    if (next==Mode::Orbit && selection) pivot=selection->center;
}
void SceneCamera::rotate(double dx, double dy) {
    const double yaw_delta=dx*.004;
    const double next_pitch=std::clamp(pitch-dy*.004,-pi*.495,pi*.495);
    const double pitch_delta=next_pitch-pitch;
    auto offset=subtract(eye,pivot);
    offset=rotate_axis(offset,{0,1,0},-yaw_delta);
    yaw=std::remainder(yaw+yaw_delta,2*pi);
    if (mode==Mode::Orbit) {
        offset=rotate_axis(offset,right(),pitch_delta);
        eye=add(pivot,offset);
    }
    pitch=next_pitch;
    if (mode==Mode::Fly) pivot=add(eye,scale(forward(),std::max(.1,length(offset))));
}
void SceneCamera::pan(double dx, double dy, double height) {
    const double metres=2*std::max(.1,length(subtract(eye,pivot)))*std::tan(pi/8)/std::max(1.,height);
    const auto delta=add(scale(right(),-dx*metres),scale(up(),dy*metres));
    eye=add(eye,delta);pivot=add(pivot,delta);
}
void SceneCamera::wheel(double steps, bool change_speed) {
    if (change_speed) { speed=std::clamp(speed*std::pow(1.25,std::clamp(steps,-100.,100.)),min_speed,max_speed);return; }
    if (mode==Mode::Orbit) {
        auto offset=subtract(eye,pivot);const double distance=length(offset);
        if (distance>.0001) eye=add(pivot,scale(offset,std::clamp(distance*std::exp(-steps*.15),.05,100000.)/distance));
    } else {
        const auto delta=scale(forward(),steps*speed*.3);
        eye=add(eye,delta);pivot=add(pivot,delta);
    }
}
void SceneCamera::move(double lateral, double vertical, double advance, double seconds, bool fast) {
    const auto delta=add(add(scale(right(),lateral),CameraPoint{0,vertical,0}),scale(forward(),advance));
    const auto step=scale(delta,speed*(fast?4:1)*std::clamp(seconds,0.,.1)/std::max(1.,length(delta)));
    eye=add(eye,step);pivot=add(pivot,step);
}
void SceneCamera::focus(const SceneBounds& bounds, double aspect) {
    const double angle=std::atan(std::tan(pi/8)*std::min(1.,std::max(.01,aspect)));
    const double distance=std::max(.1,bounds.radius)*1.1/std::sin(angle);
    pivot=bounds.center;eye=add(pivot,scale(forward(),-distance));
}
void SceneNavigation::update(const SceneNavigationInput& in, std::optional<SceneBounds> selection) {
    if (!in.enabled||in.text||in.popup||in.focus_lost||!in.focused||in.escape) { cancel();return; }
    if (in.toggle_mode&&!active()&&!in.blocked) camera.set_mode(camera.mode==SceneCamera::Mode::Orbit?SceneCamera::Mode::Fly:SceneCamera::Mode::Orbit,selection);
    if (in.focus_selection&&!active()&&!in.blocked&&selection) camera.focus(*selection,in.aspect);
    if (drag==Drag::Pan&&!in.middle) cancel();
    if (drag==Drag::Look&&!in.right) cancel();
    if (drag==Drag::Orbit&&!in.right&&!(in.left&&in.alt)) cancel();
    bool started=false;
    if (!active()&&in.hovered&&!in.blocked&&!in.over_gizmo) {
        if (in.middle_pressed) drag=Drag::Pan;
        else if ((in.left_pressed&&in.alt)||(in.right_pressed&&camera.mode==SceneCamera::Mode::Orbit)) drag=Drag::Orbit;
        else if (in.right_pressed) drag=Drag::Look;
        started=active();
        if (drag==Drag::Orbit&&selection) camera.pivot=selection->center;
    }
    // Don't apply the delta from before the press (or from crossing into the viewport).
    if (!started) {
        if (drag==Drag::Pan) camera.pan(in.dx,in.dy,in.height);
        else if (drag==Drag::Look||drag==Drag::Orbit) {
            const auto mode=camera.mode;
            if (drag==Drag::Orbit) camera.mode=SceneCamera::Mode::Orbit;
            camera.rotate(in.dx,in.dy);
            camera.mode=mode;
        }
    }
    if (drag==Drag::Look) camera.move(in.lateral,in.vertical,in.forward,in.seconds,in.shift);
    if ((in.hovered||active())&&!in.blocked&&in.wheel) camera.wheel(in.wheel,drag==Drag::Look);
}
} // namespace darkangel::editor_app
