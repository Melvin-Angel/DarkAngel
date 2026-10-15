#include "play_exercises.hpp"
#include "../editor_options.hpp"
#include "../editor_ui.hpp"
#include "../gameplay_inspector.hpp"
#include "../scene_splines.hpp"
#ifdef DAE_AUDIO
#include "../audio_workspace.hpp"
#endif
#include <darkangel/character_scene.hpp>
#include <darkangel/check.hpp>
#include <darkangel/vfx.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace darkangel::editor_app::exercises {
bool starts_play(const EditorOptions& args) {
    return args.exercise_character || args.exercise_combat || args.exercise_heavy;
}
bool local_party(const EditorOptions& args) {
    return args.exercise_two_players;
}
void select_startup_views(const EditorOptions& args, Shell& shell) {
    if(args.exercise_reaction){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;}if(args.exercise_camera_preview){shell.camera_preview=1;shell.camera_preview_yaw=25;shell.camera_preview_pitch=10;}
}
PlayExercises::PlayExercises(PlayExerciseContext context):context_(std::move(context)){}
void PlayExercises::frame(unsigned frame){
    const auto& args=context_.options;auto& editor=context_.editor;auto& shell=context_.shell;auto* const character=context_.session;const auto& character_resources=context_.resources;
    auto& cue_debug_selected=context_.debug.select_cues;auto& senses_all=context_.debug.senses_all;auto& world_debug=context_.debug.world_debug;const auto& first_actor=context_.first_actor;
        if(args.exercise_cues){shell.focus_gameplay_inspector=true;cue_debug_selected=true;}
        if(args.exercise_game_debug){shell.focus_gameplay_inspector=true;shell.select_effect_debug=true;if(frame==29){auto status=character?character->effects(first_actor()):nullptr;require(status&&status->current()&&!status->current()->effects.empty(),"Game debug needs active target effect");auto& effect=status->current()->effects.front();require(effect.credit.source_network&&effect.credit.activation,"Game debug missing captured source attribution");std::cout<<"Game inspector effect="<<effect.handle<<" source="<<effect.credit.source_network<<" activation="<<effect.credit.activation<<"\n";}}
        // Senses exercise: the player stands for a second, then walks forward. Records when the first actor's controller first has a target.
        if(args.exercise_senses&&character&&character_resources&&!character_resources->spawns.empty()){senses_all=true;const bool aware=character->controller_target(character_resources->spawns.front().actor).has_value();if(aware&&frame<60)senses_early=true;if(aware&&!senses_noticed){senses_noticed=character->motor().tick;senses_noticed_z=character->motor().position.z;}}
        if(args.select_spawn&&frame==1&&!editor.document->plan().spawns.empty())editor.selected=editor.document->plan().spawns.begin()->first;
        if(args.exercise_encounter)world_debug=true;
}
void PlayExercises::launched(unsigned frame){
    const auto& args=context_.options;auto* const character=context_.session;
        // The rejected-activation gate: authority will refuse the first press of this session.
        if(args.exercise_reject&&frame==0){require(bool(character),"The refusal exercise needs a Play session");character->refuse_next_activation();}
}
void PlayExercises::check_drawing(){
    const auto& args=context_.options;const auto& splines_drawn=context_.splines_drawn;const auto& scatter_drawn=context_.scatter_drawn;const auto& volumes_drawn=context_.volumes_drawn;
    if(args.exercise_spline){const auto built=darkangel::editor_app::build_splines(context_.editor,context_.shell);std::size_t points=0,expected=0;for(const auto& spline:built){points+=spline.points.size();if(spline.curve&&spline.definition.scatter)expected+=darkangel::scatter_along(*spline.curve,*spline.definition.scatter,spline.definition.closed).size();}
        require(!built.empty()&&splines_drawn==built.size()&&points>=2&&expected>=2&&scatter_drawn==expected,"Editor Scene: the scene's spline was not drawn, or its scattered models were not");std::cout<<"Editor spline splines="<<built.size()<<" points="<<points<<" length="<<(built.front().curve?built.front().curve->length():0.)<<" scatter="<<scatter_drawn<<"\n";}
    if(args.select_encounter){require(volumes_drawn>0,"Editor Scene: the encounter's volumes were not drawn");std::cout<<"Editor encounter_scene volumes_drawn="<<volumes_drawn<<"\n";}
}
void PlayExercises::finish(){
    const auto& args=context_.options;auto* const character=context_.session;const auto& senses_drawn=context_.senses_drawn;
    const auto& cues=context_.cues;const auto& cue_player=cues.player;const auto& play_cues=cues.cues;const auto& play_cue_closure=cues.closure;const auto& cue_visual=cues.visual;const auto& cue_audio=cues.audio;
    const auto& vfx=cues.vfx;const auto& cue_effects=cues.effects;const auto& cue_primitives=cues.primitives;const auto& play_effect_generations=cues.effect_generations;
#ifdef DAE_AUDIO
    const auto& audio_workspace=*cues.audio_workspace;const auto& audio=audio_workspace.service;const auto& play_sounds=audio_workspace.play_sounds;const auto& audio_generations=audio_workspace.generations;const auto& cue_sounds=audio_workspace.cue_sounds;
#endif
    if(args.exercise_cues){require(bool(character)&&bool(cue_player)&&bool(play_cues),"Cue exercise needs a combat session with a cue player");const auto list=[](const std::vector<std::string>& keys){std::string all;for(const auto& key:keys)all+=(all.empty()?"":",")+key;return all.empty()?std::string("none"):all;};
        // What the sinks hold is exactly what the player holds for them, and nothing was dropped.
        std::size_t visual_live=0,audio_live=0;for(const auto& [instance,held]:cue_visual.held()){(void)instance;require(character->identity(held.network).has_value(),"A live visual cue names an actor that is not in the session");++visual_live;}for(const auto& [instance,held]:cue_audio.held()){(void)instance;require(character->identity(held.network).has_value(),"A live audio cue names an actor that is not in the session");++audio_live;}
        require(cue_player->dropped()==0&&cue_player->live()>=std::max(visual_live,audio_live)&&cue_visual.starts()==cue_visual.releases()+visual_live&&cue_audio.starts()==cue_audio.releases()+audio_live,"Cue sinks and the cue player disagree about what is live");
        std::cout<<"Editor cues tick="<<character->motor().tick<<" assets="<<play_cues->size()<<" closure="<<play_cue_closure<<" live="<<cue_player->live()<<" visual_live="<<visual_live<<" audio_live="<<audio_live<<" refused="<<(args.exercise_reject?1:0)<<" visual_starts="<<cue_visual.starts()<<" audio_starts="<<cue_audio.starts()<<" audio_stops="<<cue_audio.stops()<<" unresolved="<<cue_player->unresolved()<<" visual="<<list(cue_visual.started())<<" audio="<<list(cue_audio.started());{const auto drawn=vfx?vfx->stats():darkangel::VfxStats{};std::cout<<" vfx_loaded="<<drawn.effects<<" vfx_playing="<<drawn.playing<<" vfx_particles="<<drawn.instances<<" vfx_quads="<<drawn.quads<<" vfx_draws="<<drawn.draws<<" vfx_unsupported="<<drawn.unsupported<<" vfx_missing="<<cue_effects.missing()<<" trails="<<cue_primitives.trails.live()<<" trail_points="<<cue_primitives.trails.points()<<" decals="<<cue_primitives.decals.live()<<" primitive_quads="<<drawn.primitives<<" primitive_missing="<<cue_primitives.missing();{std::cout<<" effect_generations=";bool first=true;for(const auto& [name,generation]:play_effect_generations){std::cout<<(first?"":";")<<name<<":"<<generation;first=false;}if(first)std::cout<<"none";}}
#ifdef DAE_AUDIO
        {std::cout<<" sound_generations=";bool first=true;for(const auto& [name,id]:play_sounds){std::cout<<(first?"":";")<<name<<":"<<(audio_generations.contains(id)?audio_generations.at(id).substr(0,12):std::string("none"));first=false;}if(first)std::cout<<"none";}{const auto heard=audio?audio->stats():darkangel::AudioStats{};std::cout<<" audio_service="<<(audio?1:0)<<" audio_device="<<(heard.device?1:0)<<" audio_loaded="<<heard.sounds<<" audio_played="<<heard.started<<" audio_missing="<<cue_sounds.missing();}
#endif
        std::cout<<"\n";}
    if(args.select_spawn){require(senses_drawn>0,"Editor Scene: the selected spawn point's senses were not drawn");std::cout<<"Editor senses_scene drawn="<<senses_drawn<<"\n";}
}
std::unique_ptr<PlayExercises> make_play_exercises(PlayExerciseContext context){
    return std::make_unique<PlayExercises>(std::move(context));
}
} // namespace darkangel::editor_app::exercises
