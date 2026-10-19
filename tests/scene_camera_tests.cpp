#include "scene_camera.hpp"
#include "game_resolution.hpp"
#include <darkangel/check.hpp>
#include <cmath>
#include <iostream>

using namespace darkangel::editor_app;
using darkangel::require;
namespace {
double distance(CameraPoint a,CameraPoint b) {
    double sum=0;for(unsigned i=0;i<3;++i) sum+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(sum);
}
void unchanged(const SceneCamera& a,const SceneCamera& b) {
    require(distance(a.eye,b.eye)<1e-9&&distance(a.forward(),b.forward())<1e-9,"Unexpected editor view change");
}
SceneNavigationInput ready() {
    SceneNavigationInput i;i.enabled=i.hovered=i.focused=true;i.seconds=.1;return i;
}
}
int main() {
    try {
        for(unsigned preset=0;preset<game_resolutions.size();++preset){
            const auto scene=viewport_render_size(false,preset,777,431);
            require(scene.width==777&&scene.height==431,"Game preset changed Scene render size");
            const auto game=viewport_render_size(true,preset,777,431);
            require(game.width==(preset?game_resolutions[preset].size.width:777)&&
                    game.height==(preset?game_resolutions[preset].size.height:431),"Incorrect Game render size");
        }
        const auto invalid=viewport_render_size(true,999,0,-10);
        require(invalid.width==1&&invalid.height==1,"Invalid viewport size fallback");
        SceneCamera c;
        const auto initial=c;
        const SceneBounds selected{{13,2,-8},3};
        c.set_mode(SceneCamera::Mode::Fly,selected);unchanged(c,initial);
        c.set_mode(SceneCamera::Mode::Orbit,selected);unchanged(c,initial);
        require(c.pivot==selected.center,"Orbit did not choose selected object's centre");
        const double radius=distance(c.eye,c.pivot);
        c.rotate(130,-60);
        require(std::abs(distance(c.eye,c.pivot)-radius)<1e-8,"Orbit changed pivot distance");
        c.focus(selected,.3);
        const auto delta=CameraPoint{selected.center[0]-c.eye[0],selected.center[1]-c.eye[1],selected.center[2]-c.eye[2]};
        const auto forward=c.forward();const double length=distance(c.eye,selected.center);
        require(distance({delta[0]/length,delta[1]/length,delta[2]/length},forward)<1e-9,"Focus does not centre the actual bounds");
        require(length>selected.radius/std::sin(std::atan(std::tan(std::acos(-1.)/8)*.3)),"Focus clipped bounds in portrait view");
        const auto before_pan=c;
        c.pan(15,20,500);
        require(distance(before_pan.eye,c.eye)>.1&&std::abs(distance(c.eye,c.pivot)-length)<1e-8,"Pan did not translate eye and pivot together");
        c.wheel(4,false);require(distance(c.eye,c.pivot)<length,"Orbit zoom moved out");
        c.wheel(10000,false);require(distance(c.eye,c.pivot)>=.04999,"Zoom crossed the pivot");
        for(int i=0;i<100;++i) c.wheel(100,true);
        require(c.speed==SceneCamera::max_speed,"Fly speed exceeded its upper bound");
        for(int i=0;i<100;++i) c.wheel(-100,true);
        require(c.speed==SceneCamera::min_speed,"Fly speed exceeded its lower bound");
        c=initial;c.mode=SceneCamera::Mode::Fly;c.rotate(50,100000);
        require(distance(c.eye,initial.eye)<1e-9&&std::abs(c.pitch)<std::acos(-1.)/2,"Fly rotation translated eye or flipped camera");
        auto diagonal=c,straight=c;diagonal.move(1,0,1,.1,false);straight.move(0,0,1,.1,false);
        require(std::abs(distance(diagonal.eye,c.eye)-distance(straight.eye,c.eye))<1e-9,"Diagonal fly movement is faster");
        auto fast=c;fast.move(0,0,1,.1,true);
        require(std::abs(distance(fast.eye,c.eye)-4*distance(straight.eye,c.eye))<1e-9,"Shift fly multiplier failed");
        SceneNavigation n;auto i=ready();i.right=i.right_pressed=true;n.update(i,selected);
        require(n.drag==SceneNavigation::Drag::Orbit,"Orbit drag did not start");
        i.right_pressed=false;i.hovered=false;i.dx=40;n.update(i,selected);
        require(distance(n.camera.eye,initial.eye)>.1,"Drag did not continue outside viewport");
        i.right=false;n.update(i,selected);require(!n.active(),"Release outside viewport left navigation captured");
        const auto released=n.camera;i.dx=100;i.right=true;n.update(i,selected);unchanged(n.camera,released);
        require(!n.active(),"Holding mouse from another surface began navigation");
        for(unsigned guard=0;guard<6;++guard) {
            SceneNavigation guarded;auto in=ready();in.right=in.right_pressed=true;in.dx=40;in.focus_selection=true;
            if(guard==0)in.text=true;if(guard==1)in.popup=true;if(guard==2)in.focus_lost=true;
            if(guard==3)in.focused=false;if(guard==4)in.enabled=false;if(guard==5)in.blocked=true;
            guarded.update(in,selected);unchanged(guarded.camera,initial);require(!guarded.active(),"Input ownership guard failed");
        }
        SceneNavigation over;auto hover=ready();hover.over_gizmo=true;hover.right=hover.right_pressed=true;
        over.update(hover,selected);require(!over.active(),"Gizmo hover allowed a camera drag");
        hover.right=hover.right_pressed=false;hover.focus_selection=true;over.update(hover,selected);
        require(over.camera.pivot==selected.center,"Gizmo hover incorrectly blocked focus selection");
        i=ready();i.focus_selection=true;n.update(i,{});unchanged(n.camera,released);
        i=ready();i.toggle_mode=true;n.update(i,selected);require(n.camera.mode==SceneCamera::Mode::Fly,"Mode shortcut failed");
        i=ready();i.right=i.right_pressed=true;n.update(i,selected);
        i.right_pressed=false;i.wheel=2;i.forward=1;n.update(i,selected);require(n.camera.speed>5,"Fly wheel did not adjust speed");
        i.focus_lost=true;n.update(i,selected);require(!n.active(),"Focus loss failed to cancel capture");
        std::cout<<"Scene camera: bounds focus, orbit/fly continuity, pan/zoom, speed, normalized movement and input ownership pass\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
