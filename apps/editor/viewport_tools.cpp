#include "viewport_tools.hpp"
#include "editor_controller.hpp"
#include "gameplay_inspector.hpp"
#include "scene_splines.hpp"
#include "character_preview.hpp"
#include "render/scene_resources.hpp"
#ifdef DAE_NAVIGATION
#include "navigation_tool.hpp"
#endif
#include <darkangel/character_scene.hpp>
#include <darkangel/encounter.hpp>
#include <darkangel/world_objects.hpp>
#include <DirectXMath.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace darkangel::editor_app {
ViewportTools::ViewportTools(ViewportToolsContext context)
    :editor(context.editor),shell(context.shell),scene_(context.scene),character(context.session),character_resources(context.resources),navigation_(context.navigation),
     senses_shown(context.debug.senses_shown),senses_all(context.debug.senses_all),world_debug(context.debug.world_debug){}
ViewportTools::~ViewportTools()=default;
void ViewportTools::install(){
    shell.pick_object=[this](const std::array<double,3>& from,const std::array<double,3>& to){return pick(from,to);};
    shell.place_surface=[this](const std::array<double,3>& from,const std::array<double,3>& to){return place(from,to);};
    shell.debug_overlay=[this](ImDrawList* draw,darkangel::editor_app::ViewArea area){draw_overlay(draw,area);};
}
// A model uses its cooked bounds under the object's transform; a character or a spawn point uses a standing body's box.
StableId ViewportTools::pick(const std::array<double,3>& from,const std::array<double,3>& to){
        const auto id=scene_.model();const auto& gpu=scene_.gpu;const auto& scene_models=scene_.scene_models;
        using namespace DirectX;StableId best;float nearest=2;
        auto test=[&](StableId object,const std::array<float,3>& center,const std::array<float,3>& half){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return;const auto pose=editor.displayed().read(entity).transform;
            const auto inverse=XMMatrixInverse(nullptr,XMMatrixScaling(float(pose.scale),float(pose.scale),float(pose.scale))*XMMatrixRotationRollPitchYaw(float(pose.pitch),float(pose.yaw),float(pose.roll))*XMMatrixTranslation(float(pose.x),float(pose.y),float(pose.z)));
            XMFLOAT3 a,b;XMStoreFloat3(&a,XMVector3TransformCoord(XMVectorSet(float(from[0]),float(from[1]),float(from[2]),1),inverse));XMStoreFloat3(&b,XMVector3TransformCoord(XMVectorSet(float(to[0]),float(to[1]),float(to[2]),1),inverse));
            const float start[3]={a.x,a.y,a.z},delta[3]={b.x-a.x,b.y-a.y,b.z-a.z};float enter=0,leave=1;
            for(unsigned axis=0;axis<3;++axis){const float reach=std::max(half[axis],.05f),low=center[axis]-reach,high=center[axis]+reach;if(std::abs(delta[axis])<1e-9f){if(start[axis]<low||start[axis]>high)return;continue;}float first=(low-start[axis])/delta[axis],second=(high-start[axis])/delta[axis];if(first>second)std::swap(first,second);enter=std::max(enter,first);leave=std::min(leave,second);if(enter>leave)return;}
            if(enter<nearest){nearest=enter;best=object;}};
        const std::array<float,3> body_center{0,.9f,0},body_half{.4f,.9f,.4f};
        for(const auto& [object,model]:editor.document->plan().models){if(model==id)test(object,gpu.center,gpu.half);else if(auto found=scene_models.find(model);found!=scene_models.end())test(object,found->second.center,found->second.half);else test(object,body_center,body_half);}
        for(const auto& [object,npc]:editor.document->plan().spawns)test(object,body_center,body_half);
        // A volume is picked only where no solid object smaller than it is under the cursor, so things inside a volume stay clickable.
        const StableId solid=best;const float solid_at=nearest;float solid_size=1e9f;if(solid){solid_size=1.f;if(auto model=editor.document->plan().models.find(solid);model!=editor.document->plan().models.end()){if(model->second==id)solid_size=gpu.radius;else if(auto found=scene_models.find(model->second);found!=scene_models.end())solid_size=found->second.radius;}auto entity=editor.displayed().find(solid);if(editor.displayed().valid(entity))solid_size*=float(editor.displayed().read(entity).transform.scale);}
        StableId volume;float volume_size=1e9f;auto offer=[&](StableId object,const std::array<float,3>& half){best={};nearest=2;test(object,{0,0,0},half);const float size=std::max({half[0],half[1],half[2]});if(best&&size<volume_size){volume=best;volume_size=size;}};
        for(const auto& [object,info]:editor.document->plan().volumes)offer(object,{float(info.half[0]),float(info.half[1]),float(info.half[2])});
        for(const auto& [object,json]:editor.document->plan().interactables)if(!editor.document->plan().models.contains(object))offer(object,{.4f,.4f,.4f});
        for(const auto& [object,info]:editor.document->plan().spline_points)offer(object,{.3f,.3f,.3f});for(const auto& [object,json]:editor.document->plan().splines)offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().triggers){try{const auto trigger=darkangel::decode_trigger(json);offer(object,{float(trigger.half.x),float(trigger.half.y),float(trigger.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().transitions){try{const auto transition=darkangel::decode_transition(json);offer(object,{float(transition.half.x),float(transition.half.y),float(transition.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().entries)offer(object,{.4f,.4f,.4f});
        for(const auto& [object,json]:editor.document->plan().pickups)if(!editor.document->plan().models.contains(object))offer(object,{.3f,.3f,.3f});
        for(const auto& [object,json]:editor.document->plan().altars)if(!editor.document->plan().models.contains(object))offer(object,{.5f,.5f,.5f});
        for(const auto& [object,json]:editor.document->plan().movers){try{const auto mover=darkangel::decode_mover(json);test(object,{0,float(mover.half.y),0},{float(mover.half.x),float(mover.half.y),float(mover.half.z)});}catch(const std::exception&){}}
        for(const auto& [object,json]:editor.document->plan().encounters){best={};nearest=2;test(object,{0,.5f,0},{.4f,.4f,.4f});if(best){volume=best;volume_size=.4f;}}
        best=solid;nearest=solid_at;if(volume&&(!solid||solid_size>volume_size))best=volume;
        return best;
}
// Rays against the scene's cooked static collision, in a query world built when the collision changes.
std::optional<std::array<double,3>> ViewportTools::place(const std::array<double,3>& from,const std::array<double,3>& to){
        if(!character_resources||!character_resources->collision)return std::nullopt;
        if(place_collision!=character_resources->collision.get()){place_world=std::make_unique<darkangel::PhysicsWorld>();place_world->load_scene(*character_resources->collision);place_collision=character_resources->collision.get();}
        const auto found=place_world->query_ray({from[0],from[1],from[2]},{to[0]-from[0],to[1]-from[1],to[2]-from[2]},{darkangel::StaticCollision,0});const darkangel::CollisionHit* nearest=nullptr;for(const auto& hit:found.hits)if(!nearest||hit.fraction<nearest->fraction)nearest=&hit;
        if(!nearest)return std::nullopt;return std::array<double,3>{nearest->point.x,nearest->point.y,nearest->point.z};
}
// Encounter volumes, trigger volumes and markers always while authoring and on request in Play; the senses of the
// selected spawn point's NPC from its draft while authoring, and of the actors the Gameplay Inspector ticks in Play.
void ViewportTools::draw_overlay(ImDrawList* draw,ViewArea area){
#ifdef DAE_NAVIGATION
        auto& navigation=*navigation_;
#endif
        using namespace DirectX;using darkangel::MotorVec;senses_drawn=0;volumes_drawn=0;splines_drawn=0;if(!shell.scene_camera||!draw)return;const auto matrix=XMLoadFloat4x4(&shell.scene_view)*XMLoadFloat4x4(&shell.scene_projection);
        auto project=[&](const MotorVec& point,ImVec2& out){XMFLOAT4 clip;XMStoreFloat4(&clip,XMVector4Transform(XMVectorSet(float(point.x),float(point.y),float(point.z),1),matrix));if(clip.w<=.05f)return false;out={area.x+(clip.x/clip.w*.5f+.5f)*area.width,area.y+(.5f-clip.y/clip.w*.5f)*area.height};return true;};
        auto line=[&](const MotorVec& a,const MotorVec& b,ImU32 colour,float width=1.5f){ImVec2 p,q;if(project(a,p)&&project(b,q))draw->AddLine(p,q,colour,width);};
#ifdef DAE_NAVIGATION
        // The baked navigation mesh: polygon edges a little above the surface, the border brighter.
        navigation_drawn=0;if(navigation.shown&&navigation.mesh)for(const auto& edge:navigation.outline){ImVec2 p,q;if(project({edge.from.x,edge.from.y+.05,edge.from.z},p)&&project({edge.to.x,edge.to.y+.05,edge.to.z},q)){draw->AddLine(p,q,edge.border?IM_COL32(80,220,255,255):IM_COL32(80,220,255,110),edge.border?2.f:1.f);++navigation_drawn;}}
#endif
        auto ring=[&](const MotorVec& centre,double radius,ImU32 colour){constexpr unsigned steps=48;for(unsigned i=0;i<steps;++i){const double a=6.283185307179586*i/steps,b=6.283185307179586*(i+1)/steps;line({centre.x+std::sin(a)*radius,centre.y,centre.z+std::cos(a)*radius},{centre.x+std::sin(b)*radius,centre.y,centre.z+std::cos(b)*radius},colour);}};
        auto shape=[&](const darkangel::SensesDefinition& senses,const MotorVec& foot,double yaw,bool alert){
            ++senses_drawn;const MotorVec eye{foot.x,foot.y+senses.eye_height,foot.z};const ImU32 vision=alert?IM_COL32(255,80,60,255):IM_COL32(90,230,120,255);constexpr double radians=3.141592653589793/180;
            auto toward=[&](double aside,double above,double range){const double a=yaw+aside,c=std::cos(above);return MotorVec{eye.x+std::sin(a)*c*range,eye.y+std::sin(above)*range,eye.z+std::cos(a)*c*range};};
            for(const auto& sensor:senses.sensors){
                if(sensor.kind==darkangel::SensorKind::Distance)ring({foot.x,foot.y+.05,foot.z},sensor.range,IM_COL32(255,200,60,255));
                else if(sensor.kind==darkangel::SensorKind::LineOfSight)ring(eye,sensor.range,IM_COL32(80,200,255,255));
                else{const double h=sensor.horizontal*radians,v=sensor.vertical*radians;constexpr unsigned steps=24;
                    // Three arcs at the far end (bottom, level, top), the two side edges, and the facing line.
                    for(double above:{-v,0.,v})for(unsigned i=0;i<steps;++i)line(toward(-h+2*h*i/steps,above,sensor.range),toward(-h+2*h*(i+1)/steps,above,sensor.range),vision,above==0?2.f:1.f);
                    for(double aside:{-h,h}){for(double above:{-v,0.,v})line(eye,toward(aside,above,sensor.range),vision,above==0?2.f:1.f);for(unsigned i=0;i<8;++i)line(toward(aside,-v+2*v*i/8,sensor.range),toward(aside,-v+2*v*(i+1)/8,sensor.range),vision,1.f);}
                    line(eye,toward(0,0,sensor.range),vision,1.f);}
            }
            if(senses.everything){ImVec2 at;if(project({eye.x,eye.y+.5,eye.z},at))draw->AddText(at,IM_COL32(255,255,255,255),"sees everything");}
        };
        auto placed=[&](StableId object,MotorVec& foot,double& yaw){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;const auto transform=editor.displayed().read(entity).transform;foot={transform.x,transform.y,transform.z};yaw=transform.yaw;return true;};
        draw->PushClipRect({area.x,area.y},{area.x+area.width,area.y+area.height},true);
        if(character&&character_resources&&editor.preview.active()){
            for(const auto& spawn:character_resources->spawns){if(!senses_all&&!senses_shown.contains(spawn.actor)&&editor.selected!=spawn.point&&editor.selected!=spawn.actor)continue;const auto* perception=character->perception(spawn.actor);MotorVec foot;double yaw{};if(!perception||!placed(spawn.actor,foot,yaw))continue;
                bool alert=false;const MotorVec eye{foot.x,foot.y+perception->senses().eye_height,foot.z};
                for(const auto& other:perception->actors()){alert|=other.detected;line(eye,{other.motor.position.x,other.motor.position.y+darkangel::perception_body_height,other.motor.position.z},other.detected?(other.visible?IM_COL32(255,60,60,255):IM_COL32(255,160,40,255)):other.sensed?IM_COL32(255,240,90,255):IM_COL32(160,160,160,255),other.detected?2.5f:1.f+float(other.awareness)*1.5f);}
                shape(perception->senses(),foot,yaw,alert);}
        }else if(auto spawn=editor.document->plan().spawns.find(editor.selected);spawn!=editor.document->plan().spawns.end()){
            try{const auto& source=editor.authoring.open(*editor.assets,spawn->second).value;MotorVec foot;double yaw{};if(source.contains("senses")&&placed(spawn->first,foot,yaw))shape(darkangel::decode_npc_senses(source.at("senses").dump()),foot,yaw,false);}catch(const std::exception&){}
        }
        // Encounter volumes, trigger volumes and encounter markers: always while authoring, on request in Play.
        if(!editor.preview.active()||world_debug){const auto& plan=editor.document->plan();auto pose_of=[&](StableId object,darkangel::Transform& out){auto entity=editor.displayed().find(object);if(!editor.displayed().valid(entity))return false;out=editor.displayed().read(entity).transform;return true;};darkangel::Transform at,to;
            auto box=[&](const darkangel::Transform& pose,const std::array<double,3>& half,ImU32 colour,float width){const double c=std::cos(pose.yaw),s=std::sin(pose.yaw);auto corner=[&](int index){const double x=(index&1?1:-1)*half[0]*pose.scale,y=(index&2?1:-1)*half[1]*pose.scale,z=(index&4?1:-1)*half[2]*pose.scale;return MotorVec{pose.x+c*x+s*z,pose.y+y,pose.z-s*x+c*z};};for(int index=0;index<8;++index)for(int bit:{1,2,4})if(!(index&bit))line(corner(index),corner(index|bit),colour,width);};
            StableId focus=editor.selected;if(auto volume=plan.volumes.find(focus);volume!=plan.volumes.end())focus=volume->second.encounter;else if(auto point=plan.spawn_owners.find(focus);point!=plan.spawn_owners.end())focus=point->second;
            for(const auto& [object,volume]:plan.volumes)if(pose_of(object,at)){const bool lit=volume.encounter==focus||object==editor.selected;box(at,volume.half,lit?IM_COL32(120,220,255,255):IM_COL32(70,140,170,170),object==editor.selected?2.5f:lit?1.8f:1.f);++volumes_drawn;if(lit&&pose_of(volume.encounter,to))line({at.x,at.y,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,160),1.f);}
            for(const auto& [point,owner]:plan.spawn_owners)if(owner==focus&&pose_of(point,at)&&pose_of(owner,to))line({at.x,at.y+.9,at.z},{to.x,to.y+.5,to.z},IM_COL32(120,220,255,220),1.5f);
            // A spline: its curve and its points.
            for(const auto& built:darkangel::editor_app::build_splines(editor,shell)){const bool lit=built.id==editor.selected||(plan.spline_points.contains(editor.selected)&&plan.spline_points.at(editor.selected).spline==built.id);const ImU32 colour=IM_COL32(255,120,220,lit?255:190);
                if(built.curve){const auto& path=built.curve->polyline();for(std::size_t piece=0;piece+1<path.size();++piece)line({path[piece].x,path[piece].y+.05,path[piece].z},{path[piece+1].x,path[piece+1].y+.05,path[piece+1].z},colour,lit?2.5f:1.6f);++splines_drawn;}
                for(const auto& point:built.points){darkangel::Transform marker;marker.x=point.x;marker.y=point.y;marker.z=point.z;marker.scale=1;box(marker,{.15,.15,.15},colour,1.2f);}}
            // An interactable: its reach as a ring on the ground and a small marker box.
            for(const auto& [object,json]:plan.interactables)if(pose_of(object,at)){try{const auto item=darkangel::decode_interactable(json);const ImU32 colour=IM_COL32(120,255,160,object==editor.selected?255:190);ring({at.x,at.y+.05,at.z},item.radius,colour);box(at,{.25,.25,.25},colour,object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            // Party travel: a transition's box, and an entry point as a ring with a mark the way the party will face.
            for(const auto& [object,json]:plan.transitions)if(pose_of(object,at)){try{const auto transition=darkangel::decode_transition(json);box(at,{transition.half.x,transition.half.y,transition.half.z},IM_COL32(190,130,255,object==editor.selected?255:190),object==editor.selected?2.5f:1.2f);if(transition.activation==darkangel::TransitionActivation::Interact)ring({at.x,at.y+.05,at.z},transition.radius,IM_COL32(190,130,255,170));ImVec2 label;if(project({at.x,at.y+transition.half.y+.3,at.z},label))draw->AddText(label,IM_COL32(230,210,255,255),"Scene transition");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.entries)if(pose_of(object,at)){const ImU32 colour=IM_COL32(120,200,255,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},.6,colour);darkangel::Transform facing=at;facing.x+=std::sin(at.yaw)*.9;facing.z+=std::cos(at.yaw)*.9;facing.scale=1;box(facing,{.12,.12,.12},colour,2.f);ImVec2 label;if(project({at.x,at.y+1.,at.z},label)){std::string name="Entry";try{name="Entry: "+darkangel::decode_entry(json).name;}catch(const std::exception&){}draw->AddText(label,IM_COL32(210,235,255,255),name.c_str());}++volumes_drawn;}
            for(const auto& [object,json]:plan.pickups)if(pose_of(object,at)){try{const auto pickup=darkangel::decode_pickup(json);const ImU32 colour=IM_COL32(255,220,90,object==editor.selected?255:200);ring({at.x,at.y+.05,at.z},pickup.radius,colour);darkangel::Transform marker=at;marker.y+=.3;marker.scale=1;box(marker,{.2,.2,.2},colour,object==editor.selected?2.5f:1.4f);ImVec2 label;if(project({at.x,at.y+.8,at.z},label))draw->AddText(label,colour,("Pickup x"+std::to_string(pickup.quantity)).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.altars)if(pose_of(object,at)){try{const auto altar=darkangel::decode_altar(json);const ImU32 colour=IM_COL32(255,150,110,object==editor.selected?255:210);ring({at.x,at.y+.05,at.z},altar.radius,colour);darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.35,.5,.35},colour,object==editor.selected?2.5f:1.6f);const auto arrival=darkangel::altar_arrival(altar,{at.x,at.y,at.z},at.yaw);ring({arrival.x,arrival.y+.05,arrival.z},.5,IM_COL32(120,200,255,200));line({at.x,at.y+.1,at.z},{arrival.x,arrival.y+.1,arrival.z},IM_COL32(120,200,255,160),1.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,colour,("Altar: "+altar.name).c_str());++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.movers)if(pose_of(object,at)){try{const auto mover=darkangel::decode_mover(json);const ImU32 colour=IM_COL32(150,230,230,object==editor.selected?255:200);darkangel::Transform start=at;start.y+=mover.half.y*at.scale;box(start,{mover.half.x,mover.half.y,mover.half.z},colour,object==editor.selected?2.5f:1.4f);
                const double c=std::cos(at.yaw),s=std::sin(at.yaw);darkangel::Transform end=start;end.x+=(c*mover.offset.x+s*mover.offset.z)*at.scale;end.y+=mover.offset.y*at.scale;end.z+=(-s*mover.offset.x+c*mover.offset.z)*at.scale;box(end,{mover.half.x,mover.half.y,mover.half.z},IM_COL32(150,230,230,110),1.f);line({start.x,start.y,start.z},{end.x,end.y,end.z},IM_COL32(150,230,230,160),1.f);
                ImVec2 label;if(project({start.x,start.y+mover.half.y*at.scale+.3,start.z},label))draw->AddText(label,colour,mover.door?(mover.locked?"Door (locked)":"Door"):"Platform");++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.triggers)if(pose_of(object,at)){try{const auto trigger=darkangel::decode_trigger(json);box(at,{trigger.half.x,trigger.half.y,trigger.half.z},IM_COL32(255,170,60,object==editor.selected?255:180),object==editor.selected?2.5f:1.2f);++volumes_drawn;}catch(const std::exception&){}}
            for(const auto& [object,json]:plan.encounters)if(pose_of(object,at)){darkangel::Transform marker=at;marker.y+=.5;marker.scale=1;box(marker,{.4,.4,.4},object==focus?IM_COL32(255,255,255,255):IM_COL32(120,220,255,230),2.f);ImVec2 label;if(project({at.x,at.y+1.3,at.z},label))draw->AddText(label,IM_COL32(255,255,255,255),"Encounter");}
        }
        draw->PopClipRect();
}
} // namespace darkangel::editor_app
