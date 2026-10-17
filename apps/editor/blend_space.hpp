#pragma once
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace darkangel::editor_app {
inline bool draw_blend_space(nlohmann::json& node,unsigned& selected,float speed,float forward,float lateral){
 bool changed=false;
 auto& points=node.at("points");if(points.empty()||points.size()>32){ImGui::TextDisabled("Blend view requires 1-32 points.");return false;}if(selected>=points.size())selected=0;
 const bool two=node.at("kind")=="blend2d";float measured_x=two?lateral:node.at("parameter")=="forward"?forward:node.at("parameter")=="lateral"?lateral:speed;float measured_y=two?forward:0;
 float bound=std::max({1.f,std::abs(measured_x),std::abs(measured_y)});for(const auto& point:points){float x=point.at("x").get<float>(),y=point.at("y").get<float>();if(!std::isfinite(x)||!std::isfinite(y)){ImGui::TextColored({1,.6f,.3f,1},"Non-finite blend coordinates; fix the fields before preview.");return false;}bound=std::max({bound,std::abs(x),std::abs(y)});}bound*=1.2f;
 ImGui::SeparatorText(two?"Directional blend space":"1D blend space");ImGui::TextWrapped(two?"X = local lateral, Y = local forward. Select a point to edit its typed fields below.":"X = the selected measured parameter. Select a point to edit its fields below.");
 auto origin=ImGui::GetCursorScreenPos();ImVec2 size{std::max(160.f,ImGui::GetContentRegionAvail().x),260};ImGui::InvisibleButton("##blend-space",size);bool hovered=ImGui::IsItemHovered();auto draw=ImGui::GetWindowDrawList();ImVec2 center{origin.x+size.x*.5f,origin.y+size.y*.5f};float scale=std::min(size.x-60,size.y-50)/(2*bound);auto position=[&](float x,float y){return ImVec2{center.x+x*scale,center.y-y*scale};};
 draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(29,33,36,255),4);draw->AddLine({origin.x+20,center.y},{origin.x+size.x-20,center.y},IM_COL32(86,91,98,255));if(two)draw->AddLine({center.x,origin.y+15},{center.x,origin.y+size.y-15},IM_COL32(86,91,98,255));
 std::vector<ImVec2> plotted;for(const auto& point:points)plotted.push_back(position(point.at("x").get<float>(),two?point.at("y").get<float>():0));unsigned invalid=0;
 if(two)for(const auto& triangle:node.at("triangles")){if(!triangle.is_array()||triangle.size()!=3){++invalid;continue;}auto a=triangle[0].get<unsigned>(),b=triangle[1].get<unsigned>(),c=triangle[2].get<unsigned>();if(a>=plotted.size()||b>=plotted.size()||c>=plotted.size()){++invalid;continue;}draw->AddTriangleFilled(plotted[a],plotted[b],plotted[c],IM_COL32(54,112,142,50));draw->AddTriangle(plotted[a],plotted[b],plotted[c],IM_COL32(87,135,157,170));}
 static ImGuiID drag_canvas{};static unsigned drag_point=std::numeric_limits<unsigned>::max();if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))drag_point=std::numeric_limits<unsigned>::max();
 auto mouse=ImGui::GetMousePos();for(unsigned i=0;i<plotted.size();++i){auto p=plotted[i];draw->AddCircleFilled(p,i==selected?7:5,i==selected?IM_COL32(111,209,238,255):IM_COL32(194,207,215,255));auto text=std::to_string(i);draw->AddText({p.x+8,p.y-14},IM_COL32(221,226,232,255),text.c_str());auto dx=mouse.x-p.x,dy=mouse.y-p.y;if(hovered&&dx*dx+dy*dy<144){ImGui::SetTooltip("Point %u / Node %u\nX %.3f  Y %.3f",i,points[i].at("input").get<unsigned>(),points[i].at("x").get<double>(),points[i].at("y").get<double>());if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){selected=i;drag_canvas=ImGui::GetID("##blend-space");drag_point=i;}}}
 if(hovered&&ImGui::IsMouseDown(ImGuiMouseButton_Left)&&drag_canvas==ImGui::GetID("##blend-space")&&drag_point==selected&&selected<plotted.size()){
  const auto selected_point=plotted[selected];const auto dx=mouse.x-selected_point.x,dy=mouse.y-selected_point.y;
  if(ImGui::IsItemActive()||dx*dx+dy*dy<196){
   double x=std::clamp(static_cast<double>(mouse.x-center.x)/scale,-100.0,100.0),y=std::clamp(static_cast<double>(center.y-mouse.y)/scale,-100.0,100.0);
   if(!two)y=0;
   if(!two){if(selected>0)x=std::max(x,points[selected-1].at("x").get<double>()+.001);if(selected+1<points.size())x=std::min(x,points[selected+1].at("x").get<double>()-.001);}
   if(std::abs(points[selected].at("x").get<double>()-x)>.0001||std::abs(points[selected].at("y").get<double>()-y)>.0001){points[selected]["x"]=x;points[selected]["y"]=y;changed=true;}
  }
 }
 auto measured=position(measured_x,measured_y);auto color=IM_COL32(242,186,63,255);draw->AddLine({measured.x-6,measured.y},{measured.x+6,measured.y},color,2);draw->AddLine({measured.x,measured.y-6},{measured.x,measured.y+6},color,2);draw->AddText({origin.x+12,origin.y+10},color,"Cross = requested preview input");
 ImGui::Text("View +/- %.2f | selected point %u | preview input %.2f, %.2f",bound,selected,measured_x,measured_y);ImGui::TextDisabled("Drag a sample point to author its blend-space position.");if(invalid)ImGui::TextColored({1,.6f,.3f,1},"%u invalid triangle references; Save/preview validates geometry.",invalid);
 return changed;
}
}
