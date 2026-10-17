#pragma once
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <darkangel/assets.hpp>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>
namespace darkangel::editor_app {
enum class GraphCanvasCommand {Connect,AddBlend1D,AddBlend2D,DuplicateNode,RemoveNode};
struct GraphInputConnection {unsigned node{},point{},input{};GraphCanvasCommand command{GraphCanvasCommand::Connect};};
struct GraphNodePayload {AssetId asset;unsigned node{};};
inline std::optional<GraphInputConnection> draw_graph_canvas(AssetId asset,const nlohmann::json& value,unsigned& selected,float& zoom,ImVec2& pan){
 using Json=nlohmann::json;const auto& nodes=value.at("nodes");std::optional<GraphInputConnection> result;
 if(nodes.empty()||nodes.size()>32){ImGui::TextDisabled("Canvas requires 1-32 native nodes.");return result;}
 std::map<unsigned,const Json*> lookup;for(const auto& node:nodes)lookup.emplace(node.at("id").get<unsigned>(),&node);
 std::map<unsigned,unsigned> depths;auto root=value.at("root").get<unsigned>();std::vector<unsigned> queue{root};depths[root]=0;unsigned maximum=0;
 for(unsigned i=0;i<queue.size()&&i<32;++i){auto id=queue[i];auto found=lookup.find(id);if(found==lookup.end()||!found->second->contains("points"))continue;for(const auto& point:found->second->at("points")){auto input=point.at("input").get<unsigned>();if(!lookup.contains(input)||depths.contains(input))continue;depths[input]=depths[id]+1;maximum=std::max(maximum,depths[input]);queue.push_back(input);}}
 bool disconnected=false;for(const auto& [id,node]:lookup)if(!depths.contains(id))disconnected=true;
 std::map<unsigned,ImVec2> positions;std::map<unsigned,float> row;float extent_x=0,extent_y=0;
 for(const auto& [id,node]:lookup){auto depth=depths.contains(id)?maximum-depths[id]:maximum+1;auto y=row[depth]+18.f;auto h=65.f+(node->contains("points")?22.f*float(node->at("points").size()):0.f);positions[id]={18.f+235.f*depth,y};row[depth]=y+h+18.f;extent_x=std::max(extent_x,positions[id].x+205.f);extent_y=std::max(extent_y,row[depth]);}
 zoom=std::clamp(zoom,.65f,1.6f);
 ImGui::TextDisabled("Graph-first authoring | middle-drag pans | wheel zooms | drag a node onto a numbered input to connect it.");
 if(ImGui::SmallButton("Auto Layout")){zoom=1.f;pan={18.f,18.f};}
 ImGui::SameLine();ImGui::SetNextItemWidth(150);ImGui::SliderFloat("Zoom",&zoom,.65f,1.6f,"%.0f%%",ImGuiSliderFlags_NoInput);
 ImGui::TextWrapped("Select nodes to inspect. Save validates reachability, geometry and frozen consumers.");
 if(disconnected)ImGui::TextColored({1,.7f,.3f,1},"Unconnected nodes are in the last column; connect or remove them before Save.");
 const float canvas_height=std::max(380.f,ImGui::GetContentRegionAvail().y);
 ImGui::BeginChild("Native graph canvas",{0,canvas_height},ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
 auto origin=ImGui::GetCursorScreenPos();auto local=ImGui::GetCursorPos();origin={origin.x-local.x,origin.y-local.y};auto draw=ImGui::GetWindowDrawList();
 const auto scaled=[&](ImVec2 p){return ImVec2{p.x*zoom+pan.x,p.y*zoom+pan.y};};
 if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)){
  if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle))pan.x+=ImGui::GetIO().MouseDelta.x,pan.y+=ImGui::GetIO().MouseDelta.y;
  if(ImGui::GetIO().MouseWheel!=0){const auto mouse=ImGui::GetMousePos();const auto before=ImVec2{(mouse.x-origin.x-pan.x)/zoom,(mouse.y-origin.y-pan.y)/zoom};zoom=std::clamp(zoom+ImGui::GetIO().MouseWheel*.08f,.65f,1.6f);pan.x=mouse.x-origin.x-before.x*zoom;pan.y=mouse.y-origin.y-before.y*zoom;}
 }
 const auto mouse=ImGui::GetMousePos();bool over_node=false;
 for(const auto& [id,node]:lookup){const auto p=scaled(positions[id]);const float h=(65.f+(node->contains("points")?22.f*float(node->at("points").size()):0.f))*zoom;if(mouse.x>=origin.x+p.x&&mouse.x<=origin.x+p.x+205.f*zoom&&mouse.y>=origin.y+p.y&&mouse.y<=origin.y+p.y+h){over_node=true;break;}}
 if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)&&!over_node&&ImGui::IsMouseClicked(ImGuiMouseButton_Right))ImGui::OpenPopup("Native graph context");
 if(ImGui::BeginPopup("Native graph context")){if(ImGui::MenuItem("Add 1D blend"))result=GraphInputConnection{0,0,selected,GraphCanvasCommand::AddBlend1D};if(ImGui::MenuItem("Add 2D blend"))result=GraphInputConnection{0,0,selected,GraphCanvasCommand::AddBlend2D};ImGui::EndPopup();}
 for(const auto& [id,node]:lookup)if(node->contains("points"))for(unsigned i=0;i<node->at("points").size();++i){auto input=node->at("points")[i].at("input").get<unsigned>();if(!positions.contains(input))continue;auto a=scaled(positions[input]),b=scaled(positions[id]);a={origin.x+a.x+205.f*zoom,origin.y+a.y+22.f*zoom};b={origin.x+b.x,origin.y+b.y+66.f*zoom+22.f*zoom*i};auto color=IM_COL32(116,165,182,220);draw->AddBezierCubic(a,{a.x+65*zoom,a.y},{b.x-65*zoom,b.y},b,color,2.f);draw->AddTriangleFilled(b,{b.x-7*zoom,b.y-4*zoom},{b.x-7*zoom,b.y+4*zoom},color);}
 for(const auto& [id,node]:lookup){ImGui::PushID(id);auto p=scaled(positions[id]);float h=(65.f+(node->contains("points")?22.f*float(node->at("points").size()):0.f))*zoom;ImVec2 a{origin.x+p.x,origin.y+p.y};draw->AddRectFilled(a,{a.x+205.f*zoom,a.y+h},id==selected?IM_COL32(45,84,104,255):IM_COL32(39,43,47,255),5);draw->AddRect(a,{a.x+205.f*zoom,a.y+h},id==root?IM_COL32(99,181,210,255):IM_COL32(83,89,93,255),5);
 ImGui::SetCursorPos(p);auto title=std::to_string(id)+" / "+node->at("kind").get<std::string>()+(id==root?" / Root":"");if(ImGui::Selectable(title.c_str(),id==selected,0,{205.f*zoom,26.f*zoom}))selected=id;
 if(ImGui::BeginPopupContextItem("Native graph node context")){if(ImGui::MenuItem("Duplicate node"))result=GraphInputConnection{id,0,0,GraphCanvasCommand::DuplicateNode};if(ImGui::MenuItem("Delete node",nullptr,false,id!=root))result=GraphInputConnection{id,0,0,GraphCanvasCommand::RemoveNode};ImGui::EndPopup();}
 if(ImGui::BeginDragDropSource()){GraphNodePayload payload{asset,id};ImGui::SetDragDropPayload("DA_NATIVE_GRAPH_NODE",&payload,sizeof(payload));ImGui::Text("Connect node %u",id);ImGui::EndDragDropSource();}
 ImGui::SetCursorPos({p.x+8*zoom,p.y+32.f*zoom});if(node->contains("parameter"))ImGui::Text("Parameter: %s",(node->at("kind")=="blend2d"?std::string("lateral / forward"):node->at("parameter").get<std::string>()).c_str());else if(node->at("kind")=="tag-select")ImGui::TextUnformatted("Tag condition / two branches");else {auto clip=node->at("clip").get<std::string>();ImGui::Text("Clip %.8s...",clip.c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",clip.c_str());}
 if(node->contains("points"))for(unsigned i=0;i<node->at("points").size();++i){ImGui::PushID(i);ImGui::SetCursorPos({p.x+8*zoom,p.y+(55.f+22.f*i)*zoom});auto input=node->at("points")[i].at("input").get<unsigned>();auto label=(node->at("kind")=="tag-select"?std::string(i?"Matched":"Unmatched"):"Input "+std::to_string(i))+" <- Node "+std::to_string(input);if(ImGui::SmallButton(label.c_str()))selected=input;
 if(ImGui::BeginDragDropTarget()){if(auto payload=ImGui::AcceptDragDropPayload("DA_NATIVE_GRAPH_NODE")){if(payload->DataSize==sizeof(GraphNodePayload)){auto source=*static_cast<const GraphNodePayload*>(payload->Data);if(source.asset==asset&&lookup.contains(source.node)&&source.node!=input)result=GraphInputConnection{id,i,source.node,GraphCanvasCommand::Connect};}}ImGui::EndDragDropTarget();}ImGui::PopID();}
 ImGui::PopID();}
 ImGui::SetCursorPos({extent_x,extent_y});ImGui::Dummy({1,1});ImGui::EndChild();return result;
}
inline std::optional<GraphInputConnection> draw_graph_canvas(AssetId asset,const nlohmann::json& value,unsigned& selected){static float zoom=1.f;static ImVec2 pan{18.f,18.f};return draw_graph_canvas(asset,value,selected,zoom,pan);}
}
