#include "editor_ui.hpp"
#include <darkangel/action.hpp>
#include <algorithm>
#include <cmath>
#include <map>

namespace darkangel::editor_app {
void Shell::draw_action_lanes(AssetId asset,const nlohmann::json& source){
    ImGui::SeparatorText("Action Composer");
    // Same source-timeline projection as the native action cooker. Clip/rig
    // closure validation still belongs to Save, not this read-only lane view.
    auto timeline=source;
    if(timeline.at("schema")==2){timeline.erase("motion");timeline["schema"]=1;}
    ActionDefinition action;
    try{action=decode_action_source(timeline.dump());}
    catch(const std::exception& error){ImGui::TextWrapped("Timeline needs attention: %s",error.what());return;}
    if(composer_asset!=asset){composer_asset=asset;composer_block=0;composer_tick=0;}
    const double duration=double(action.duration)/action_tick_units;
    composer_tick=std::clamp(std::isfinite(composer_tick)?composer_tick:0.,0.,duration);
    ImGui::TextDisabled("Read-only lanes. Select a block to inspect its numeric fields.");
    ImGui::SliderScalar("Scrub tick",ImGuiDataType_Double,&composer_tick,&composer_zero,&duration,"%.2f");
    std::map<unsigned,std::vector<const ActionBlock*>> tracks;
    for(const auto& block:action.blocks)tracks[block.track].push_back(&block);
    const float label_width=70.f,row_height=32.f,ruler_height=28.f;
    const float width=std::max(180.f,ImGui::GetContentRegionAvail().x);
    const float span=width-label_width-8.f,height=ruler_height+row_height*float(tracks.size());
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Action lanes",{width,height});
    auto* draw=ImGui::GetWindowDrawList();
    const auto x=[&](double tick){return origin.x+label_width+float(tick/duration)*span;};
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},IM_COL32(27,29,34,255),4.f);
    const unsigned step=std::max(1u,unsigned(std::ceil(duration/8.)));
    for(unsigned tick=0;tick<=unsigned(duration);tick+=step){
        float px=x(tick);draw->AddLine({px,origin.y+20},{px,origin.y+height},IM_COL32(65,68,76,255));
        auto label=std::to_string(tick);draw->AddText({px+2,origin.y+3},IM_COL32(185,188,198,255),label.c_str());
    }
    bool hit=false;unsigned row{};
    for(const auto& [track,blocks]:tracks){
        float y=origin.y+ruler_height+row_height*float(row++);
        auto label="Track "+std::to_string(track);draw->AddText({origin.x+5,y+7},IM_COL32(185,188,198,255),label.c_str());
        draw->PushClipRect({origin.x+label_width,y},{origin.x+width,y+row_height},true);
        for(const auto* block:blocks){
            float begin=x(double(block->begin)/action_tick_units),end=x(double(block->end)/action_tick_units);
            const bool marker=block->begin==block->end;
            ImU32 color=block->kind==ActionBlockKind::HitWindow?IM_COL32(185,85,65,255):block->kind==ActionBlockKind::Commit?IM_COL32(220,172,55,255):IM_COL32(55,105,165,255);
            if(marker){draw->AddTriangleFilled({begin,y+3},{begin-5,y+13},{begin+5,y+13},color);draw->AddLine({begin,y+3},{begin,y+row_height-3},color,2);}
            else draw->AddRectFilled({begin,y+4},{end,y+row_height-4},color,3);
            if(composer_block==block->id)draw->AddRect({begin-3,y+2},{std::max(end,begin+3)+3,y+row_height-2},IM_COL32(245,245,250,255),3,0,2);
            if(!marker&&end-begin>35)draw->AddText({begin+4,y+8},IM_COL32(255,255,255,255),block->key.c_str());
            auto mouse=ImGui::GetMousePos();
            if(ImGui::IsItemHovered()&&mouse.x>=begin-5&&mouse.x<=std::max(end,begin+5)&&mouse.y>=y&&mouse.y<y+row_height){
                ImGui::SetTooltip("%s | block %u\n%.2f - %.2f ticks",block->key.c_str(),block->id,double(block->begin)/action_tick_units,double(block->end)/action_tick_units);
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){composer_block=block->id;composer_tick=double(block->begin)/action_tick_units;hit=true;}
            }
        }
        draw->PopClipRect();
    }
    if(!hit&&ImGui::IsItemHovered()&&ImGui::IsMouseDown(ImGuiMouseButton_Left))composer_tick=std::clamp(double(ImGui::GetMousePos().x-origin.x-label_width)/span*duration,0.,duration);
    float cursor=x(composer_tick);draw->AddLine({cursor,origin.y+20},{cursor,origin.y+height},IM_COL32(235,235,245,255),2);
    ImGui::Text("Cursor %.2f / %.2f ticks | %u loop(s)",composer_tick,duration,action.loops);
}
}
