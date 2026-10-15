#include "scene_camera_exercise.hpp"
#include "../editor_ui.hpp"
#include <darkangel/check.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace darkangel::editor_app::exercises {
namespace {
double distance(CameraPoint a,CameraPoint b) {
    double sum=0;for(unsigned axis=0;axis<3;++axis) sum+=(a[axis]-b[axis])*(a[axis]-b[axis]);return std::sqrt(sum);
}
class CameraExercise final:public Exercise {
    bool enabled_,done_{};
    Controller& editor;
    Shell& shell;
    StableId target;
    SceneCamera before;
    std::uint64_t revision{};
    std::size_t objects{};
    std::string item;
    double orbit_radius{};
    std::filesystem::path path;
    ImVec2 at(const std::string& name) const {
        const auto found=shell.probes.find(name);
        require(found!=shell.probes.end(),"Scene camera exercise: missing control "+name);
        return found->second;
    }
    ImVec2 point(float x=.2f,float y=.3f) const {
        const auto origin=at("viewport-origin"),drop=at("viewport-drop");
        return {origin.x+(drop.x-origin.x)*2*x,origin.y+(drop.y-origin.y)/.62f*y};
    }
    void move(ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x,p.y); }
    void button(int which,bool down) { ImGui::GetIO().AddMouseButtonEvent(which,down); }
    void key(ImGuiKey which,bool down) { ImGui::GetIO().AddKeyEvent(which,down); }
    void same_view() const {
        const auto& camera=shell.scene_navigation.camera;
        require(distance(camera.eye,before.eye)<1e-6&&distance(camera.forward(),before.forward())<1e-6,
                "Scene camera exercise: view changed without navigation ownership");
    }
  public:
    CameraExercise(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s) {}
    void frame(unsigned frame) override {
        if (!enabled_) return;
        auto& camera=shell.scene_navigation.camera;
        auto& io=ImGui::GetIO();
        if (frame==2) {
            shell.refresh_place_items(editor);
            const auto npc=std::find_if(shell.place_items.begin(),shell.place_items.end(),[](const auto& i){return i.npc;});
            require(npc!=shell.place_items.end(),"Scene camera exercise needs an NPC Place entry");
            item="place:"+npc->name;
            target=shell.place(editor,*npc,80,80,0,12);
            objects=editor.document->plan().origins.size();revision=editor.document->revision();
            move(at("scene-camera-focus"));
        }
        if(frame==3)button(0,true);
        if(frame==4)button(0,false);
        if(frame==6) {
            const auto bounds=shell.scene_selection_bounds(editor);
            require(bounds&&distance(camera.pivot,bounds->center)<1e-5,"Focus button did not use selected bounds");
            before=camera;orbit_radius=distance(camera.eye,camera.pivot);move(point());
        }
        if(frame==7)button(1,true);
        if(frame==8)move(point(.27f,.36f));
        if(frame==9)move(point(.31f,.39f));
        if(frame==10)button(1,false);
        if(frame==12) {
            require(distance(camera.eye,before.eye)>.05&&std::abs(distance(camera.eye,camera.pivot)-orbit_radius)<1e-4,"Orbit pointer input failed");
            before=camera;button(2,true);
        }
        if(frame==13)move(point(.35f,.44f));
        if(frame==14)move({at("viewport-origin").x-8,at("viewport-origin").y+60});
        if(frame==15)button(2,false);
        if(frame==18) {
            require(!shell.scene_navigation.active()&&distance(camera.eye,before.eye)>.05,"Pan/release outside viewport failed");
            orbit_radius=distance(camera.eye,camera.pivot);move(point());
        }
        if(frame==19)io.AddMouseWheelEvent(0,1);
        if(frame==21) {
            require(distance(camera.eye,camera.pivot)<orbit_radius,"Viewport wheel zoom failed");
            before=camera;move(at("scene-camera-mode"));
        }
        if(frame==22)button(0,true);
        if(frame==23)button(0,false);
        if(frame==25) {
            require(camera.mode==SceneCamera::Mode::Fly,"Mode toolbar did not select Fly");same_view();move(point());
        }
        if(frame==26)button(1,true);
        if(frame==27)key(ImGuiKey_W,true);
        if(frame==28)move(point(.26f,.25f));
        if(frame==30)io.AddMouseWheelEvent(0,2);
        if(frame==33) {key(ImGuiKey_W,false);button(1,false);}
        if(frame==35) {
            require(distance(camera.eye,before.eye)>.001&&camera.speed>5,"Fly move/look/wheel speed failed");
            require(editor.document->revision()==revision,"Camera navigation edited authored scene data");
            before=camera;move(point());
        }
        if(frame==37) {key(ImGuiMod_Shift,true);key(ImGuiKey_LeftShift,true);key(ImGuiKey_F,true);}
        if(frame==38) {key(ImGuiKey_F,false);key(ImGuiKey_LeftShift,false);key(ImGuiMod_Shift,false);}
        if(frame==40) {require(camera.mode==SceneCamera::Mode::Orbit,"Shift+F mode shortcut failed");same_view();}
        if(frame==42)key(ImGuiKey_F,true);
        if(frame==43)key(ImGuiKey_F,false);
        if(frame==45) {
            const auto bounds=shell.scene_selection_bounds(editor);
            require(bounds&&distance(camera.pivot,bounds->center)<1e-5,"F did not focus selection");
            editor.selected={};
        }
        if(frame==46) {
            using namespace DirectX;
            const auto bounds=shell.scene_object_bounds(target);
            require(bool(bounds),"Missing target bounds");
            XMFLOAT4 projected;const auto& center=bounds->center;
            XMStoreFloat4(&projected,XMVector4Transform(XMVectorSet(float(center[0]),float(center[1]),float(center[2]),1),
                XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection)));
            require(projected.w>0,"Focus put selection behind camera");
            move(point(.5f+projected.x/projected.w*.5f,.5f-projected.y/projected.w*.5f));
        }
        if(frame==47)button(0,true);
        if(frame==48)button(0,false);
        if(frame==50) {
            require(editor.selected==target,"Moved-camera viewport picking did not select target");
            editor.selected={};before=camera;
        }
        if(frame==52)key(ImGuiKey_F,true);
        if(frame==53)key(ImGuiKey_F,false);
        if(frame==55) {same_view();move(at("outliner-filter"));}
        if(frame==56)button(0,true);
        if(frame==57)button(0,false);
        if(frame==60) {key(ImGuiKey_F,true);key(ImGuiKey_W,true);io.AddInputCharactersUTF8("F");}
        if(frame==62) {key(ImGuiKey_F,false);key(ImGuiKey_W,false);}
        if(frame==64) {same_view();require(editor.document->revision()==revision,"Focus/text input changed source");key(ImGuiKey_Escape,true);}
        if(frame==65)key(ImGuiKey_Escape,false);
        // A real Place-panel drag after the camera moved; no direct source edit for the placement.
        if(frame==69)move(at(item));
        if(frame==70)button(0,true);
        if(frame==71) {auto p=at(item);move({p.x+14,p.y+4});}
        if(frame==72)move(at("viewport-drop"));
        if(frame==73) {auto p=at("viewport-drop");move({p.x+3,p.y});}
        if(frame==74)button(0,false);
        if(frame==78) {
            require(editor.document->plan().origins.size()==objects+1&&editor.selected!=target&&editor.document->plan().spawns.contains(editor.selected),
                    "Moved-camera Place drag failed");
            const auto p=editor.document->world().read(editor.document->world().find(editor.selected)).transform;
            require(std::abs(p.y-shell.surface_height(p.x,p.z))<1e-3,"Moved-camera placement missed surface under cursor");
            before=camera;move(point());
        }
        if(frame==80) {editor.play();io.AddMouseWheelEvent(0,2);}
        if(frame==81)button(1,true);
        if(frame==82)move(point(.35f,.4f));
        if(frame==83)key(ImGuiKey_W,true);
        if(frame==86) {key(ImGuiKey_W,false);button(1,false);}
        if(frame==90) {same_view();require(!shell.scene_navigation.active(),"Play retained scene navigation capture");editor.stop();}
        if(frame==94) {
            same_view();path=editor.path;require(!path.empty(),"Scene camera exercise needs disposable scene path");
            editor.save(path);before=camera;
        }
        if(frame==96)editor.demo();
        if(frame==99) {require(distance(camera.eye,before.eye)>.1,"New scene inherited the prior scene camera");editor.open(path);}
        if(frame==103) {same_view();require(!shell.scene_navigation.active(),"Reopened scene retained capture");done_=true;
            std::cout<<"Editor scene_camera orbit=1 fly=1 pan=1 zoom=1 speed=1 focus=1 mode_continuity=1 picking=1 placement=1 text_isolation=1 empty_focus=1 play_isolation=1 per_scene=1\n";}
    }
    void finish() override {require(!enabled_||done_,"Scene camera exercise ended before acceptance");}
    bool done() const override{return !enabled_||done_;}
    std::string_view result() const override{return {};}
};
}
std::unique_ptr<Exercise> make_scene_camera(bool enabled,Controller& editor,Shell& shell) {
    return std::make_unique<CameraExercise>(enabled,editor,shell);
}
}
