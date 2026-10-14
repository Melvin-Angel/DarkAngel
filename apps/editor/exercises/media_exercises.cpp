#include "media_exercises.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../native_authoring.hpp"
#include "../package_file.hpp"
#include "../vfx_workspace.hpp"
#ifdef DAE_AUDIO
#include "../audio_workspace.hpp"
#endif
#include <darkangel/character_scene.hpp>
#include <darkangel/check.hpp>
#include <darkangel/cue_assets.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace darkangel::editor_app::exercises {
namespace {
class MediaExercises final : public Exercise {
    MediaContext context_;

  public:
    explicit MediaExercises(MediaContext context) : context_(context) {}
    void frame(unsigned frame) override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& vfx_preview = context_.vfx.state;
#ifdef DAE_AUDIO
        auto& audio_workspace = *context_.audio;
        auto& audio = audio_workspace.service;
        auto& audio_director = audio_workspace.director;
        auto& audio_applied = audio_workspace.applied;
        auto& audio_tab = audio_workspace.state;
        const auto& audio_mix_path = audio_workspace.mix_path;
        const auto project_mix = [&] { return audio_workspace.project_mix(); };
        const auto ensure_audio = [&] { return audio_workspace.ensure(); };
        const auto load_audio = [&](std::vector<AssetId> wanted) { return audio_workspace.load(std::move(wanted)); };
#endif
        // Stands in for saving the effect in Effekseer: the source file changes on disk while the VFX tab shows it.
#ifdef DAE_AUDIO
        // The Audio tab without Play: a sound definition auditioned, a mixer volume moved in its draft, a whole profile auditioned.
        if(args.exercise_audio&&args.audio_workspace){const auto named=[&](const char* path){for(const auto& asset:editor.asset_inventory)if(asset.path==path)return asset.id;throw std::runtime_error(std::string("The audio exercise needs ")+path);};
            if(frame==3){require(ensure_audio(),"The audio exercise needs the audio service");const auto wind=named("audio/wind.dasound");load_audio({wind});audio_tab.sound=wind;audio_tab.audition=audio->play(wind,{0,0,1});require(audio_tab.audition!=0,"The auditioned sound did not start");}
            if(frame==6){auto& value=editor.authoring.open(*editor.assets,named("audio/project.damix")).value;value["buses"]["music"]["volume"]=0.3;editor.authoring.record_changes(*editor.assets,"Edit audio");}
            if(frame==9){const auto calm=named("audio/profiles/royal_calm.daaudioprofile");auto loaded=load_audio({calm});require(loaded.contains(calm),"The audio profile did not cook");audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();audio_director->scene(loaded.at(calm));audio_tab.profile=calm;audio_tab.profile_audition=true;}}
        // During Play: the NPC bus turned down in the mixer's draft.
        if(args.exercise_audio&&!args.audio_workspace&&frame==20&&editor.assets){for(const auto& asset:editor.asset_inventory)if(asset.path==audio_mix_path){auto& value=editor.authoring.open(*editor.assets,asset.id).value;value["buses"]["npc"]["volume"]=0.25;editor.authoring.record_changes(*editor.assets,"Edit audio");}}
#endif
        // The cue form's own path: a new cue staged as a draft, its fields set the way the form sets them, saved, and read back cooked.
        if(args.exercise_cue_form&&frame==3){require(bool(editor.assets),"The cue form exercise needs the project's sources");vfx_preview.cue=editor.authoring.create_cue(*editor.assets,"gate_"+AssetId::random().text().substr(0,8));editor.refresh_assets();}
        if(args.exercise_cue_form&&frame==6){auto& value=editor.authoring.open(*editor.assets,vfx_preview.cue).value;value["name"]="Gate cue";value["visual"]="vfx/placeholder/hit_spark.efkproj";value["audio"]="audio/placeholder/hit.wav";value["tail_ticks"]=20;editor.authoring.record_changes(*editor.assets,"Edit cue");require(editor.authoring.can_undo()&&editor.authoring.drafts.at(vfx_preview.cue).dirty(),"The cue edit is not an undoable draft");
            editor.authoring.save(*editor.assets,vfx_preview.cue);editor.refresh_assets();}
        if(args.exercise_vfx_edit&&frame==5){require(editor.assets&&!vfx_preview.path.empty(),"The VFX edit exercise needs a selected effect");std::ofstream changed(editor.assets->source_root()/vfx_preview.path,std::ios::binary|std::ios::app);changed<<"\n";require(bool(changed),"The effect source could not be changed");}
    }
    void finish() override {
        const auto& args = context_.options;
        auto& editor = context_.editor;
        auto& shell = context_.shell;
        auto& vfx_preview = context_.vfx.state;
        auto& vfx = context_.vfx.service;
        const auto vfx_effects = [&] { return context_.vfx.effects(); };
        CharacterSceneSession* const& character = context_.session;
#ifdef DAE_AUDIO
        auto& audio_workspace = *context_.audio;
        auto& audio = audio_workspace.service;
        auto& audio_director = audio_workspace.director;
        auto& audio_tab = audio_workspace.state;
#endif
#ifdef DAE_AUDIO
    if(args.exercise_audio){require(bool(audio),"The audio exercise ended without an audio service");const auto heard=audio->stats();const auto held=audio_director?audio_director->stats():darkangel::AudioDirectorStats{};const auto text=[](AssetId id){return id==AssetId{}?std::string("none"):id.text();};
        std::size_t waves{},definitions{},profiles{},mixers{};for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();waves+=extension==".wav";definitions+=extension==".dasound";profiles+=extension==".daaudioprofile";mixers+=extension==".damix";}
        std::cout<<"Editor audio workspace="<<(shell.workspace==darkangel::editor_app::Workspace::Audio?1:0)<<" waves="<<waves<<" definitions="<<definitions<<" profiles="<<profiles<<" mixers="<<mixers<<" driver="<<heard.driver<<" loaded="<<heard.sounds<<" playing="<<heard.playing<<" started="<<heard.started<<" audition="<<(audio->alive(audio_tab.audition)?1:0)
            <<" bus_music="<<heard.bus_gain[1]<<" bus_ambient="<<heard.bus_gain[2]<<" bus_npc="<<heard.bus_gain[4]<<" limit="<<(heard.limit?1:0)<<" director="<<(audio_director?1:0)<<" profile="<<text(held.profile)<<" music="<<text(held.music)<<" ambient="<<held.ambient<<" layers="<<held.layers<<" missing="<<held.missing<<"\n";}
#endif
    if(args.exercise_cue_form){require(bool(editor.assets)&&vfx_preview.cue!=AssetId{},"The cue form exercise did not make a cue");const auto registry=editor.assets->cas_path().parent_path()/("cue-form-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};const std::array roots{vfx_preview.cue};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);const auto cooked=darkangel::load_cooked_cues(registry,editor.assets->cas_path());require(cooked.size()==1,"The saved cue did not cook");
        std::size_t cues{};std::string path;for(const auto& asset:editor.asset_inventory)if(std::filesystem::path(asset.path).extension()==".dacue"){++cues;if(asset.id==vfx_preview.cue)path=asset.path;}
        std::cout<<"Editor cue_form cues="<<cues<<" path="<<path<<" saved="<<(editor.authoring.drafts.at(vfx_preview.cue).dirty()?0:1)<<" tail="<<cooked[0].tail_ticks<<" visual_resolved="<<(cooked[0].visual_asset!=AssetId{}?1:0)<<" audio_resolved="<<(cooked[0].audio_asset!=AssetId{}?1:0)<<"\n";}
    if(args.vfx_workspace){const auto drawn=vfx?vfx->stats():darkangel::VfxStats{};require(shell.workspace==darkangel::editor_app::Workspace::Vfx&&!character,"The VFX tab was left during its exercise");
        std::cout<<"Editor vfx_tab effects="<<vfx_effects().size()<<" selected="<<(vfx_preview.path.empty()?std::string("none"):vfx_preview.path)<<" cooked="<<(vfx_preview.cooked?1:0)<<" cooks="<<vfx_preview.cooks<<" textures="<<vfx_preview.textures<<" playing="<<(vfx&&vfx->alive(vfx_preview.handle)?1:0)<<" particles="<<drawn.instances<<" quads="<<drawn.quads<<" unsupported="<<drawn.unsupported<<" refused="<<(vfx_preview.error.empty()?0:1)<<"\n";if(!vfx_preview.error.empty())std::cout<<"Editor vfx_tab error: "<<vfx_preview.error<<"\n";}
    }
    bool done() const override {
        return true;
    }
    std::string_view result() const override {
        return {};
    }
};
} // namespace
std::unique_ptr<Exercise> make_media_exercises(MediaContext context) {
    return std::make_unique<MediaExercises>(context);
}
} // namespace darkangel::editor_app::exercises
