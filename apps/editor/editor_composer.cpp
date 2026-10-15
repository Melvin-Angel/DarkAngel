#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <map>

namespace darkangel::editor_app {
void Shell::draw_action_lanes(Controller& controller,AssetId asset,const nlohmann::json& source){
    if(composer_dragging&&(composer_drag_asset!=asset||ImGui::GetIO().AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||!ImGui::IsMousePosValid()))cancel_composer_drag(controller);
    ImGui::SeparatorText("Action Composer");
    // Native core timing validation; clip/rig closure remains a Save gate.
    ActionDefinition action;
    try{action=authoring_action(source);}
    catch(const std::exception& error){ImGui::TextWrapped("Timeline needs attention: %s",error.what());return;}
    if(composer_asset!=asset){composer_asset=asset;composer_block=0;composer_tick=0;composer_playing=false;}
    const double duration=double(action.duration)/action_tick_units;
    if(ImGui::GetIO().AppFocusLost)composer_playing=false;
    if(composer_playing&&source.contains("motion")&&composer_clip.text()==source["motion"]["clip"].get<std::string>()&&composer_duration==action.duration){
        composer_tick+=composer_frame_seconds*60*composer_rate;
        if(composer_tick>=duration){if(composer_loop)composer_tick=std::fmod(composer_tick,duration);else{composer_tick=duration;composer_playing=false;}}
    }
    composer_tick=std::clamp(std::isfinite(composer_tick)?composer_tick:0.,0.,duration);
    ImGui::TextDisabled("Drag a block to move; drag its edges to resize. Escape cancels.");
    if(ImGui::SliderScalar("Scrub tick",ImGuiDataType_Double,&composer_tick,&composer_zero,&duration,"%.2f"))composer_playing=false;
    std::map<unsigned,std::vector<const ActionBlock*>> tracks;
    for(const auto& block:action.blocks)tracks[block.track].push_back(&block);
    const float label_width=70.f,row_height=24.f,ruler_height=24.f;
    const float width=std::max(180.f,ImGui::GetContentRegionAvail().x);
    const float span=width-label_width-8.f,height=ruler_height+row_height*float(tracks.size());
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Action lanes",{width,height});
    auto* draw=ImGui::GetWindowDrawList();
    const auto x=[&](double tick){return origin.x+label_width+float(tick/duration)*span;};
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(theme::background),4.f);
    const unsigned step=std::max(1u,unsigned(std::ceil(duration/8.)));
    for(unsigned tick=0;tick<=unsigned(duration);tick+=step){
        float px=x(tick);draw->AddLine({px,origin.y+20},{px,origin.y+height},ImGui::GetColorU32(theme::border));
        auto label=std::to_string(tick);draw->AddText({px+2,origin.y+3},ImGui::GetColorU32(theme::secondary),label.c_str());
    }
    bool hit=false;unsigned row{};
    for(const auto& [track,blocks]:tracks){
        float y=origin.y+ruler_height+row_height*float(row++);
        auto label="Track "+std::to_string(track);draw->AddText({origin.x+5,y+3},ImGui::GetColorU32(theme::secondary),label.c_str());
        for(const auto* block:blocks){
            float begin=x(double(block->begin)/action_tick_units),end=x(double(block->end)/action_tick_units);
            const bool marker=block->begin==block->end;
            ImU32 color=block->kind==ActionBlockKind::HitWindow?IM_COL32(185,85,65,255):block->kind==ActionBlockKind::Commit?IM_COL32(220,172,55,255):IM_COL32(55,105,165,255);
            if(marker){draw->AddTriangleFilled({begin,y+3},{begin-5,y+13},{begin+5,y+13},color);draw->AddLine({begin,y+3},{begin,y+row_height-3},color,2);}
            else draw->AddRectFilled({begin,y+4},{end,y+row_height-4},color,3);
            if(composer_block==block->id)draw->AddRect({begin-3,y+2},{std::max(end,begin+3)+3,y+row_height-2},IM_COL32(245,245,250,255),3,0,2);
            if(!marker&&end-begin>35){ImVec4 clip_rect{begin,y,end,y+row_height};draw->AddText(nullptr,0,{begin+4,y+3},IM_COL32(255,255,255,255),block->key.c_str(),nullptr,0,&clip_rect);}
            auto mouse=ImGui::GetMousePos();
            if(ImGui::IsItemHovered()&&mouse.x>=begin-5&&mouse.x<=std::max(end,begin+5)&&mouse.y>=y&&mouse.y<y+row_height){
                ImGui::SetTooltip("%s | block %u\n%.2f - %.2f ticks",block->key.c_str(),block->id,double(block->begin)/action_tick_units,double(block->end)/action_tick_units);
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){composer_playing=false;composer_block=block->id;composer_tick=double(block->begin)/action_tick_units;hit=true;
                    composer_dragging=true;composer_drag_changed=false;composer_drag_block=*block;composer_drag_asset=asset;composer_drag_x=mouse.x;
                    composer_drag_edge=marker?0:std::abs(mouse.x-begin)<6?1:std::abs(mouse.x-end)<6?2:0;
                    composer_drag_revision=controller.authoring.revision();}
            }
        }
    }
    if(composer_dragging){
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            controller.authoring.record_changes(*controller.assets,"Finish action gesture");composer_dragging=false;
        }else if(ImGui::IsMouseDragging(ImGuiMouseButton_Left,3.f)){
            const auto& original=composer_drag_block;
            const auto delta=std::llround(double(ImGui::GetMousePos().x-composer_drag_x)/span*action.duration);
            unsigned begin=original.begin,end=original.end;
            if(composer_drag_edge==1)begin=static_cast<unsigned>(std::clamp<long long>(static_cast<long long>(original.begin)+delta,0,original.end-1));
            else if(composer_drag_edge==2)end=static_cast<unsigned>(std::clamp<long long>(static_cast<long long>(original.end)+delta,original.begin+1,action.duration));
            else{
                const auto length=original.end-original.begin;
                const auto maximum=original.begin==original.end?action.duration-1:action.duration-length;
                begin=static_cast<unsigned>(std::clamp<long long>(static_cast<long long>(original.begin)+delta,0,maximum));end=begin+length;
            }
            try{
                if(controller.authoring.revision()!=composer_drag_revision)throw std::runtime_error("Gesture changed elsewhere; start again");
                const auto before=controller.authoring.revision();
                controller.authoring.set_action_block_times(*controller.assets,asset,original.id,begin,end,true);
                composer_drag_revision=controller.authoring.revision();composer_drag_changed|=before!=composer_drag_revision;composer_tick=double(begin)/action_tick_units;
            }catch(const std::exception& error){composer_dragging=false;author_diagnostic=error.what();controller.log(error.what(),ConsoleSeverity::Error);}
        }
    }
    if(!hit&&!composer_dragging&&ImGui::IsItemHovered()&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){composer_playing=false;composer_tick=std::clamp(double(ImGui::GetMousePos().x-origin.x-label_width)/span*duration,0.,duration);}
    float cursor=x(composer_tick);draw->AddLine({cursor,origin.y+20},{cursor,origin.y+height},IM_COL32(235,235,245,255),2);
    ImGui::Text("Cursor %.2f / %.2f ticks | %u loop(s)",composer_tick,duration,action.loops);
}
void Shell::cancel_composer_drag(Controller& controller){
    try{if(composer_drag_changed)controller.authoring.cancel_edit(*controller.assets,composer_drag_revision,"Drag action block "+std::to_string(composer_drag_block.id));}
    catch(const std::exception& error){author_diagnostic=error.what();controller.log(error.what(),ConsoleSeverity::Error);}
    composer_dragging=false;composer_drag_changed=false;
}
bool Shell::PreviewRefresh::due(const std::string& wanted,bool stale,bool immediate){
    if(forced){forced=false;pending=wanted;return true;}
    if(!stale){pending.clear();return false;}
    if(wanted==failed)return false;
    const double now=ImGui::GetTime();
    if(wanted!=pending){pending=wanted;since=now;}
    if(immediate)return true;
    // Wait for the author to pause: no active field, drag or held button for a moment.
    return now-since>=.4&&!ImGui::IsAnyItemActive()&&!ImGui::IsMouseDown(ImGuiMouseButton_Left);
}
void Shell::preview_status(PreviewRefresh& refresh,bool stale,const char* refresh_id){
    if(!refresh.error.empty())ImGui::TextColored(attention,"Preview needs attention: %s",refresh.error.c_str());
    else ImGui::TextDisabled(stale?"Updating preview...":"Live preview of the current draft.");
    ImGui::SameLine();ImGui::PushID(refresh_id);
    if(ImGui::SmallButton("Refresh")){refresh.forced=true;if(forget_previews)forget_previews();}
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Prepare the preview again from the current drafts and source files.");
    ImGui::PopID();
}
void Shell::draw_action_preview(const nlohmann::json& source){
    if(!source.contains("motion")){ImGui::TextDisabled("This action has no clip binding.");return;}
    if(!prepare_composer){ImGui::TextDisabled("Clip preview requires a configured character scene.");return;}
    auto clip=AssetId::parse(source.at("motion").at("clip").get<std::string>());
    const auto duration=source.at("duration").get<unsigned>();
    const auto hash=sha256(source.dump());
    const bool target=composer_clip==clip&&composer_duration==duration&&composer_preview_action==composer_asset;
    const bool stale=!target||composer_preview_hash!=hash;
    const auto wanted=composer_asset.text()+"|"+hash;
    if(!previews_paused&&action_refresh.due(wanted,stale,!target)){
        try{if(prepare_action_pose)prepare_action_pose(composer_asset);else prepare_composer(clip,duration);composer_clip=clip;composer_duration=duration;composer_preview_action=composer_asset;composer_preview_hash=hash;composer_error.clear();action_refresh.succeeded();}
        catch(const std::exception& error){composer_error=error.what();action_refresh.fail(wanted,error.what());}
    }
    if(previews_paused){ImGui::TextDisabled("Previews pause during Play. Stop Play to see this action.");return;}
    preview_status(action_refresh,stale,"action-preview");
    if(composer_clip!=clip||composer_duration!=duration){ImGui::TextWrapped("Choose a compatible clip for this action to see its preview.");return;}
    draw_pose_preview();
}
void Shell::draw_pose_preview(){
    const auto duration=composer_duration;
    try{
        auto image=[&]{
            const auto scale=ImGui::GetStyle().FontScaleDpi;
            const float height=workspace==Workspace::Character||pose_preview_fill?std::max(80.f*scale,ImGui::GetContentRegionAvail().y-120.f*scale):170.f*scale;
            auto size=ImVec2(std::max(1.f,ImGui::GetContentRegionAvail().x),height);
            const auto pos=ImGui::GetCursorScreenPos();area={pos.x,pos.y,size.x,size.y};
            ImGui::Image(composer_texture,size);
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Right)){
                const auto delta=ImGui::GetIO().MouseDelta;composer_yaw=std::remainder(composer_yaw+delta.x*.01f,6.2831853f);composer_pitch=std::clamp(composer_pitch+delta.y*.01f,-.4f,.6f);
            }
            ImGui::TextDisabled("Right-drag preview to orbit.");ImGui::SliderFloat("Preview distance",&composer_distance,1.5f,8.f,"%.1f m");
            if(composer_info)ImGui::TextWrapped("%s",composer_info().c_str());
        };
        const bool graph=graph_preview_asset!=AssetId{}&&graph_preview_asset==authored_selection;
        if(graph){scrub_composer(composer_tick);composer_visible=true;image();}
        if(ImGui::Button(composer_playing?"Pause preview":"Play preview")){if(composer_tick>=double(duration)/action_tick_units)composer_tick=0;composer_playing=!composer_playing;}
        ImGui::SameLine();if(ImGui::Button("Reset preview")){composer_tick=0;composer_playing=false;}
        ImGui::SameLine();ImGui::Checkbox("Loop preview",&composer_loop);ImGui::SliderFloat("Preview rate",&composer_rate,.1f,2.f,"%.2fx");
        if(graph){if(draw_graph_info)draw_graph_info();}
        else{scrub_composer(composer_tick);composer_visible=true;image();}
    }catch(const std::exception& error){composer_error=error.what();ImGui::TextWrapped("Preview needs attention: %s",error.what());}
}

}
