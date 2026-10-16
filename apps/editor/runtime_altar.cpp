#include "runtime_altar.hpp"
#include <darkangel/scene_client.hpp>
#include <darkangel/scene_content.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace darkangel::editor_app {
namespace {const CharacterSceneAltar* find(const SceneClient* client,StableId id){if(!client||!client->combat())return nullptr;for(const auto& altar:client->combat()->altars)if(altar.id==id)return &altar;return nullptr;}}
void RuntimeAltar::notice(const SceneClient* client){
    // A new accepted use of an altar opens its menu; nothing else does.
    if(!client)return;const auto* outcome=client->last_interaction();if(!outcome||outcome->tick==seen_)return;seen_=outcome->tick;
    if(outcome->result==InteractionResult::Accepted&&find(client,outcome->object)){altar_=outcome->object;status_.clear();std::cout<<"Altar menu opened "<<find(client,outcome->object)->definition.name<<" tick="<<outcome->tick<<std::endl;}
}
AltarUiModel RuntimeAltar::model(const SceneClient* client,bool host,bool loading)const{
    AltarUiModel view;const auto* altar=altar_?find(client,*altar_):nullptr;if(!altar||loading)return view;
    view.open=true;view.name=altar->definition.name;view.can_rest=altar->definition.rest;view.can_travel=host&&altar->definition.fast_travel;view.status=status_;
    if(const auto* state=client->world_state())for(const auto& d:state->destinations){view.destinations.push_back({d.scene.text()+"/"+d.altar.text(),"altar-destination-"+std::to_string(view.destinations.size()),d.name,d.altar==altar->id});if(view.destinations.size()>=64)break;}
    if(view.status.empty())view.status=!altar->definition.fast_travel?"This altar is not a fast-travel destination.":!host?"Only the host chooses fast travel.":"Choose a destination, or rest.";
    return view;
}
void RuntimeAltar::actions(PlayerUi& hud,SceneClient* client,bool host){
    for(const auto& action:hud.drain_altar_actions()){
        if(action.kind==AltarUiActionKind::Close){altar_.reset();continue;}if(!altar_||!client)continue;
        WorldRequest request;request.altar=*altar_;
        if(action.kind==AltarUiActionKind::Rest)request.kind=WorldRequestKind::Rest;
        else{const auto* state=client->world_state();if(!host||!state||action.destination>=state->destinations.size())continue;request.kind=WorldRequestKind::Travel;request.destination=state->destinations[action.destination];}
        status_=client->world_request(request)?(request.kind==WorldRequestKind::Rest?"Resting...":"Travelling to "+request.destination.name+"..."):"Not connected yet. Try again.";
    }
}
void RuntimeAltar::flush(PlayerUi& hud,SceneClient* client,bool host,bool loading){actions(hud,client,host);hud.update_altar(model(client,host,loading));}
void RuntimeAltar::frame(PlayerUi& hud,SceneClient* client,bool host,bool focused,bool loading){
    notice(client);if(loading)altar_.reset();
    (void)focused;
    // Leaving the altar closes its menu.
    if(altar_&&client){const auto* altar=find(client,*altar_);const auto& at=client->predicted_motor().position;if(!altar||std::hypot(at.x-altar->position.x,at.z-altar->position.z)>altar->definition.radius+2.5)altar_.reset();}
    hud.update_altar(model(client,host,loading));actions(hud,client,host);hud.update_altar(model(client,host,loading));
}
void party_lifecycle_hud(OwnerUiModel& model,const SceneClient& client,const std::optional<InteractionPrompt>& offered,bool interact_held){
    const auto* state=client.world_state();if(!state)return;model.wiped=state->wiped;model.downed=client.downed();
    for(const auto& member:state->members){if(member.state!=PartyMemberState::Downed||member.revive<=0)continue;
        if(member.network==client.network()){model.revive_text="Being revived";model.revive_progress=member.revive;}
        else if(offered&&offered->hold&&interact_held&&client.identity(member.network)==offered->id){model.revive_text="Reviving";model.revive_progress=member.revive;}}
}
bool RuntimeAltar::exercise(std::string_view verb,unsigned value,PlayerUi& hud,SceneClient* client,bool host){
    notice(client);hud.update_altar(model(client,host,false));
    auto click=[&](const std::string& id){auto box=hud.box(id);if(!box)return false;const int x=int(box->x+box->width/2),y=int(box->y+box->height/2);hud.pointer(x,y,true,false);hud.pointer(x,y,false,true);return true;};
    bool done{};
    if(verb=="altar-await")done=open();
    else if(verb=="altar-rest")done=open()&&click("altar-rest");
    else if(verb=="altar-travel")done=open()&&click("altar-destination-"+std::to_string(value));
    else if(verb=="altar-close")done=!open()||click("altar-close");
    else return false;
    actions(hud,client,host);hud.update_altar(model(client,host,false));return done;
}
}
