#include "audio_workspace.hpp"
#include "cue_debug.hpp"
#include "editor_options.hpp"
#include "editor_ui.hpp"
#include "native_authoring.hpp"
#include "package_file.hpp"
#include <darkangel/audio_assets.hpp>
#include <darkangel/sound_assets.hpp>
#include <darkangel/check.hpp>
#include <imgui.h>
#include <algorithm>
#include <deque>
#include <fstream>
#include <set>

namespace darkangel::editor_app {
AudioWorkspace::AudioWorkspace(Controller& editor, const EditorOptions& options, RecordingCueSink& recording)
    : cue_sounds{&recording}, editor_(editor), options_(options) {}
AudioMix AudioWorkspace::project_mix() {
        auto& editor = editor_;
        auto& audio_tab = state;
        const auto& audio_mix_path = mix_path;
        if(!editor.assets)return {};
        try{for(const auto& asset:editor.asset_inventory)if(asset.path==audio_mix_path){if(const auto draft=editor.authoring.drafts.find(asset.id);draft!=editor.authoring.drafts.end())return darkangel::decode_audio_mix(draft->second.value.dump()).mix;}
            const auto file=editor.assets->source_root()/audio_mix_path;std::error_code ignored;const auto written=std::filesystem::last_write_time(file,ignored);if(ignored){audio_tab.file_known=false;return {};}
            if(!audio_tab.file_known||written!=audio_tab.file_seen){std::ifstream in(file,std::ios::binary);const std::string text{std::istreambuf_iterator<char>(in),{}};audio_tab.file_mix=darkangel::decode_audio_mix(text).mix;audio_tab.file_seen=written;audio_tab.file_known=true;}
            return audio_tab.file_mix;}
        catch(const std::exception&){return audio_tab.file_known?audio_tab.file_mix:darkangel::AudioMix{};}
}
bool AudioWorkspace::ensure() {
        auto& editor = editor_;
        const auto& args = options_;
        auto& audio = service;
        auto& audio_failed = failed;
        auto& audio_tried = tried;
        auto& audio_director = director;
        auto& audio_applied = applied;
        auto& audio_in_play = in_play;
        const auto project_mix = [this] { return this->project_mix(); };
        if(!editor.assets)return false;const auto mix=project_mix();
        if(audio&&!mix.same_structure(audio->started_mix())&&!audio_in_play&&!audio_director&&audio->stats().playing==0){cue_sounds.bind(nullptr,nullptr,{});audio.reset();audio_tried=false;audio_applied.reset();}
        if(!audio&&!audio_tried){audio_tried=true;try{darkangel::AudioOptions options;options.device=!args.hidden&&!args.no_audio;options.work=editor.assets->cas_path().parent_path();options.mix=mix;audio=std::make_unique<darkangel::AudioService>(options);audio_applied=mix;audio_failed.clear();}catch(const std::exception& error){audio_failed=error.what();editor.log(std::string("Audio unavailable: ")+error.what(),darkangel::editor_app::ConsoleSeverity::Warning);}}
        return bool(audio);
}
std::map<AssetId, AudioProfile> AudioWorkspace::load(std::vector<AssetId> wanted) {
        auto& editor = editor_;
        auto& audio = service;
        auto& audio_generations = generations;
        std::map<AssetId,darkangel::AudioProfile> profiles;if(!audio||!editor.assets)return profiles;std::map<AssetId,std::string> paths;for(const auto& asset:editor.assets->assets())paths[asset.id]=asset.path;
        std::vector<AssetId> roots;for(const auto id:wanted)if(paths.contains(id)&&std::find(roots.begin(),roots.end(),id)==roots.end())roots.push_back(id);if(roots.empty())return profiles;require(roots.size()<=64,"At most 64 sounds and profiles are loaded at once");
        const auto registry=editor.assets->cas_path().parent_path()/("audio-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);
        std::deque<darkangel::CookedSound> waves;std::deque<darkangel::CookedSoundDefinition> definitions;std::vector<darkangel::AudioSound> sounds;std::set<AssetId> listed;
        const auto define=[&](darkangel::CookedSoundDefinition cooked){if(!listed.insert(cooked.definition.id).second)return;definitions.push_back(std::move(cooked));const auto& held=definitions.back();sounds.push_back({held.definition.id,held.generation,&held.wave,held.definition.settings});};
        for(const auto id:roots){const auto extension=std::filesystem::path(paths.at(id)).extension();
            if(extension==".wav"){if(!listed.insert(id).second)continue;waves.push_back(darkangel::load_cooked_sound(registry,editor.assets->cas_path(),id));sounds.push_back({id,waves.back().generation,&waves.back(),{}});}
            else if(extension==".dasound")define(darkangel::load_cooked_sound_definition(registry,editor.assets->cas_path(),id));
            else if(extension==".daaudioprofile"){auto cooked=darkangel::load_cooked_audio_profile(registry,editor.assets->cas_path(),id);for(auto& held:cooked.sounds)define(std::move(held));profiles[id]=std::move(cooked.profile);}}
        for(const auto& sound:sounds)audio_generations[sound.id]=sound.generation;audio->load(sounds);return profiles;
}
void AudioWorkspace::draw_panel(bool play_running) {
        auto& editor = editor_;
        auto& audio = service;
        auto& audio_failed = failed;
        auto& audio_director = director;
        auto& audio_applied = applied;
        auto& audio_tab = state;
        const auto& audio_mix_path = mix_path;
        const auto project_mix = [this] { return this->project_mix(); };
        const auto ensure_audio = [this] { return ensure(); };
        const auto load_audio = [this](std::vector<AssetId> wanted) { return load(std::move(wanted)); };
        auto& tab=audio_tab;const auto red=[](const std::string& text){ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.f,.45f,.35f,1.f));ImGui::TextWrapped("%s",text.c_str());ImGui::PopStyleColor();};
        const auto guarded=[&](auto operation){try{operation();}catch(const std::exception& error){tab.error=error.what();}};const auto act=[&](auto operation){try{operation();tab.error.clear();}catch(const std::exception& error){tab.error=error.what();}};
        const auto root=editor.assets->source_root();const bool playing=play_running;const char* bus_names[]{"master","music","ambient","player","npc","ui"};const char* bus_labels[]{"Master","Music","Ambient","Player","NPC","UI"};
        std::vector<darkangel::AssetInfo> waves,definitions,profiles;AssetId mix_id;for(const auto& asset:editor.asset_inventory){const auto extension=std::filesystem::path(asset.path).extension();if(extension==".wav")waves.push_back(asset);else if(extension==".dasound")definitions.push_back(asset);else if(extension==".daaudioprofile")profiles.push_back(asset);if(asset.path==audio_mix_path)mix_id=asset.id;}
        const auto path_of=[&](const std::string& id)->std::string{if(id.empty())return "(none)";for(const auto& asset:editor.asset_inventory)if(asset.id.text()==id)return asset.path;return "(missing) "+id;};
        const auto dirty=[&](AssetId id){const auto draft=editor.authoring.drafts.find(id);return draft!=editor.authoring.drafts.end()&&draft->second.dirty();};
        const auto save_draft=[&](AssetId id){const auto path=editor.authoring.drafts.at(id).asset.path;editor.authoring.save(*editor.assets,id);editor.refresh_assets();if(on_cue_sources_saved)on_cue_sources_saved();editor.log("Saved "+path);};
        if(!audio_failed.empty())red("Audio is unavailable: "+audio_failed);if(!tab.error.empty())red(tab.error);
        if(audio){const auto heard=audio->stats();ImGui::TextDisabled("Audio engine: %s driver, %zu sound(s) loaded, %zu playing.%s",heard.driver.c_str(),heard.sounds,heard.playing,project_mix().same_structure(audio->started_mix())?"":" The limiter or ducking changed: it applies when audio restarts, the next time something is played while nothing else is.");}
        else ImGui::TextDisabled("The audio engine starts the first time something is played.");
        const float column=std::max(260.f,(ImGui::GetContentRegionAvail().x-16)/3);
        // ---- Sounds
        ImGui::BeginChild("Audio sounds",{column,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Sounds");
        ImGui::TextWrapped("WAV files, and sound definitions that say how a WAV is played: bus, volume, pitch, looping, positioned or flat, range and one effect.");
        ImGui::SetNextItemWidth(-90);ImGui::InputTextWithHint("##audio-import","Path of a .wav file to import",tab.import_path,sizeof(tab.import_path));ImGui::SameLine();
        if(ImGui::Button("Import"))act([&]{const std::filesystem::path from(tab.import_path);require(std::filesystem::is_regular_file(from)&&from.extension()==".wav","Give the path of an existing .wav file");const auto stem=from.stem().string();require(!stem.empty()&&stem.size()<=48&&stem.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==stem.npos,"The file name may hold letters, digits, '_' and '-'");
            const auto relative="audio/"+stem+".wav";require(!std::filesystem::exists(root/relative),"A sound with this name is already in the project");std::filesystem::create_directories(root/"audio");std::filesystem::copy_file(from,root/relative);try{editor.assets->adopt(relative);editor.assets->cook(relative);}catch(...){std::error_code ignored;std::filesystem::remove(root/relative,ignored);std::filesystem::remove(root/(relative+".daimport"),ignored);throw;}
            editor.refresh_assets();for(const auto& asset:editor.asset_inventory)if(asset.path==relative)tab.sound=asset.id;tab.import_path[0]=0;editor.log("Imported "+relative);});
        for(const auto& asset:waves)if(ImGui::Selectable((asset.path+"##wave").c_str(),asset.id==tab.sound))tab.sound=asset.id;
        for(const auto& asset:definitions)if(ImGui::Selectable((asset.path+(dirty(asset.id)?" *":"")+"##definition").c_str(),asset.id==tab.sound))tab.sound=asset.id;
        // Audition plays through the real service. A definition is played as its draft has it, saved or not.
        const auto audition=[&](AssetId wave,AssetId as,const darkangel::SoundSettings& settings,const std::string& settings_text){if(!ensure_audio())throw std::runtime_error(audio_failed.empty()?"Audio is unavailable":audio_failed);std::string wave_path;for(const auto& asset:editor.asset_inventory)if(asset.id==wave)wave_path=asset.path;require(!wave_path.empty(),"The sound's WAV is not in the project");
            const auto registry=editor.assets->cas_path().parent_path()/("audition-"+AssetId::random().text()+".json");ComposerPackageFile cleanup{registry};const std::array roots{wave};darkangel::editor_app::package_authoring(*editor.assets,roots,registry);const auto cooked=darkangel::load_cooked_sound(registry,editor.assets->cas_path(),wave);
            const std::array<darkangel::AudioSound,1> one{{{as,darkangel::sha256(cooked.generation+settings_text),&cooked,settings}}};audio->load(one);if(tab.audition)audio->stop(tab.audition,0);audio->listener({0,0,0},{0,0,1},{0,1,0});tab.audition=audio->play(as,{0,0,1.5f});require(tab.audition!=0,"The sound did not start");};
        if(tab.sound!=AssetId{}){const auto selected=std::find_if(editor.asset_inventory.begin(),editor.asset_inventory.end(),[&](const auto& asset){return asset.id==tab.sound;});
            if(selected==editor.asset_inventory.end())tab.sound=AssetId{};
            else if(std::filesystem::path(selected->path).extension()==".wav"){ImGui::SeparatorText("Selected WAV");ImGui::TextWrapped("%s",selected->path.c_str());
                if(ImGui::Button("Play##wave"))act([&]{audition(selected->id,selected->id,{},"plain");});ImGui::SameLine();if(ImGui::Button("Stop##wave")&&audio&&tab.audition){audio->stop(tab.audition,.05);tab.audition=0;}
                ImGui::SetNextItemWidth(180);ImGui::InputText("##definition-name",tab.sound_name,sizeof(tab.sound_name));ImGui::SameLine();if(ImGui::Button("New definition"))act([&]{const auto wave=*selected;tab.sound=editor.authoring.create_sound_definition(*editor.assets,wave.id,wave.path,tab.sound_name);editor.refresh_assets();});}
            else guarded([&]{auto& draft=editor.authoring.open(*editor.assets,tab.sound);auto& value=draft.value;ImGui::SeparatorText("Selected definition");ImGui::TextWrapped("%s",draft.asset.path.c_str());ImGui::TextDisabled("WAV: %s",path_of(value.value("wave",std::string{})).c_str());
                {char buffer[65]{};std::snprintf(buffer,sizeof(buffer),"%s",value.value("name",std::string{}).c_str());if(ImGui::InputText("Name##sound",buffer,sizeof(buffer)))value["name"]=std::string(buffer);}
                {const auto current=value.value("bus",std::string("player"));if(ImGui::BeginCombo("Bus##sound",current.c_str())){for(int bus=1;bus<6;++bus)if(ImGui::Selectable(bus_labels[bus],current==bus_names[bus]))value["bus"]=bus_names[bus];ImGui::EndCombo();}}
                const auto slider=[&](const char* label,nlohmann::json& field,float least,float most,const char* format="%.2f"){float number=field.is_number()?field.get<float>():least;if(ImGui::SliderFloat(label,&number,least,most,format))field=double(std::round(number*1000)/1000);};
                slider("Volume##sound",value["gain"],0,4);slider("Pitch##sound",value["pitch"],.25f,4);{bool loop=value.value("loop",false);if(ImGui::Checkbox("Loops",&loop))value["loop"]=loop;}ImGui::SameLine();{bool positioned=value.value("positioned",true);if(ImGui::Checkbox("Positioned in the world",&positioned))value["positioned"]=positioned;}
                if(value.value("positioned",true))slider("Silent beyond (m)##sound",value["range"],1,500,"%.0f");
                {auto& effect=value["effect"];const auto current=effect.value("kind",std::string("none"));const auto kinds=darkangel::sound_effect_kinds();const auto known=std::find_if(kinds.begin(),kinds.end(),[&](const auto& kind){return current==kind.kind;});
                    if(ImGui::BeginCombo("Effect##sound",known!=kinds.end()?known->label:current.c_str())){for(const auto& kind:kinds)if(ImGui::Selectable(kind.label,current==kind.kind)){effect={{"kind",kind.kind},{"parameters",nlohmann::json::object()}};for(const auto& parameter:kind.parameters)effect["parameters"][parameter.name]=double(parameter.initial);}ImGui::EndCombo();}
                    if(known!=kinds.end())for(const auto& parameter:known->parameters){if(!effect["parameters"].contains(parameter.name))effect["parameters"][parameter.name]=double(parameter.initial);slider((std::string(parameter.name)+"##effect").c_str(),effect["parameters"][parameter.name],parameter.least,parameter.most);}}
                std::string problem;darkangel::SoundDefinitionAsset decoded;try{decoded=darkangel::decode_sound_definition(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
                ImGui::BeginDisabled(!problem.empty());if(ImGui::Button("Play##definition")){const auto text=value.dump();act([&]{audition(decoded.wave,decoded.id,decoded.settings,text);});}ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Stop##definition")&&audio&&tab.audition){audio->stop(tab.audition,.05);tab.audition=0;}
                ImGui::SameLine();ImGui::BeginDisabled(!draft.dirty()||!problem.empty());if(ImGui::Button("Save##definition")){const auto id=tab.sound;act([&]{save_draft(id);});}ImGui::EndDisabled();});}
        ImGui::EndChild();ImGui::SameLine();
        // ---- Mixer
        ImGui::BeginChild("Audio mixer",{column,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Mixer");
        if(mix_id==AssetId{}){ImGui::TextWrapped("This project has no mixer yet. Without one every bus plays at full volume.");if(ImGui::Button("Create the project mixer"))act([&]{editor.authoring.create_audio_mix(*editor.assets);editor.refresh_assets();});}
        else guarded([&]{auto& draft=editor.authoring.open(*editor.assets,mix_id);auto& value=draft.value;ImGui::TextWrapped("Volumes and mutes are heard at once, also during Play. Master is over the other five.");
            for(int bus=0;bus<6;++bus){auto& entry=value["buses"][bus_names[bus]];float volume=entry.value("volume",1.f);ImGui::SetNextItemWidth(170);if(ImGui::SliderFloat((std::string(bus_labels[bus])+"##volume").c_str(),&volume,0,2,"%.2f"))entry["volume"]=double(std::round(volume*100)/100);ImGui::SameLine();bool mute=entry.value("mute",false);if(ImGui::Checkbox((std::string("Mute##")+bus_names[bus]).c_str(),&mute))entry["mute"]=mute;}
            ImGui::SeparatorText("Loudness");{bool limit=value.value("limit_each_sound",false);if(ImGui::Checkbox("Limit each sound to",&limit))value["limit_each_sound"]=limit;ImGui::SameLine();float threshold=value.value("threshold_db",-1.f);ImGui::SetNextItemWidth(120);if(ImGui::SliderFloat("dB##limit",&threshold,-40,0,"%.1f"))value["threshold_db"]=double(std::round(threshold*10)/10);}
            ImGui::TextDisabled("The audio engine limits sounds one by one, not their sum.");
            ImGui::SeparatorText("Ducking");ImGui::TextWrapped("While a sound plays on the first bus, the second is lowered.");auto& ducking=value["ducking"];
            const auto bus_combo=[&](const char* label,nlohmann::json& field){const auto current=field.is_string()?field.get<std::string>():std::string("music");ImGui::SetNextItemWidth(90);if(ImGui::BeginCombo(label,current.c_str())){for(int bus=1;bus<6;++bus)if(ImGui::Selectable(bus_names[bus],current==bus_names[bus]))field=bus_names[bus];ImGui::EndCombo();}};
            for(unsigned index=0;index<ducking.size();++index){ImGui::PushID(int(index));auto& rule=ducking[index];bus_combo("##when",rule["when"]);ImGui::SameLine();ImGui::TextUnformatted("lowers");ImGui::SameLine();bus_combo("##lower",rule["lower"]);ImGui::SameLine();float to=rule.value("to",.5f);ImGui::SetNextItemWidth(70);if(ImGui::SliderFloat("to",&to,0,1,"%.2f"))rule["to"]=double(std::round(to*100)/100);ImGui::SameLine();const bool remove=ImGui::SmallButton("Remove");ImGui::PopID();if(remove){ducking.erase(index);break;}}
            if(ducking.size()<8&&ImGui::Button("Add a ducking rule"))ducking.push_back({{"when","ui"},{"lower","music"},{"to",.5},{"fade_in",.2},{"fade_out",.5}});
            std::string problem;try{darkangel::decode_audio_mix(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
            ImGui::BeginDisabled(!draft.dirty()||!problem.empty());if(ImGui::Button("Save##mixer"))act([&]{save_draft(mix_id);});ImGui::EndDisabled();});
        ImGui::EndChild();ImGui::SameLine();
        // ---- Scene audio
        ImGui::BeginChild("Audio profiles",{0,0},ImGuiChildFlags_Borders);ImGui::SeparatorText("Scene audio");
        const auto scene_profile=darkangel::editor_app::scene_audio_profile(editor);ImGui::TextWrapped("This scene uses: %s",path_of(scene_profile==AssetId{}?std::string{}:scene_profile.text()).c_str());
        ImGui::TextWrapped("A profile is music, ambient loops and bus offsets. The scene's starts with Play; an encounter can layer its own while it runs (set in the encounter's Inspector).");
        for(const auto& asset:profiles)if(ImGui::Selectable((asset.path+(dirty(asset.id)?" *":"")+(asset.id==scene_profile?"  (this scene)":"")+"##profile").c_str(),asset.id==tab.profile))tab.profile=asset.id;
        ImGui::SetNextItemWidth(180);ImGui::InputText("##profile-name",tab.profile_name,sizeof(tab.profile_name));ImGui::SameLine();if(ImGui::Button("New profile"))act([&]{tab.profile=editor.authoring.create_audio_profile(*editor.assets,tab.profile_name);editor.refresh_assets();});
        if(tab.profile!=AssetId{})guarded([&]{auto& draft=editor.authoring.open(*editor.assets,tab.profile);auto& value=draft.value;const auto before=value;ImGui::SeparatorText("Selected profile");ImGui::TextWrapped("%s",draft.asset.path.c_str());
            {char buffer[65]{};std::snprintf(buffer,sizeof(buffer),"%s",value.value("name",std::string{}).c_str());if(ImGui::InputText("Name##profile",buffer,sizeof(buffer)))value["name"]=std::string(buffer);}
            const auto slider=[&](const char* label,nlohmann::json& field,float least,float most,const char* format="%.2f"){float number=field.is_number()?field.get<float>():least;ImGui::SetNextItemWidth(170);if(ImGui::SliderFloat(label,&number,least,most,format))field=double(std::round(number*100)/100);};
            {const auto current=value["music"].is_string()?value["music"].get<std::string>():std::string{};if(ImGui::BeginCombo("Music##profile",path_of(current).c_str())){if(ImGui::Selectable("(none)",current.empty()))value["music"]=nullptr;for(const auto& asset:definitions)if(ImGui::Selectable(asset.path.c_str(),asset.id.text()==current))value["music"]=asset.id.text();ImGui::EndCombo();}}
            slider("Music volume##profile",value["music_volume"],0,2);ImGui::SeparatorText("Ambient loops");auto& ambient=value["ambient"];
            for(unsigned index=0;index<ambient.size();++index){ImGui::PushID(int(index));ImGui::TextWrapped("%s",path_of(ambient[index].value("sound",std::string{})).c_str());slider("Volume",ambient[index]["volume"],0,2);ImGui::SameLine();const bool remove=ImGui::SmallButton("Remove");ImGui::PopID();if(remove){ambient.erase(index);break;}}
            if(ambient.size()<8&&ImGui::BeginCombo("##add-ambient","Add an ambient loop")){for(const auto& asset:definitions){const bool listed=std::any_of(ambient.begin(),ambient.end(),[&](const auto& loop){return loop.value("sound",std::string{})==asset.id.text();});if(ImGui::Selectable(asset.path.c_str(),false,listed?ImGuiSelectableFlags_Disabled:0))ambient.push_back({{"sound",asset.id.text()},{"volume",1.0}});}ImGui::EndCombo();}
            ImGui::SeparatorText("Bus offsets (dB)");for(int bus=0;bus<6;++bus)slider((std::string(bus_labels[bus])+"##offset").c_str(),value["bus_offsets_db"][bus_names[bus]],-60,12,"%.1f");slider("Crossfade (s)##profile",value["crossfade_seconds"],0,30,"%.1f");
            // The locators of the sounds it names are kept with the names.
            if(value!=before){nlohmann::json located=nlohmann::json::object();const auto locate=[&](const nlohmann::json& id){if(!id.is_string())return;for(const auto& asset:definitions)if(asset.id.text()==id.get<std::string>())located[asset.id.text()]=asset.path;};locate(value["music"]);for(const auto& loop:value["ambient"])locate(loop["sound"]);value["sources"]={{"sounds",located}};}
            std::string problem;try{darkangel::decode_audio_profile(value.dump());}catch(const std::exception& error){problem=error.what();}if(!problem.empty())red(problem);
            const bool unsaved=draft.dirty();ImGui::BeginDisabled(!unsaved||!problem.empty());if(ImGui::Button("Save##profile")){const auto id=tab.profile;act([&]{save_draft(id);});}ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||unsaved||tab.profile==scene_profile);if(ImGui::Button("Use for this scene"))act([&]{darkangel::editor_app::set_scene_audio_profile(editor,tab.profile);});ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||scene_profile==AssetId{});if(ImGui::Button("Scene uses none"))act([&]{darkangel::editor_app::set_scene_audio_profile(editor,AssetId{});});ImGui::EndDisabled();
            // Audition of the whole profile, as saved.
            ImGui::BeginDisabled(playing||unsaved);if(ImGui::Button(tab.profile_audition?"Restart audition":"Audition"))act([&]{const auto id=tab.profile;if(!ensure_audio())throw std::runtime_error(audio_failed.empty()?"Audio is unavailable":audio_failed);audio_director.reset();auto loaded=load_audio({id});require(loaded.contains(id),"The profile did not cook");audio_director=std::make_unique<darkangel::AudioDirector>(*audio,project_mix());audio_applied=project_mix();audio_director->scene(loaded.at(id));tab.profile_audition=true;});ImGui::EndDisabled();
            ImGui::SameLine();ImGui::BeginDisabled(playing||!tab.profile_audition);if(ImGui::Button("Stop audition")){audio_director.reset();tab.profile_audition=false;}ImGui::EndDisabled();
            if(unsaved)ImGui::TextDisabled("Save the profile to audition it or to use it for the scene.");});
        ImGui::EndChild();
        try{editor.authoring.record_changes(*editor.assets,"Edit audio",ImGui::IsAnyItemActive());}catch(const std::exception& error){tab.error=error.what();}
}
} // namespace darkangel::editor_app
