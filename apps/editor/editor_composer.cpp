#include "editor_ui.hpp"
#include "editor_theme.hpp"
#include "composer_timing.hpp"
#include <darkangel/action.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <fstream>

namespace darkangel::editor_app {
void Shell::draw_action_lanes(Controller& controller,AssetId asset,const nlohmann::json& source){
    if(composer_dragging&&(composer_drag_asset!=asset||ImGui::GetIO().AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||!ImGui::IsMousePosValid()))cancel_composer_drag(controller);
    const auto composer_name=std::filesystem::path(controller.authoring.drafts.at(asset).asset.path).stem().string();ImGui::SeparatorText(composer_name.c_str());
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
    ImGui::TextDisabled("Drag blocks or edges | right-click for commands | Escape cancels.");
    const bool add_command=ImGui::Button("+ Add track / block");
    if(ImGui::Button(composer_playing?"Pause":"Play")){if(composer_tick>=duration)composer_tick=0;composer_playing=!composer_playing;}ImGui::SameLine();if(ImGui::Button("Restart")){composer_tick=0;composer_playing=false;}ImGui::SameLine();if(ImGui::Button("Step")){composer_tick=std::min(composer_tick+1.,duration);composer_playing=false;}
    ImGui::SetNextItemWidth(140);ImGui::SliderFloat("Timeline zoom",&composer_zoom,1.f,4.f,"%.2fx",ImGuiSliderFlags_NoInput);
    if(ImGui::SliderScalar("Scrub tick",ImGuiDataType_Double,&composer_tick,&composer_zero,&duration,"%.2f"))composer_playing=false;
    std::map<unsigned,std::vector<const ActionBlock*>> tracks;
    for(const auto& block:action.blocks)tracks[block.track].push_back(&block);
    const float label_width=110.f,row_height=32.f,ruler_height=28.f;
    ImGui::BeginChild("Scrollable action tracks",{0,std::max(140.f,ImGui::GetContentRegionAvail().y-35.f)},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
    const float width=std::max(500.f,ImGui::GetContentRegionAvail().x)*std::max(1.f,composer_zoom);
    const float span=width-label_width-8.f;
    const float height=std::max(140.f,ruler_height+row_height*static_cast<float>(tracks.size()+unsigned(source.contains("motion"))));
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Action lanes",{width,height});
    const bool timeline_hovered=ImGui::IsItemHovered();
    auto* draw=ImGui::GetWindowDrawList();
    const double visible_duration=std::max(1.,duration);
    const auto x=[&](double tick){return origin.x+label_width+float(tick/visible_duration)*span;};
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(theme::background),4.f);
    const unsigned step=std::max(1u,unsigned(std::ceil(visible_duration/8.)));
    for(unsigned tick=0;tick<=unsigned(visible_duration);tick+=step){
        float px=x(tick);draw->AddLine({px,origin.y+20},{px,origin.y+height},ImGui::GetColorU32(theme::border));
        auto label=std::to_string(tick);draw->AddText({px+2,origin.y+3},ImGui::GetColorU32(theme::secondary),label.c_str());
    }
    bool hit=false,surface_context=false;unsigned row{},context_block{};ActionBlock context_copy{};
    if(source.contains("motion")){const float y=origin.y+ruler_height;row=1;draw->AddText({origin.x+5,y+3},ImGui::GetColorU32(theme::secondary),"Animation");draw->AddRectFilled({x(0),y+4},{x(duration),y+row_height-4},IM_COL32(55,115,175,255),3);const auto clip=std::filesystem::path(source.at("motion").at("source").get<std::string>()).stem().string();draw->AddText({x(0)+5,y+3},IM_COL32(240,245,255,255),clip.c_str());}
    for(const auto& [track,blocks]:tracks){
        float y=origin.y+ruler_height+row_height*float(row++);
        auto label="Track "+std::to_string(track);draw->AddText({origin.x+5,y+3},ImGui::GetColorU32(theme::secondary),label.c_str());
        for(const auto* block:blocks){
            float begin=x(double(block->begin)/action_tick_units),end=x(double(block->end)/action_tick_units);
            const bool marker=block->begin==block->end;
            ImU32 color=block->kind==ActionBlockKind::HitWindow||block->kind==ActionBlockKind::Hitbox?IM_COL32(185,85,65,255):block->kind==ActionBlockKind::ComboWindow?IM_COL32(55,155,95,255):block->kind==ActionBlockKind::Commit?IM_COL32(220,172,55,255):block->kind==ActionBlockKind::Vfx?IM_COL32(155,74,190,255):block->kind==ActionBlockKind::Audio?IM_COL32(190,115,45,255):block->kind==ActionBlockKind::Projectile||block->kind==ActionBlockKind::Aoe?IM_COL32(45,155,125,255):block->kind==ActionBlockKind::Gameplay?IM_COL32(75,145,75,255):IM_COL32(55,105,165,255);
            if(marker){draw->AddTriangleFilled({begin,y+3},{begin-5,y+13},{begin+5,y+13},color);draw->AddLine({begin,y+3},{begin,y+row_height-3},color,2);}
            else draw->AddRectFilled({begin,y+4},{end,y+row_height-4},color,3);
            if(composer_block==block->id)draw->AddRect({begin-3,y+2},{std::max(end,begin+3)+3,y+row_height-2},IM_COL32(245,245,250,255),3,0,2);
            if(!marker&&end-begin>35){ImVec4 clip_rect{begin,y,end,y+row_height};draw->AddText(nullptr,0,{begin+4,y+3},IM_COL32(255,255,255,255),block->key.c_str(),nullptr,0,&clip_rect);}
            auto mouse=ImGui::GetMousePos();
            if(ImGui::IsItemHovered()&&mouse.x>=begin-5&&mouse.x<=std::max(end,begin+5)&&mouse.y>=y&&mouse.y<y+row_height){
                ImGui::SetTooltip("%s | block %u\n%.2f - %.2f ticks",block->key.c_str(),block->id,double(block->begin)/action_tick_units,double(block->end)/action_tick_units);
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)){context_block=block->id;context_copy=*block;}
                if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)){composer_playing=false;composer_block=block->id;composer_tick=double(block->begin)/action_tick_units;hit=true;
                    composer_dragging=true;composer_drag_changed=false;composer_drag_block=*block;composer_drag_asset=asset;composer_drag_x=mouse.x;
                    composer_drag_edge=marker?0:std::abs(mouse.x-begin)<6?1:std::abs(mouse.x-end)<6?2:0;
                    composer_drag_revision=controller.authoring.revision();}
            }
        }
    }
    surface_context=timeline_hovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&context_block==0;
    if(context_block){composer_context_block=context_copy;composer_context_asset=asset;ImGui::OpenPopup("Composer block context");}else if(surface_context||add_command)ImGui::OpenPopup("Composer timeline context");
    if(ImGui::BeginPopup("Composer block context")){
        const auto selected_context=composer_context_asset==asset?composer_context_block:ActionBlock{};composer_block=selected_context.id;
        if(ImGui::MenuItem("Copy block"))composer_clipboard=selected_context;
        if(ImGui::MenuItem("Duplicate block"))try{auto copy=selected_context;copy.id=0;copy.track=std::min(copy.track+1,31u);controller.authoring.add_action_block(*controller.assets,asset,copy);}catch(const std::exception& error){author_diagnostic=error.what();controller.log(error.what(),ConsoleSeverity::Error);}
        if(ImGui::MenuItem("Delete block"))try{controller.authoring.remove_action_block(*controller.assets,asset,selected_context.id);composer_block=0;}catch(const std::exception& error){author_diagnostic=error.what();controller.log(error.what(),ConsoleSeverity::Error);}
        ImGui::EndPopup();
    }
    if(ImGui::BeginPopup("Composer timeline context")){
        const auto add=[&](ActionBlock block){try{composer_block=controller.authoring.add_action_block(*controller.assets,asset,block);}catch(const std::exception& error){author_diagnostic=error.what();controller.log(error.what(),ConsoleSeverity::Error);}};
        if(ImGui::BeginMenu("Add track")){const auto track=tracks.empty()?0u:std::min(tracks.rbegin()->first+1,31u);const auto begin=std::min(static_cast<unsigned>(composer_tick*action_tick_units),action.duration-action_tick_units);if(ImGui::MenuItem("Animation / Cue"))add({0,track,begin,begin,ActionBlockKind::Cue,"Animation.New"});if(ImGui::MenuItem("VFX track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Vfx,"VFX.New"});if(ImGui::MenuItem("Audio track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Audio,"Audio.New"});if(ImGui::MenuItem("Combo window track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::ComboWindow,"Combo.Next"});if(ImGui::MenuItem("Movement lock track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::MovementLock,"Motor.Root"});if(ImGui::MenuItem("Invulnerability track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Invulnerability,"Invulnerable.Window"});if(ImGui::MenuItem("Gameplay effect track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units),ActionBlockKind::Gameplay,"Gameplay.New"});if(ImGui::MenuItem("Hitbox track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units),ActionBlockKind::Hitbox,"Hitbox.New"});if(ImGui::MenuItem("Projectile track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Projectile,"Projectile.New"});if(ImGui::MenuItem("AoE track"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Aoe,"AoE.New"});ImGui::EndMenu();}
        if(ImGui::BeginMenu("Add block")){const auto track=tracks.empty()?0u:tracks.begin()->first;const auto begin=std::min(static_cast<unsigned>(std::clamp(composer_tick,0.,duration)*action_tick_units),action.duration-action_tick_units);if(ImGui::MenuItem("Cue"))add({0,track,begin,begin,ActionBlockKind::Cue,"Cue.New"});if(ImGui::MenuItem("VFX"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Vfx,"VFX.New"});if(ImGui::MenuItem("Audio"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Audio,"Audio.New"});if(ImGui::MenuItem("Combo window"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::ComboWindow,"Combo.Next"});if(ImGui::MenuItem("Movement lock"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::MovementLock,"Motor.Root"});if(ImGui::MenuItem("Invulnerability"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Invulnerability,"Invulnerable.Window"});if(ImGui::MenuItem("Gameplay effect event"))add({0,track,begin,std::min(action.duration,begin+action_tick_units),ActionBlockKind::Gameplay,"Gameplay.New"});if(ImGui::MenuItem("Hitbox"))add({0,track,begin,std::min(action.duration,begin+action_tick_units),ActionBlockKind::Hitbox,"Hitbox.New"});if(ImGui::MenuItem("Projectile"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Projectile,"Projectile.New"});if(ImGui::MenuItem("AoE"))add({0,track,begin,std::min(action.duration,begin+action_tick_units*12),ActionBlockKind::Aoe,"AoE.New"});if(ImGui::MenuItem("Commit marker"))add({0,track,begin,begin,ActionBlockKind::Commit,"Ability.Commit"});ImGui::EndMenu();}
        if(ImGui::MenuItem("Paste block",nullptr,false,composer_clipboard.has_value())){auto copy=*composer_clipboard;copy.id=0;copy.begin=std::min(copy.begin,action.duration);copy.end=std::min(std::max(copy.end,copy.begin),action.duration);add(copy);}
        ImGui::EndPopup();
    }
    if(composer_dragging){
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            controller.authoring.record_changes(*controller.assets,"Finish action gesture");composer_dragging=false;
        }else if(ImGui::IsMouseDragging(ImGuiMouseButton_Left,3.f)){
            const auto& original=composer_drag_block;
            const auto delta=composer_drag_units(double(ImGui::GetMousePos().x-composer_drag_x),span,visible_duration);
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
    ImGui::EndChild();
    ImGui::Text("Cursor %.2f / %.2f ticks | %u loop(s)",composer_tick,duration,action.loops);
}
namespace {
using Json=nlohmann::json;
void block_number(const char* label,Json& field){double value=field.get<double>();if(ImGui::InputDouble(label,&value,0,0,"%.3f"))field=value;}
void draw_hit_properties(Json& hit){
 ImGui::SeparatorText("Sphere hitbox");ImGui::TextWrapped("Authority resolves socket sweeps; once per target per action loop. Only sphere geometry is currently supported.");
 block_number("Radius (m)",hit["radius"]);block_number("Power",hit["power"]);
 for(unsigned axis=0;axis<3;++axis){ImGui::PushID(axis);block_number(axis==0?"Offset X":axis==1?"Offset Y":"Offset Z",hit["offset"][axis]);ImGui::PopID();}
 char socket[65]{};auto name=hit["socket"].get<std::string>();std::copy_n(name.begin(),std::min(name.size(),sizeof(socket)-1),socket);if(ImGui::InputText("Socket",socket,sizeof(socket)))hit["socket"]=socket;
 for(auto field:{"evaluator","damage_type"}){int value=hit[field].get<int>();if(ImGui::InputInt(field,&value))hit[field]=static_cast<unsigned>(std::max(1,value));}
}
void draw_projectile_properties(Controller& controller,Json& source,Json& block){
 auto& shot=block["projectile"];ImGui::SeparatorText("Projectile motion");for(auto field:{"speed","gravity","range"})block_number(field,shot[field]);int lifetime=shot["lifetime"].get<int>();if(ImGui::InputInt("Lifetime (ticks)",&lifetime))shot["lifetime"]=static_cast<unsigned>(std::clamp(lifetime,1,600));
 const auto model=shot.value("model_source",std::string("No model"));
 if(ImGui::BeginCombo("Projectile model",model.c_str())){for(const auto& candidate:controller.authoring.inventory(*controller.assets)){const auto ext=std::filesystem::path(candidate.path).extension();if(ext!=".glb"&&ext!=".gltf")continue;auto meta=controller.assets->source_root()/candidate.path;meta+=".daimport";std::ifstream input(meta);if(!input)continue;auto metadata=Json::parse(input,nullptr,false);if(!metadata.is_object()||metadata.value("importer","")!="static-gltf-v1")continue;if(ImGui::Selectable(candidate.path.c_str(),shot.value("visual_model",std::string{})==candidate.id.text())){shot["visual_model"]=candidate.id.text();shot["model_source"]=candidate.path;}}ImGui::EndCombo();}
 if(shot.contains("visual_model")&&ImGui::SmallButton("Clear projectile visual")){shot.erase("visual_model");shot.erase("model_source");shot.erase("preparation");}
 bool prepared=shot.contains("preparation");ImGui::BeginDisabled(!shot.contains("visual_model"));if(ImGui::Checkbox("Prepare visual before release",&prepared)){if(prepared){std::vector<unsigned> visuals;for(const auto& b:source["blocks"])if(b["kind"]=="vfx"&&b["end"]<=block["begin"])visuals.push_back(b["id"].get<unsigned>());shot["preparation"]={{"emergence_block",visuals.empty()?0u:visuals[0]},{"move_block",visuals.size()<2?0u:visuals[1]},{"release_block",block["id"]},{"offset",{0.,0.,.5}},{"rise",.6},{"socket","weapon_right"},{"ready_pitch_degrees",-45.}};}else shot.erase("preparation");}ImGui::EndDisabled();
 if(shot.contains("preparation")){auto& prep=shot["preparation"];for(const auto* field:{"emergence_block","move_block"}){std::string name="Choose VFX window";for(const auto& b:source["blocks"])if(b["id"]==prep[field])name=b["key"].get<std::string>();if(ImGui::BeginCombo(field,name.c_str())){for(const auto& b:source["blocks"])if(b["kind"]=="vfx"&&b["end"]<=block["begin"]&&ImGui::Selectable(b["key"].get<std::string>().c_str(),b["id"]==prep[field]))prep[field]=b["id"];ImGui::EndCombo();}}for(unsigned axis=0;axis<3;++axis){ImGui::PushID(axis);block_number(axis==0?"Ground X":axis==1?"Ground Y":"Ground Z",prep["offset"][axis]);ImGui::PopID();}block_number("Rise (m)",prep["rise"]);block_number("Ready pitch",prep["ready_pitch_degrees"]);char socket[65]{};const auto name=prep["socket"].get<std::string>();std::copy_n(name.begin(),std::min(name.size(),sizeof(socket)-1),socket);if(ImGui::InputText("Preparation socket",socket,sizeof(socket)))prep["socket"]=socket;ImGui::TextWrapped("Release is this block. Emergence and move windows must be ordered before release.");}
 ImGui::TextWrapped("One authoritative launch per block and loop. Sphere collision stops at the first actor or wall. Flight outlives the action until impact, range or lifetime ends it.");
}
}
void Shell::draw_action_block_details(Controller& controller,AssetId asset){
 auto& source=controller.authoring.open(*controller.assets,asset).value;
 auto& blocks=source["blocks"];auto found=std::find_if(blocks.begin(),blocks.end(),[&](const auto& block){return block["id"]==composer_block;});
 if(found==blocks.end()){ImGui::TextWrapped("Select a timeline block to edit its timing and typed configuration.");return;}
 ImGui::BeginDisabled(controller.preview.active()!=nullptr);auto& block=*found;const auto kind=block["kind"].get<std::string>();
 ImGui::Text("Block %u / %s",composer_block,block["kind"].get<std::string>().c_str());
 char label[65]{};auto key=block["key"].get<std::string>();std::copy_n(key.begin(),std::min(key.size(),sizeof(label)-1),label);if(ImGui::InputText(kind=="cue"||kind=="vfx"||kind=="audio"?"Cue key":"Name",label,sizeof(label)))block["key"]=label;
 if((kind=="cue"||kind=="vfx"||kind=="audio")&&ImGui::BeginCombo("Presentation asset","Choose registered cue")){for(const auto& candidate:controller.authoring.inventory(*controller.assets))if(std::filesystem::path(candidate.path).extension()==".dacue"&&ImGui::Selectable(candidate.path.c_str())){const auto& cue=controller.authoring.open(*controller.assets,candidate.id).value;block["key"]=cue.at("key");}ImGui::EndCombo();}
 for(auto field:{"begin","end"}){double tick=block[field].get<double>()/action_tick_units;if(ImGui::InputDouble(field,&tick,1,5,"%.2f ticks"))block[field]=static_cast<unsigned>(std::clamp(tick,0.,double(source["duration"].get<unsigned>())/action_tick_units)*action_tick_units);}
 int track=block["track"].get<int>();if(ImGui::InputInt("Track",&track))block["track"]=static_cast<unsigned>(std::clamp(track,0,31));
 if(kind=="hit"||kind=="hitbox"||kind=="projectile"||kind=="aoe"){
  if(!block.contains("hit")){ImGui::TextWrapped("Legacy hit window: gameplay may still be owned by the ability. Migrate its profile before adding composer data to avoid dual ownership.");if(ImGui::Button("Add composer-owned sphere"))block["hit"]={{"schema",1},{"shape","sphere"},{"offset",{0.,1.,.8}},{"radius",.5},{"power",0.},{"evaluator",1u},{"damage_type",1u},{"socket",""}};}
  if(block.contains("hit"))draw_hit_properties(block["hit"]);
  if(block.contains("projectile"))draw_projectile_properties(controller,source,block);
  if(block.contains("area")){int period=block["area"]["period_ticks"].get<int>();if(ImGui::InputInt("Pulse interval (ticks)",&period))block["area"]["period_ticks"]=static_cast<unsigned>(std::clamp(period,1,600));ImGui::TextWrapped("Attached spherical aura. Hits hostile actors once per pulse while the window is active; interruption ends further pulses.");}
 }else if(kind=="gameplay")ImGui::TextWrapped("Gameplay effect event: applies the selected effects to the caster once when the block begins. Effects keep their authored lifetime; this interval does not repeat their execution.");
 else if(kind=="combo")ImGui::TextWrapped("Input advances the ability's linear continuation while this window is active. The same ability keeps cost and cooldown identity.");
 else if(kind=="cue"||kind=="vfx"||kind=="audio")ImGui::TextWrapped("The typed cue registry resolves this key to its existing visual/audio presentation. VFX intervals receive end/cancellation cleanup.");
 else if(kind=="commit")ImGui::TextWrapped("Commit marker. Select deferred commitment in Ability Details to reserve costs until this marker.");
 else ImGui::TextWrapped("This interval is consumed by the existing action/motor or registered action-tag policy.");
 if(block.contains("hit")||kind=="gameplay"){
  ImGui::SeparatorText(kind=="gameplay"?"Self effects":"On-hit effects");
  if(kind!="gameplay")ImGui::TextWrapped("Applied by authority to a surviving target after positive evaluated damage. The effect asset owns status duration, stacking, modifiers and presentation.");
  const auto count=block.contains("effects")?block["effects"]["entries"].size():0u;
  if(block.contains("effects"))for(unsigned index=0;index<block["effects"]["entries"].size();++index){auto& effect=block["effects"]["entries"][index];ImGui::PushID(static_cast<int>(index));ImGui::TextWrapped("%s",effect.at("source").get<std::string>().c_str());block_number("Effect power",effect["power"]);if(ImGui::SmallButton("Remove effect")){block["effects"]["entries"].erase(index);ImGui::PopID();break;}ImGui::PopID();}
  if(count<8&&ImGui::BeginCombo("Add effect","Select effect asset")){for(const auto& candidate:controller.authoring.inventory(*controller.assets)){if(std::filesystem::path(candidate.path).extension()!=".daeffect")continue;bool duplicate=false;if(block.contains("effects"))for(const auto& existing:block["effects"]["entries"])duplicate|=existing["asset"]==candidate.id.text();if(ImGui::Selectable(candidate.path.c_str(),false,duplicate?ImGuiSelectableFlags_Disabled:0)){if(!block.contains("effects"))block["effects"]={{"schema",1},{"entries",nlohmann::json::array()}};block["effects"]["entries"].push_back({{"asset",candidate.id.text()},{"source",candidate.path},{"power",1.}});}}ImGui::EndCombo();}
  if(kind=="gameplay"&&count==0)ImGui::TextColored({1,.65f,.25f,1},"No effect assigned: this event is inert.");
 }
 try{authoring_action(source);}catch(const std::exception& error){ImGui::TextColored({1,.5f,.25f,1},"%s",error.what());}
 ImGui::EndDisabled();
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
            const float height=workspace==Workspace::Character||pose_preview_fill?std::max(80.f*scale,ImGui::GetContentRegionAvail().y-210.f*scale):170.f*scale;
            auto size=ImVec2(std::max(1.f,ImGui::GetContentRegionAvail().x),height);
            const auto pos=ImGui::GetCursorScreenPos();area={pos.x,pos.y,size.x,size.y};
            ImGui::Image(composer_texture,size);
            if(composer_overlay)composer_overlay(ImGui::GetForegroundDrawList(),area);
            if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Right)){
                const auto delta=ImGui::GetIO().MouseDelta;composer_yaw=std::remainder(composer_yaw+delta.x*.01f,6.2831853f);composer_pitch=std::clamp(composer_pitch+delta.y*.01f,-.4f,.6f);
            }
            ImGui::TextDisabled("Right-drag preview to orbit.");ImGui::SliderFloat("Preview distance",&composer_distance,1.5f,8.f,"%.1f m");
            if(composer_info)ImGui::TextWrapped("%s",composer_info().c_str());
        };
        const bool graph=graph_preview_asset!=AssetId{}&&graph_preview_asset==authored_selection;
        auto controls=[&]{
            if(ImGui::Button(composer_playing?"Pause preview":"Play preview")){if(composer_tick>=double(duration)/action_tick_units)composer_tick=0;composer_playing=!composer_playing;}
            ImGui::SameLine();if(ImGui::Button("Step tick")){composer_playing=false;composer_tick=std::min(composer_tick+1.,double(duration)/action_tick_units);}
            ImGui::SameLine();if(ImGui::Button("Reset preview")){composer_tick=0;composer_playing=false;}
            ImGui::SameLine();ImGui::Checkbox("Loop preview",&composer_loop);ImGui::SliderFloat("Preview rate",&composer_rate,.1f,2.f,"%.2fx");
        };
        if(graph){scrub_composer(composer_tick);composer_visible=true;image();controls();if(draw_graph_info)draw_graph_info();}
        else if(pose_preview_fill||workspace==Workspace::Character){scrub_composer(composer_tick);composer_visible=true;image();if(draw_composer_effects)draw_composer_effects(composer_tick);controls();}
        else{controls();scrub_composer(composer_tick);composer_visible=true;image();if(draw_composer_effects)draw_composer_effects(composer_tick);}
    }catch(const std::exception& error){composer_error=error.what();ImGui::TextWrapped("Preview needs attention: %s",error.what());}
}

}
