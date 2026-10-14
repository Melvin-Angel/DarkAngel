#include "world_ui_presenter.hpp"
#include <darkangel/scene_client.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
namespace darkangel::editor_app {
WorldUiCamera world_ui_camera(const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection,float width,float height){WorldUiCamera camera;std::memcpy(camera.view.data(),&view,sizeof(view));std::memcpy(camera.projection.data(),&projection,sizeof(projection));camera.width=width;camera.height=height;return camera;}
std::vector<HudWorldMarker> character_world_ui(const CharacterSceneSession& session,const SceneClient& client,const WorldUiCamera& camera,std::span<const PartyMember> roster){
    std::vector<HudWorldMarker> result;if(!client.bootstrapped())return result;
    auto marker=[&](StableId id,std::string label,bool bar){
        if(id==client.player()||result.size()>=33)return;const auto observed=client.observer_if_ready(id,double(client.tick()));if(!observed)return;
        const auto handle=client.presentation().find(id);if(!client.presentation().valid(handle))return;const auto health=client.presentation().read(handle).health;if(bar&&(health.current<=0||health.maximum<=0))return;
        const auto position=observed->frame.motor.position;const auto at=project_world_ui(camera,position.x,position.y+2.1,position.z);if(!at)return;
        const float width=(bar?140.f:160.f)*at->scale;float top=at->y-26.f*at->scale;const float left=at->x-width*.5f;for(const auto& placed:result)if(left<placed.x+placed.width&&left+width>placed.x&&std::abs(top-placed.y)<36.f)top=placed.y-36.f;if(top<0)return;result.push_back({label.substr(0,64),left,top,width,13.f*at->scale,7.f*at->scale,health.maximum>0?float(std::clamp(health.current/health.maximum,0.,1.)*100):0.f,bar});
    };
    for(auto id:session.actors())if(const auto* actor=session.actor(id))marker(id,actor->name,true);
    if(!roster.empty()){for(const auto& member:roster)marker(member.avatar,"Player "+std::to_string(member.seat+1)+(member.seat==0?" (Host)":" (Client)"),false);}
    else {unsigned seat{};for(auto id:session.players()){++seat;marker(id,"Player "+std::to_string(seat)+(seat==1?" (Host)":" (Client)"),false);}}
    return result;
}
}
