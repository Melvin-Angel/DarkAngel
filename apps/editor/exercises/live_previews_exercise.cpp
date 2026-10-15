#include "live_previews_exercise.hpp"
#include "../editor_ui.hpp"
#include <darkangel/action.hpp>
#include <darkangel/check.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
namespace darkangel::editor_app::exercises {
namespace {
// Opens each authoring tab the way an author does and requires its preview to be ready without a Prepare click,
// then edits an action draft and requires the visible preview to follow it. Every edit is undone again. Returning to
// Character afterwards must show its kept preview within a few frames, without preparing it again.
class LivePreviews final:public Exercise {
    bool enabled_,done_{};Controller& editor;Shell& shell;
    int stage{};unsigned since{};AssetId character,action,effect,graph,player;
    // Open-to-ready wall time of each preview, reported with the result.
    std::chrono::steady_clock::time_point opened{};double character_ms{},action_ms{},effect_ms{},graph_ms{},revisit_ms{};
    double elapsed()const{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-opened).count();}
    void open(AssetId id){opened=std::chrono::steady_clock::now();shell.open_native_asset(editor,id);}
    AssetId first(const char* extension,bool motion=false){
        for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==extension){
            if(!motion||editor.authoring.open(*editor.assets,asset.id).value.contains("motion"))return asset.id;}
        return {};
    }
    // Moves to the next stage once ready holds, or fails after a bounded wait (previews refresh after a pause in editing).
    // The bound is both frames and wall time: fast hidden frames must not run out before the 0.4 s edit pause has passed.
    std::chrono::steady_clock::time_point stage_started=std::chrono::steady_clock::now();
    void until(bool ready,unsigned frame,const char* failure){if(ready){++stage;since=frame;stage_started=std::chrono::steady_clock::now();return;}
        require(frame-since<240||std::chrono::steady_clock::now()-stage_started<std::chrono::seconds(5),failure);}
public:
    LivePreviews(bool enabled,Controller& e,Shell& s):enabled_(enabled),editor(e),shell(s){}
    void frame(unsigned frame)override{
        if(!enabled_||done_)return;
        switch(stage){
        case 0:
            if(frame<3)return;
            require(editor.assets&&shell.prepare_character_pose,"Live previews need project sources and the character scene");
            character=first(".dacharacter");action=first(".daaction",true);effect=first(".daeffect");graph=first(".dagraph");player=first(".daplayer");
            require(character!=AssetId{}&&action!=AssetId{}&&effect!=AssetId{}&&graph!=AssetId{}&&player!=AssetId{},"Live previews need character, action, effect, graph and player fixtures");
            shell.character_clip={};open(character);++stage;since=frame;stage_started=std::chrono::steady_clock::now();return;
        case 1:
            until(shell.workspace==Workspace::Character&&shell.character_preview_asset==character&&shell.character_clip!=AssetId{}&&shell.composer_duration,frame,"Character preview did not prepare on opening the tab");
            if(stage==2){character_ms=elapsed();open(action);}
            return;
        case 2:
            until(shell.workspace==Workspace::Animation&&shell.composer_preview_action==action&&shell.composer_visible,frame,"Action preview did not prepare on opening the Animation tab");
            // A cue block is the smallest valid timeline edit: it changes the draft without touching the clip binding or duration.
            if(stage==3){action_ms=elapsed();const auto& value=editor.authoring.open(*editor.assets,action).value;unsigned track{};for(const auto& block:value.at("blocks"))track=std::max(track,block.at("track").get<unsigned>());
                editor.authoring.add_action_block(*editor.assets,action,{0,std::min(track+1,31u),0,0,ActionBlockKind::Cue,"Cue.New"});}
            return;
        case 3:
            until(shell.composer_preview_hash==sha256(editor.authoring.open(*editor.assets,action).value.dump()),frame,
                  ("Action preview did not follow the edited draft: "+(shell.action_refresh.error.empty()?std::string("no preparation error"):shell.action_refresh.error)).c_str());
            if(stage==4){editor.authoring.undo(*editor.assets);open(effect);}
            return;
        case 4:
            until(shell.workspace==Workspace::Ability&&shell.effect_preview&&shell.effect_preview_asset==effect,frame,"Effect preview did not prepare on opening the Ability tab");
            if(stage==5){effect_ms=elapsed();open(graph);}
            return;
        case 5:
            until(shell.workspace==Workspace::Animation&&shell.graph_preview_asset==graph,frame,"Graph preview did not prepare on opening the graph");
            if(stage==6){graph_ms=elapsed();shell.open_native_asset(editor,player);}
            return;
        case 6:
            until(shell.workspace==Workspace::Player&&shell.camera_preview_visible,frame,"Player camera preview is not shown by default");
            if(stage==7)open(character);
            return;
        case 7:
            // The Character preview was replaced by the action and graph previews; it is kept, so it returns at once.
            require(frame-since<=3,"Returning to Character prepared its preview again instead of showing the kept one");
            until(shell.workspace==Workspace::Character&&shell.character_preview_asset==character&&shell.composer_duration,frame,"Character preview did not return");
            if(stage==8){
                revisit_ms=elapsed();
                require(!editor.authoring.dirty()&&!editor.preview.active(),"Live previews changed a source or started Play");
                done_=true;std::cout<<"Editor live_previews character=1 action=1 action_follows_edit=1 effect=1 graph=1 camera=1 source=unchanged revisit=1"
                    <<" open_ms character="<<character_ms<<" action="<<action_ms<<" effect="<<effect_ms<<" graph="<<graph_ms<<" revisit="<<revisit_ms<<"\n";}
            return;
        }
    }
    void finish()override{if(enabled_)require(done_,"Live previews exercise ended at stage "+std::to_string(stage));}
    bool done()const override{return !enabled_||done_;}
    std::string_view result()const override{return {};}
};
}
std::unique_ptr<Exercise> make_live_previews(bool enabled,Controller& e,Shell& s){return std::make_unique<LivePreviews>(enabled,e,s);}
}
