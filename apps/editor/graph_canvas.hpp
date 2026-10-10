#pragma once
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <darkangel/assets.hpp>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>
namespace darkangel::editor_app {
struct GraphInputConnection {unsigned node{},point{},input{};};
struct GraphNodePayload {AssetId asset;unsigned node{};};
inline std::optional<GraphInputConnection> draw_graph_canvas(AssetId asset,const nlohmann::json& value,unsigned& selected){
 using Json=nlohmann::json;const auto& nodes=value.at("nodes");std::optional<GraphInputConnection> result;
 if(nodes.empty()||nodes.size()>32){ImGui::TextDisabled("Canvas requires 1-32 native nodes.");return result;}
 std::map<unsigned,const Json*> lookup;for(const auto& node:nodes)lookup.emplace(node.at("id").get<unsigned>(),&node);
 std::map<unsigned,unsigned> depths;auto root=value.at("root").get<unsigned>();std::vector<unsigned> queue{root};depths[root]=0;unsigned maximum=0;
 for(unsigned i=0;i<queue.size()&&i<32;++i){auto id=queue[i];auto found=lookup.find(id);if(found==lookup.end()||!found->second->contains("points"))continue;for(const auto& point:found->second->at("points")){auto input=point.at("input").get<unsigned>();if(!lookup.contains(input)||depths.contains(input))continue;depths[input]=depths[id]+1;maximum=std::max(maximum,depths[input]);queue.push_back(input);}}
 bool disconnected=false;for(const auto& [id,node]:lookup)if(!depths.contains(id))disconnected=true;
 std::map<unsigned,ImVec2> positions;std::map<unsigned,float> row;float extent_x=0,extent_y=0;
 for(const auto& [id,node]:lookup){auto depth=depths.contains(id)?maximum-depths[id]:maximum+1;auto y=row[depth]+18.f;auto h=65.f+(node->contains("points")?22.f*float(node->at("points").size()):0.f);positions[id]={18.f+235.f*depth,y};row[depth]=y+h+18.f;extent_x=std::max(extent_x,positions[id].x+205.f);extent_y=std::max(extent_y,row[depth]);}
 ImGui::TextWrapped("Select nodes to inspect. Drag a node onto a numbered blend input to connect it. Save validates reachability, geometry and frozen consumers.");
 if(disconnected)ImGui::TextColored({1,.7f,.3f,1},"Unconnected nodes are in the last column; connect or remove them before Save.");
 ImGui::BeginChild("Native graph canvas",{0,360},ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar);
 auto origin=ImGui::GetCursorScreenPos();auto local=ImGui::GetCursorPos();origin={origin.x-local.x,origin.y-local.y};auto draw=ImGui::GetWindowDrawList();
 for(const auto& [id,node]:lookup)if(node->contains("points"))for(unsigned i=0;i<node->at("points").size();++i){auto input=node->at("points")[i].at("input").get<unsigned>();if(!positions.contains(input))continue;auto a=positions[input],b=positions[id];a={origin.x+a.x+205.f,origin.y+a.y+22.f};b={origin.x+b.x,origin.y+b.y+66.f+22.f*i};auto color=IM_COL32(116,165,182,220);draw->AddBezierCubic(a,{a.x+65,a.y},{b.x-65,b.y},b,color,2.f);draw->AddTriangleFilled(b,{b.x-7,b.y-4},{b.x-7,b.y+4},color);}
 for(const auto& [id,node]:lookup){ImGui::PushID(id);auto p=positions[id];float h=65.f+(node->contains("points")?22.f*float(node->at("points").size()):0.f);ImVec2 a{origin.x+p.x,origin.y+p.y};draw->AddRectFilled(a,{a.x+205.f,a.y+h},id==selected?IM_COL32(45,84,104,255):IM_COL32(39,43,47,255),5);draw->AddRect(a,{a.x+205.f,a.y+h},id==root?IM_COL32(99,181,210,255):IM_COL32(83,89,93,255),5);
 ImGui::SetCursorPos(p);auto title=std::to_string(id)+" / "+node->at("kind").get<std::string>()+(id==root?" / Root":"");if(ImGui::Selectable(title.c_str(),id==selected,0,{205,26}))selected=id;
 if(ImGui::BeginDragDropSource()){GraphNodePayload payload{asset,id};ImGui::SetDragDropPayload("DA_NATIVE_GRAPH_NODE",&payload,sizeof(payload));ImGui::Text("Connect node %u",id);ImGui::EndDragDropSource();}
 ImGui::SetCursorPos({p.x+8,p.y+32});if(node->contains("parameter"))ImGui::Text("Parameter: %s",(node->at("kind")=="blend2d"?std::string("lateral / forward"):node->at("parameter").get<std::string>()).c_str());else {auto clip=node->at("clip").get<std::string>();ImGui::Text("Clip %.8s...",clip.c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",clip.c_str());}
 if(node->contains("points"))for(unsigned i=0;i<node->at("points").size();++i){ImGui::PushID(i);ImGui::SetCursorPos({p.x+8,p.y+55.f+22.f*i});auto input=node->at("points")[i].at("input").get<unsigned>();auto label="Input "+std::to_string(i)+" <- Node "+std::to_string(input);if(ImGui::SmallButton(label.c_str()))selected=input;
 if(ImGui::BeginDragDropTarget()){if(auto payload=ImGui::AcceptDragDropPayload("DA_NATIVE_GRAPH_NODE")){if(payload->DataSize==sizeof(GraphNodePayload)){auto source=*static_cast<const GraphNodePayload*>(payload->Data);if(source.asset==asset&&lookup.contains(source.node)&&source.node!=input)result=GraphInputConnection{id,i,source.node};}}ImGui::EndDragDropTarget();}ImGui::PopID();}
 ImGui::PopID();}
 ImGui::SetCursorPos({extent_x,extent_y});ImGui::Dummy({1,1});ImGui::EndChild();return result;
}
}
