#include "play_ui.hpp"
#include "hud_overlay.hpp"
#include <ashen_roots/royal_ui_assets.hpp>
#include <darkangel/scene_client.hpp>
#include <darkangel/scene_content.hpp>
#include <darkangel/ui_assets.hpp>
#include <imgui.h>
#include <cmath>
#include <algorithm>
namespace darkangel::editor_app {
namespace {
UiDocumentSource source(const std::filesystem::path& registry,const std::filesystem::path& cas,std::string_view id){
 auto cooked=load_cooked_ui_document(registry,cas,AssetId::parse(id));UiDocumentSource result;
 result.binding=cooked.definition.binding;result.surfaces=cooked.definition.surfaces;result.markup=cooked.definition.markup;result.style=cooked.definition.style;result.script=cooked.definition.script;
 for(auto& [family,bytes]:cooked.fonts)result.fonts.push_back({family,std::move(bytes)});
 for(auto& image:cooked.images)result.images.push_back({image.url,{image.width,image.height,std::move(image.rgba)}});return result;
}
PlayerUiDocuments documents(const ActorUiDefinition& actor){auto result=ashen_roots::player_ui_documents();if(!actor.hud.empty()){result.hud.clear();for(const auto& a:actor.hud)result.hud.push_back(a.document.text());}return result;}
}
PlayUi::PlayUi(ImGuiHudRenderer& r,UiFont font,const std::filesystem::path& registry,const std::filesystem::path& cas,const ActorUiDefinition& definition,const std::map<StableId,ActorUiDefinition>& actors)
 :service(r,std::move(font),[registry,cas](auto id){return source(registry,cas,id);}),player(service,documents(definition),1280,720),player_markers(definition.markers),actor_specs(actors),renderer(r){}
void PlayUi::frame(SceneClient& client,const OwnerUiModel&,const WorldUiCamera& camera,bool focused,bool loading,float x,float y,std::span<const StableId> players,std::span<const UiActorInfo> actors){
 std::set<StableId> wanted;marker_rows.clear();
 auto add=[&](StableId id,std::string name,bool peer){if(id==client.player())return;wanted.insert(id);if(!attached.insert(id).second)return;
  auto list=peer?player_markers:actor_specs[id].markers;if(list.empty())list.push_back({AssetId::parse(ashen_roots::ui_actormarker),"ui/markers/ActorMarker.daui"});
  for(const auto& a:list)if(markers.size()<32)markers.push_back({id,name,a,service.attach(a.document.text(),"actor:"+id.text(),256,96),peer});
 };
 for(const auto& actor:actors)add(actor.id,actor.name.substr(0,64),false);
 unsigned seat{};for(auto id:players){++seat;add(id,"Player "+std::to_string(seat),true);}
 for(auto it=attached.begin();it!=attached.end();)if(!wanted.contains(*it)){const auto id=*it;service.close_scope("actor:"+id.text());std::erase_if(markers,[&](const auto& m){return m.actor==id;});it=attached.erase(it);}else ++it;
 service.resize_screens(std::max(1,int(camera.width)),std::max(1,int(camera.height)));
 for(auto& marker:markers){auto observed=client.observer_if_ready(marker.actor,double(client.tick()));auto handle=client.presentation().find(marker.actor);bool visible=false;
  if(!loading&&observed&&client.presentation().valid(handle)){
   const auto at=observed->frame.motor.position;const auto owner=client.predicted_motor().position;const auto health=client.presentation().read(handle).health;
   auto& model=service.model(marker.handle);model.health=health.current;model.maximum_health=health.maximum;model.name=marker.name;model.flag=!marker.player;
   if(marker.player)if(const auto* state=client.world_state())for(const auto& p:state->members)if(client.identity(p.network)==marker.actor&&p.state==PartyMemberState::Downed)model.name=(marker.name+" - Downed").substr(0,64);
   if((marker.player||health.current>0)&&std::hypot(at.x-owner.x,at.z-owner.z)<=marker.attachment.distance){const auto& o=marker.attachment.offset;auto projected=project_world_ui(camera,at.x+o[0],at.y+o[1],at.z+o[2]);if(projected){
    model.scale=projected->scale;float width=160*projected->scale,left=projected->x-width*.5f,top=projected->y-26*projected->scale;
    for(const auto& placed:marker_rows)if(left<placed.x+placed.width&&left+width>placed.x&&std::abs(top-placed.y)<36)top=placed.y-36;
    if(top>=0){service.place(marker.handle,left,top,true);visible=true;marker_rows.push_back({model.name,left,top,width,14*projected->scale,6*projected->scale,health.maximum>0?float(std::clamp(health.current/health.maximum,0.,1.)*100):0.f,!marker.player});}
   }}
  }if(!visible)service.place(marker.handle,0,0,false);
 }
 if(loading){service.close_scope("puzzle");puzzle={};}
 const auto* outcome=client.last_interaction();
 if(!loading&&outcome&&outcome->tick!=seen){seen=outcome->tick;
  if(outcome->result==InteractionResult::Accepted&&client.combat())for(const auto& item:client.combat()->interactables)if(item.id==outcome->object&&item.definition.puzzle){
   service.close_scope("puzzle");puzzle_object=item.id;puzzle=service.open(item.definition.puzzle->document,UiService::Input::Modal,"puzzle",int(camera.width),int(camera.height));
  }
 }
 if(service.valid(puzzle)){
  if(const auto* state=client.world_state())for(const auto& p:state->puzzles)if(p.object==puzzle_object){auto& m=service.model(puzzle);m.flag=p.solved;m.name=p.solved?"Solved":std::to_string(p.progress)+" / "+std::to_string(p.length);}
  if(client.combat())for(const auto& item:client.combat()->interactables)if(item.id==puzzle_object){const auto at=client.predicted_motor().position;if(std::hypot(at.x-item.position.x,at.z-item.position.z)>item.definition.radius+2||client.downed())service.close(puzzle);}
 }
 if(service.modal_open())suppress=true;
 else if(!ImGui::IsMouseDown(0)&&!ImGui::IsKeyDown(ImGuiKey_Enter)&&!ImGui::IsKeyDown(ImGuiKey_Escape)&&!ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown)&&!ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight))suppress=false;
 if(focused&&service.modal_open()){
  auto key=[&](ImGuiKey a,ImGuiKey b,UiKey k){if(ImGui::IsKeyPressed(a,false)||ImGui::IsKeyPressed(b,false))service.key(k);};
  key(ImGuiKey_UpArrow,ImGuiKey_GamepadDpadUp,UiKey::Up);key(ImGuiKey_DownArrow,ImGuiKey_GamepadDpadDown,UiKey::Down);
  key(ImGuiKey_LeftArrow,ImGuiKey_GamepadDpadLeft,UiKey::Left);key(ImGuiKey_RightArrow,ImGuiKey_GamepadDpadRight,UiKey::Right);
  key(ImGuiKey_Enter,ImGuiKey_GamepadFaceDown,UiKey::Confirm);key(ImGuiKey_Escape,ImGuiKey_GamepadFaceRight,UiKey::Cancel);

 }
 const auto mouse=ImGui::GetIO().MousePos;const bool inside=mouse.x>=x&&mouse.y>=y&&mouse.x<x+camera.width&&mouse.y<y+camera.height;
 if(focused&&(inside||ImGui::IsMouseReleased(0)))service.pointer(int(mouse.x-x),int(mouse.y-y),inside&&ImGui::IsMouseClicked(0),ImGui::IsMouseReleased(0),inside?ImGui::GetIO().MouseWheel:0);
 service.update();
 for(const auto& command:service.drain_commands(puzzle))if(command.handle==puzzle&&service.valid(puzzle)&&command.command.starts_with("puzzle.rune.")&&command.command.size()==13){
  const unsigned press=unsigned(command.command.back()-'0');if(press>=1&&press<=9)client.world_request({WorldRequestKind::Puzzle,puzzle_object,{},press});
 }
}
void PlayUi::render(ImDrawList* draw,float x,float y,float width,float height){
 bool first=true;service.render([&](auto& view,float left,float top,float w,float h){renderer.begin(draw,{x+left,y+top},{std::min(w,width),std::min(h,height)},first);first=false;view.render();renderer.end();});
}
}
