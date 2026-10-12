#include <darkangel/audio_director.hpp>
#include <darkangel/cue_assets.hpp>
#include <darkangel/hash.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>
using namespace darkangel;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>bool refused(F call){try{call();}catch(const std::exception&){return true;}return false;}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);check(bool(in),"Audio fixture missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,std::string_view bytes){std::filesystem::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<bytes;check(bool(out),"Audio fixture write");}
Json sound(AssetId id,const char* name,AssetId wave,const char* path,const char* bus,bool loop,bool positioned){return {{"schema",1},{"kind","sound"},{"asset",id.text()},{"name",name},{"wave",wave.text()},{"bus",bus},{"gain",1.0},{"pitch",1.0},{"loop",loop},{"positioned",positioned},{"range",40.0},{"effect",{{"kind","none"},{"parameters",Json::object()}}},{"sources",{{"wave",path}}}};}
Json offsets(double ambient=0){return {{"master",0.0},{"music",0.0},{"ambient",ambient},{"player",0.0},{"npc",0.0},{"ui",0.0}};}
void run(AudioService& audio,unsigned frames){for(unsigned frame=0;frame<frames;++frame){audio.update(1./60);std::this_thread::sleep_for(std::chrono::milliseconds(16));}}
}
int main(){try{
    const auto owned=std::filesystem::path(DAE_SOURCE_DIR)/"content/audio/placeholder",workspace=std::filesystem::path(DAE_BINARY_DIR)/("audio-authoring-"+AssetId::random().text().substr(0,12)),mount=workspace/"s";std::filesystem::create_directories(mount/"audio");
    for(const auto* file:{"swing.wav","swing.wav.daimport","hit.wav","hit.wav.daimport"})write(mount/"audio"/file,read(owned/file));
    AssetService assets(mount,workspace/"c");assets.scan();AssetId swing_wave,hit_wave;for(const auto& asset:assets.assets()){if(asset.path=="audio/swing.wav")swing_wave=asset.id;if(asset.path=="audio/hit.wav")hit_wave=asset.id;}check(swing_wave!=AssetId{}&&hit_wave!=AssetId{},"The fixture sounds are not in the catalog");
    // Sound definitions: one WAV may have several, each with its own bus, looping and effect.
    const auto echo_id=AssetId::random(),calm_id=AssetId::random(),wind_id=AssetId::random(),battle_id=AssetId::random();
    auto echo=sound(echo_id,"Swing with echo",swing_wave,"audio/swing.wav","npc",false,true);echo["effect"]={{"kind","delay"},{"parameters",{{"seconds",0.2},{"decay",0.5}}}};echo["range"]=25.0;
    const auto calm=sound(calm_id,"Calm music",hit_wave,"audio/hit.wav","music",true,false),wind=sound(wind_id,"Wind",swing_wave,"audio/swing.wav","ambient",true,false);auto battle=sound(battle_id,"Battle music",hit_wave,"audio/hit.wav","music",true,false);battle["pitch"]=1.5;
    check(assets.create_native("audio/echo.dasound",echo.dump(2)+"\n").root==echo_id,"Sound definition creation");assets.create_native("audio/calm.dasound",calm.dump(2)+"\n");assets.create_native("audio/wind.dasound",wind.dump(2)+"\n");assets.create_native("audio/battle.dasound",battle.dump(2)+"\n");
    check(!assets.cook("audio/echo.dasound").changed,"A warm sound definition cook did work again");assets.package(echo_id,workspace/"echo.json");const auto cooked_echo=load_cooked_sound_definition(workspace/"echo.json",assets.cas_path(),echo_id);
    check(cooked_echo.definition.wave==swing_wave&&cooked_echo.wave.id==swing_wave&&cooked_echo.wave.format.frames>10000&&cooked_echo.definition.settings.bus==AudioBus::Npc&&cooked_echo.definition.settings.range==25&&cooked_echo.definition.settings.effect.kind=="delay"&&cooked_echo.definition.settings.effect.parameters.size()==2&&std::abs(cooked_echo.definition.settings.effect.parameters[0]-.2f)<1e-5f,"A cooked sound definition lost its WAV or its settings");
    // Refusals: fields outside their bounds, an effect parameter of another kind, a WAV that is not the one named.
    {const auto bad=[&](auto change){auto value=echo;change(value);return refused([&]{decode_sound_definition(value.dump());});};
        check(bad([](Json& v){v["bus"]="master";})&&bad([](Json& v){v["bus"]="drums";})&&bad([](Json& v){v["gain"]=5.0;})&&bad([](Json& v){v["pitch"]=0.1;})&&bad([](Json& v){v["range"]=0.5;})&&bad([](Json& v){v["effect"]["kind"]="reverb";})&&bad([](Json& v){v["effect"]["parameters"]["frequency"]=500.0;})&&bad([](Json& v){v["effect"]["parameters"]["decay"]=2.0;})&&bad([](Json& v){v["extra"]=1;}),"An invalid sound definition was accepted");
        auto other=echo;other["asset"]=AssetId::random().text();other["sources"]["wave"]="audio/hit.wav";check(refused([&]{assets.create_native("audio/other.dasound",other.dump(2)+"\n");})&&!std::filesystem::exists(mount/"audio/other.dasound"),"A sound definition locating another WAV than it names was created");}
    // The mixer.
    const auto mix_id=AssetId::random();Json buses;for(const auto* name:{"master","music","ambient","player","npc","ui"})buses[name]={{"volume",1.0},{"mute",false}};buses["music"]["volume"]=0.5;
    const Json mix={{"schema",1},{"kind","mix"},{"asset",mix_id.text()},{"buses",buses},{"limit_each_sound",true},{"threshold_db",-3.0},{"ducking",Json::array({{{"when","ui"},{"lower","music"},{"to",0.4},{"fade_in",0.1},{"fade_out",0.3}}})}};
    assets.create_native("audio/project.damix",mix.dump(2)+"\n");assets.package(mix_id,workspace/"mix.json");const auto cooked_mix=load_cooked_audio_mix(workspace/"mix.json",assets.cas_path(),mix_id).mix;
    check(cooked_mix.volume[std::size_t(AudioBus::Music)]==.5f&&cooked_mix.limit&&cooked_mix.threshold_db==-3&&cooked_mix.ducking.size()==1&&cooked_mix.ducking[0].when==AudioBus::Ui&&cooked_mix.ducking[0].lower==AudioBus::Music,"A cooked mixer lost its values");
    {const auto bad=[&](auto change){auto value=mix;change(value);return refused([&]{decode_audio_mix(value.dump());});};check(bad([](Json& v){v["buses"].erase("ui");})&&bad([](Json& v){v["buses"]["npc"]["volume"]=3.0;})&&bad([](Json& v){v["threshold_db"]=1.0;})&&bad([](Json& v){v["ducking"][0]["lower"]="ui";})&&bad([](Json& v){v["ducking"].push_back(v["ducking"][0]);})&&bad([](Json& v){v["ducking"][0]["when"]="master";}),"An invalid mixer was accepted");}
    // Profiles carry their sound definitions, and those their WAVs.
    const auto scene_id=AssetId::random(),fight_id=AssetId::random();
    const Json scene={{"schema",1},{"kind","audio_profile"},{"asset",scene_id.text()},{"name","Calm"},{"music",calm_id.text()},{"music_volume",0.8},{"ambient",Json::array({{{"sound",wind_id.text()},{"volume",0.6}}})},{"bus_offsets_db",offsets()},{"crossfade_seconds",0.1},{"sources",{{"sounds",{{calm_id.text(),"audio/calm.dasound"},{wind_id.text(),"audio/wind.dasound"}}}}}};
    const Json fight={{"schema",1},{"kind","audio_profile"},{"asset",fight_id.text()},{"name","Fight"},{"music",battle_id.text()},{"music_volume",1.0},{"ambient",Json::array()},{"bus_offsets_db",offsets(-6)},{"crossfade_seconds",0.1},{"sources",{{"sounds",{{battle_id.text(),"audio/battle.dasound"}}}}}};
    assets.create_native("audio/calm.daaudioprofile",scene.dump(2)+"\n");assets.create_native("audio/fight.daaudioprofile",fight.dump(2)+"\n");const std::array<AssetId,1> more{fight_id};assets.package(scene_id,workspace/"profiles.json",more);
    const auto cooked_scene=load_cooked_audio_profile(workspace/"profiles.json",assets.cas_path(),scene_id),cooked_fight=load_cooked_audio_profile(workspace/"profiles.json",assets.cas_path(),fight_id);
    check(cooked_scene.profile.music==calm_id&&cooked_scene.sounds.size()==2&&cooked_scene.sounds[0].wave.id==hit_wave&&cooked_scene.profile.ambient.size()==1&&cooked_fight.sounds.size()==1&&cooked_fight.profile.bus_offsets_db[std::size_t(AudioBus::Ambient)]==-6,"A cooked profile lost its sounds or its values");
    {const auto bad=[&](auto change){auto value=scene;change(value);return refused([&]{decode_audio_profile(value.dump());});};check(bad([](Json& v){v["sources"]["sounds"].erase(v["ambient"][0]["sound"].get<std::string>());})&&bad([](Json& v){v["ambient"].push_back(v["ambient"][0]);})&&bad([](Json& v){v["bus_offsets_db"]["music"]=20.0;})&&bad([](Json& v){v["crossfade_seconds"]=-1.0;})&&bad([](Json& v){v["music_volume"]=3.0;}),"An invalid audio profile was accepted");}
    // A changed sound definition cooks the profile that carries it again.
    {auto louder=wind;louder["gain"]=2.0;assets.edit_native("audio/wind.dasound",sha256(wind.dump(2)+"\n"),louder.dump(2)+"\n");check(assets.cook("audio/calm.daaudioprofile").changed,"A changed sound definition did not cook its profile again");assets.edit_native("audio/wind.dasound",sha256(louder.dump(2)+"\n"),wind.dump(2)+"\n");assets.cook("audio/calm.daaudioprofile");}
    // A cue may name a sound definition instead of a WAV.
    {const auto cue_id=AssetId::random();const Json cue={{"schema",1},{"kind","cue"},{"asset",cue_id.text()},{"name","Echo"},{"key","Test.Echo"},{"visual",""},{"audio","audio/echo.dasound"},{"tail_ticks",10},{"predictable",false}};assets.create_native("audio/echo.dacue",cue.dump(2)+"\n");assets.package(cue_id,workspace/"cue.json");check(load_cooked_cues(workspace/"cue.json",assets.cas_path()).at(0).audio_asset==echo_id,"A cue did not resolve a sound definition");}
    // The service with a mixer and sounds with settings, on the silent driver.
    {AudioOptions options;options.device=false;options.work=workspace/"work";options.mix=cooked_mix;AudioService audio(options);auto stats=audio.stats();
        check(stats.limit&&stats.ducking==1&&stats.bus_gain[std::size_t(AudioBus::Music)]==.5f&&!stats.bus_muted[std::size_t(AudioBus::Music)],"The service did not start with the mixer");
        std::vector<AudioSound> sounds{{echo_id,cooked_echo.generation,&cooked_echo.wave,cooked_echo.definition.settings}};for(const auto* profile:{&cooked_scene,&cooked_fight})for(const auto& held:profile->sounds)sounds.push_back({held.definition.id,held.generation,&held.wave,held.definition.settings});audio.load(sounds);check(audio.stats().sounds==4,"The sound definitions did not load");
        // A looped sound outlasts its length (the WAV is a quarter second); a stopped one ends.
        const auto looped=audio.play(calm_id,{0,0,0}),once=audio.play(echo_id,{1,1,2});check(looped&&once,"Sounds with settings did not start");audio.gain(looped,.5f);run(audio,90);check(audio.alive(looped),"A looped sound ended by itself");audio.stop(looped,0);run(audio,20);check(!audio.alive(looped),"A stopped loop kept playing");
        // A fade is carried out by the service over its own clock: the sound lasts for the fade and then ends.
        {const auto fading=audio.play(calm_id,{0,0,0});audio.stop(fading,.25);run(audio,6);check(audio.alive(fading),"A fading sound ended at once");run(audio,20);check(!audio.alive(fading),"A faded sound kept playing");}
        // Every effect kind is accepted by the engine and plays.
        {std::vector<AudioSound> kinds;std::vector<AssetId> ids;for(const auto& kind:sound_effect_kinds()){SoundSettings settings;settings.bus=AudioBus::Ui;settings.positioned=false;settings.effect.kind=kind.kind;for(const auto& parameter:kind.parameters)settings.effect.parameters.push_back(parameter.initial);ids.push_back(AssetId::random());kinds.push_back({ids.back(),kind.kind,&cooked_echo.wave,settings});}audio.load(kinds);for(const auto id:ids)check(audio.play(id,{0,0,0})!=0,"A sound with an effect did not play");run(audio,40);}
        // Volumes and mutes change while it runs.
        {auto changed=cooked_mix;changed.volume[std::size_t(AudioBus::Npc)]=.25f;changed.mute[std::size_t(AudioBus::Ui)]=true;audio.mix(changed);stats=audio.stats();check(stats.bus_gain[std::size_t(AudioBus::Npc)]==.25f&&stats.bus_muted[std::size_t(AudioBus::Ui)]&&changed.same_structure(audio.started_mix()),"A live mixer change was not applied");auto restart=changed;restart.limit=false;check(!restart.same_structure(audio.started_mix()),"A changed limiter looks like a live change");audio.mix(cooked_mix);}
        // The director: the scene's profile, an encounter's layered over it, and back.
        {AudioDirector director(audio,cooked_mix);check(director.stats().music==AssetId{}&&director.stats().ambient==0,"A director without a profile plays something");
            director.scene(cooked_scene.profile);run(audio,5);auto now=director.stats();check(now.profile==scene_id&&now.music==calm_id&&now.ambient==1&&now.layers==0&&now.missing==0,"The scene profile did not start its music and ambience");
            director.push(7,cooked_fight.profile);director.push(7,cooked_fight.profile);run(audio,5);now=director.stats();check(now.profile==fight_id&&now.music==battle_id&&now.layers==1&&now.ambient==1&&director.layered(7),"A layered profile did not take over the music and keep the ambience");
            check(std::abs(audio.stats().bus_gain[std::size_t(AudioBus::Ambient)]-std::pow(10.f,-6.f/20))<1e-4f,"A layered profile's bus offset was not applied");
            director.pop(7);run(audio,5);now=director.stats();check(now.profile==scene_id&&now.music==calm_id&&now.layers==0&&audio.stats().bus_gain[std::size_t(AudioBus::Ambient)]==1,"The scene profile did not return when the layer ended");
            auto unknown=cooked_fight.profile;unknown.music=AssetId::random();director.push(9,unknown);check(director.stats().missing==1&&director.stats().music==AssetId{},"A profile sound that is not loaded was not counted");director.pop(9);
            director.scene(std::nullopt);run(audio,30);check(director.stats().music==AssetId{}&&director.stats().ambient==0&&audio.stats().playing==0,"Removing the scene profile left sounds playing");}
        check(audio.stats().bus_gain[std::size_t(AudioBus::Music)]==.5f,"The director did not leave the mixer as it found it");}
    std::cout<<"Audio authoring: sound definitions, mixer and profiles (create, cook, package, load, refusals, dependencies), a cue naming a definition, the service with buses, limiter, ducking, looping, fades, effects and live volumes, the director layering profiles passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
