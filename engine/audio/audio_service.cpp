#include <darkangel/audio.hpp>
// The FlatBuffers parser comes first: Amplitude's headers define a macro that collides with one of its option names.
#include <flatbuffers/idl.h>
#include <SparkyStudios/Audio/Amplitude/Amplitude.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <thread>

namespace darkangel {
namespace {
using namespace SparkyStudios::Audio::Amplitude;
void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
bool exists{};
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"An Amplitude schema is missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,std::string_view bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));require(bool(out),"The audio project could not be written");}
// Amplitude reads its definitions as binary FlatBuffers. They are written here from JSON with the SDK's own compiled schemas.
void compile(const std::filesystem::path& schemas,const char* schema,const std::string& json,const std::filesystem::path& output){
    const auto compiled=read(schemas/schema);flatbuffers::IDLOptions options;options.strict_json=false;options.lang_to_generate=flatbuffers::IDLOptions::kCpp|flatbuffers::IDLOptions::kBinary;flatbuffers::Parser parser(options);
    require(parser.Deserialize(reinterpret_cast<const std::uint8_t*>(compiled.data()),compiled.size()),"An Amplitude schema could not be read");
    if(!parser.Parse(json.c_str()))throw std::runtime_error("An audio definition does not match Amplitude's schema: "+parser.error_);
    write(output,{reinterpret_cast<const char*>(parser.builder_.GetBufferPointer()),parser.builder_.GetSize()});
}
AmVector3 vector(const std::array<float,3>& value){AmVector3 out{};out.x=value[0];out.y=value[1];out.z=value[2];return out;}
std::string curve(double start,double end){return "{\"parts\":[{\"start\":{\"x\":0,\"y\":"+std::to_string(start)+"},\"end\":{\"x\":1,\"y\":"+std::to_string(end)+"},\"fader\":\"Linear\"}]}";}
template<class Ready>void wait(Ready ready,const char* error){for(unsigned turn=0;turn<5000;++turn){if(ready())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error(error);}
}
struct AudioService::Impl {
    AudioOptions options;std::filesystem::path project;NullLogger logger;std::shared_ptr<DiskFileSystem> files;bool initialized{},opened{};Listener ears;
    struct Loaded {SoundHandle handle{};std::string generation;};std::map<AssetId,Loaded> sounds;unsigned banks{};AmObjectID next_sound{100};std::map<unsigned,AmObjectID> ranges;AudioMix mixed;
    // A fade is done here, by lowering the channel's gain over update() and then stopping it at once: Amplitude's own timed stop crashes in the pinned build.
    struct Playing {Channel channel;float gain{1};double fade{},left{};};std::map<std::uint64_t,Playing> playing;std::uint64_t next{1},started{};
    void close(){
        if(initialized){amEngine->StopAll();amEngine->UnloadSoundBanks();amEngine->Deinitialize();initialized=false;}
        if(opened){amEngine->StartCloseFileSystem();for(unsigned turn=0;turn<5000&&!amEngine->TryFinalizeCloseFileSystem();++turn)std::this_thread::sleep_for(std::chrono::milliseconds(1));opened=false;}
        Engine::UnregisterDefaultExtensions();Engine::DestroyInstance();files.reset();if(MemoryManager::IsInitialized())MemoryManager::Deinitialize();
        std::error_code ignored;if(!project.empty())std::filesystem::remove_all(project,ignored);exists=false;
    }
};
AudioService::AudioService(AudioOptions options):impl_(std::make_unique<Impl>()){
    auto& p=*impl_;require(!exists,"A process has one audio service");require(!options.work.empty(),"An audio service needs a work directory");
#ifdef DAE_AMPLITUDE_SCHEMAS
    if(options.schemas.empty())options.schemas=DAE_AMPLITUDE_SCHEMAS;
#endif
    require(std::filesystem::is_directory(options.schemas),"Amplitude's schemas are missing");p.options=options;exists=true;
    try{
        p.project=options.work/("audio-"+AssetId::random().text());std::filesystem::create_directories(p.project/"data");
        // The smallest project Amplitude accepts: stereo panning (no HRTF data), a master bus with one child, one attenuation.
        const std::string config=std::string("{\"driver\":\"")+(options.device?"miniaudio":"null")+"\",\"output\":{\"frequency\":48000,\"buffer_size\":2048,\"format\":\"Float32\"},"
            "\"mixer\":{\"panning_mode\":\"Stereo\",\"active_channels\":32,\"virtual_channels\":64,\"pipeline\":\"stereo.ampipeline\"},"
            "\"game\":{\"listener_fetch_mode\":\"Nearest\",\"track_environments\":false,\"listeners\":4,\"entities\":256,\"environments\":16,\"rooms\":16,\"doppler_factor\":1.0,\"sound_speed\":343,"
            "\"obstruction\":{\"lpf_curve\":"+curve(0,1)+",\"gain_curve\":"+curve(1,1)+"},\"occlusion\":{\"lpf_curve\":"+curve(0,1)+",\"gain_curve\":"+curve(1,0)+"}},\"buses_file\":\"buses.ambus\"}";
        compile(options.schemas,"engine_config_definition.bfbs",config,p.project/"config.amconfig");
        // Master with the five buses a sound can play on. Ducking rules belong to the bus whose sounds lower another.
        {std::string buses="{\"buses\":[{\"id\":1,\"name\":\"master\",\"gain\":"+std::to_string(options.mix.volume[0])+",\"child_buses\":[2,3,4,5,6],\"duck_buses\":[],\"fader\":\"Linear\"}";
            for(std::size_t bus=1;bus<audio_bus_count;++bus){std::string ducks;for(const auto& duck:options.mix.ducking)if(std::size_t(duck.when)==bus)ducks+=std::string(ducks.empty()?"":",")+"{\"id\":"+std::to_string(std::size_t(duck.lower)+1)+",\"target_gain\":"+std::to_string(duck.to)+",\"fade_in\":{\"duration\":"+std::to_string(duck.fade_in)+",\"fader\":\"Linear\"},\"fade_out\":{\"duration\":"+std::to_string(duck.fade_out)+",\"fader\":\"Linear\"}}";
                buses+=",{\"id\":"+std::to_string(bus+1)+",\"name\":\""+audio_bus_name(AudioBus(bus))+"\",\"gain\":"+std::to_string(options.mix.volume[bus])+",\"child_buses\":[],\"duck_buses\":["+ducks+"],\"fader\":\"Linear\"}";}
            compile(options.schemas,"buses_definition.bfbs",buses+"]}",p.project/"buses.ambus");}
        // The pipeline every sound runs through. Amplitude's limiter is a node of it, so it holds each sound under the threshold, not the sum.
        {const std::string limiter=options.mix.limit?",{\"id\":10,\"name\":\"Limiter\",\"consume\":[8],\"parameters\":["+std::to_string(options.mix.threshold_db)+",5,50]}":"";
            compile(options.schemas,"pipeline_definition.bfbs","{\"id\":3,\"name\":\"stereo\",\"nodes\":[{\"id\":1,\"name\":\"Input\",\"consume\":[]},{\"id\":2,\"name\":\"Attenuation\",\"consume\":[1]},{\"id\":3,\"name\":\"Occlusion\",\"consume\":[2]},{\"id\":4,\"name\":\"StereoPanning\",\"consume\":[3]},{\"id\":6,\"name\":\"NearFieldEffect\",\"consume\":[3]},{\"id\":8,\"name\":\"StereoMixer\",\"consume\":[4,6]}"+limiter+",{\"id\":9,\"name\":\"Output\",\"consume\":["+(options.mix.limit?"10":"8")+"]}]}",p.project/"pipelines/stereo.ampipeline");}
        p.mixed=options.mix;
        MemoryManager::Initialize();Logger::SetLogger(&p.logger);p.files=std::make_shared<DiskFileSystem>();p.files->SetBasePath(p.project.native());amEngine->SetFileSystem(p.files);amEngine->StartOpenFileSystem();p.opened=true;
        wait([]{return amEngine->TryFinalizeOpenFileSystem();},"The audio project folder could not be opened");Engine::RegisterDefaultExtensions();
        require(amEngine->Initialize(AM_OS_STRING("config.amconfig")),options.device?"The audio engine could not start on this machine's audio device":"The audio engine could not start on its silent driver");p.initialized=true;
        p.ears=amEngine->AddListener(1);amEngine->SetDefaultListener(&p.ears);mix(options.mix);
    }catch(...){p.close();throw;}
}
AudioService::~AudioService(){if(impl_)impl_->close();}
void AudioService::load(std::span<const CookedSound> sounds){std::vector<AudioSound> plain;for(const auto& sound:sounds)plain.push_back({sound.id,sound.generation,&sound,{}});load(std::span<const AudioSound>(plain));}
void AudioService::load(std::span<const AudioSound> sounds){
    // Sounds are added, never taken out: Amplitude keeps a bank's sounds in use by its mixer, and unloading a bank under it is not safe.
    // A sound whose generation is already loaded is left alone; a new generation gets a new entry and the old one is no longer played.
    auto& p=*impl_;std::string listed,effects,attenuators;std::vector<std::pair<const AudioSound*,std::string>> named;const auto fixed=[](float value){return std::to_string(value);};
    for(const auto& sound:sounds){require(sound.id!=AssetId{}&&sound.wave&&!sound.wave->wave.empty(),"Cooked sound bounds");if(const auto held=p.sounds.find(sound.id);held!=p.sounds.end()&&held->second.generation==sound.generation)continue;
        const auto& settings=sound.settings;require(settings.bus!=AudioBus::Master&&std::size_t(settings.bus)<audio_bus_count&&settings.gain>=0&&settings.gain<=4&&settings.pitch>=.25f&&settings.pitch<=4&&settings.range>=1&&settings.range<=500,"Sound settings bounds");
        require(p.next_sound<100+512,"An audio service holds at most 512 sound generations; restart it");read_sound_format(sound.wave->wave);const auto id=p.next_sound++;const auto name=sound.id.text()+"-"+std::to_string(id);write(p.project/"data"/(name+".wav"),sound.wave->wave);
        // One effect, with its parameters in Amplitude's own order (the first is always how much of the effect is heard).
        AmObjectID effect{};if(settings.effect.kind!="none"){const auto& given=settings.effect.parameters;const auto at=[&](std::size_t index,float fallback){return index<given.size()?given[index]:fallback;};std::string engine;std::vector<float> values;
            if(settings.effect.kind=="low_pass"||settings.effect.kind=="high_pass"){engine="BiquadResonant";values={1,settings.effect.kind=="low_pass"?0.f:1.f,at(0,1000),at(1,.707f),0};}
            else if(settings.effect.kind=="delay"){engine="Delay";values={1,at(0,.3f),at(1,.4f),0};}
            else if(settings.effect.kind=="lofi"){engine="Lofi";values={1,at(0,8000),at(1,8)};}
            else if(settings.effect.kind=="compressor"){engine="CompressorFilter";values={1,at(0,-18),at(1,4),10,100,6,at(2,0)};}
            else throw std::runtime_error("Unknown sound effect kind");
            std::string parameters;for(const auto value:values)parameters+=std::string(parameters.empty()?"":",")+"{\"kind\":\"Static\",\"value\":"+fixed(value)+"}";effect=5000+id;
            compile(p.options.schemas,"effect_definition.bfbs","{\"id\":"+std::to_string(effect)+",\"name\":\""+name+"\",\"effect\":\""+engine+"\",\"parameters\":["+parameters+"]}",p.project/"effects"/(name+".amfx"));effects+=(effects.empty()?"\"":",\"")+name+".amfx\"";}
        // Distance: full gain within two metres (less for a short range), silent at the sound's range. One attenuation per distinct range.
        AmObjectID attenuation{};if(settings.positioned){const unsigned key=unsigned(std::lround(settings.range*10));auto known=p.ranges.find(key);if(known==p.ranges.end()){const auto made=AmObjectID(2000+p.ranges.size());const auto file="range-"+std::to_string(key)+".amattenuation";const auto outer=fixed(settings.range),inner=fixed(std::min(2.f,settings.range*.25f));
                compile(p.options.schemas,"attenuation_definition.bfbs","{\"id\":"+std::to_string(made)+",\"name\":\"range-"+std::to_string(key)+"\",\"max_distance\":"+outer+",\"shape\":{\"zone_type\":\"Sphere\",\"zone\":{\"inner\":{\"radius\":"+inner+"},\"outer\":{\"radius\":"+outer+"}},\"max_attenuation_factor\":0},\"gain_curve\":{\"parts\":[{\"start\":{\"x\":0,\"y\":1},\"end\":{\"x\":"+outer+",\"y\":0},\"fader\":\"Linear\"}]},\"air_absorption\":{\"enabled\":false,\"coefficients\":[]}}",p.project/"attenuators"/file);
                attenuators+=(attenuators.empty()?"\"":",\"")+file+"\"";known=p.ranges.emplace(key,made).first;}attenuation=known->second;}
        const auto definition="{\"id\":"+std::to_string(id)+",\"name\":\""+name+"\",\"effect\":"+std::to_string(effect)+",\"gain\":{\"kind\":\"Static\",\"value\":"+fixed(settings.gain)+"},\"pitch\":{\"kind\":\"Static\",\"value\":"+fixed(settings.pitch)+"},\"bus\":"+std::to_string(std::size_t(settings.bus)+1)+",\"priority\":{\"kind\":\"Static\",\"value\":1},\"spatialization\":\""+(settings.positioned?"Position":"None")+"\",\"attenuation\":"+std::to_string(attenuation)+",\"scope\":\"World\",\"fader\":\"Linear\",\"stream\":false,\"loop\":{\"enabled\":"+(settings.loop?"true":"false")+",\"loop_count\":0},\"near_field_gain\":{\"kind\":\"Static\",\"value\":0},\"path\":\""+name+".wav\"}";
        compile(p.options.schemas,"sound_definition.bfbs",definition,p.project/"sounds"/(name+".amsound"));listed+=(listed.empty()?"\"":",\"")+name+".amsound\"";named.emplace_back(&sound,name);}
    if(named.empty())return;const auto bank="session-"+std::to_string(p.banks++);
    compile(p.options.schemas,"sound_bank_definition.bfbs","{\"id\":"+std::to_string(1000+p.banks)+",\"name\":\""+bank+"\",\"switch_containers\":[],\"collections\":[],\"sounds\":["+listed+"],\"events\":[],\"attenuators\":["+attenuators+"],\"switches\":[],\"rtpc\":[],\"effects\":["+effects+"]}",p.project/"soundbanks"/(bank+".ambank"));
    const std::filesystem::path file(bank+".ambank");require(amEngine->LoadSoundBank(file.native()),"The audio engine refused the session's sounds");amEngine->StartLoadSoundFiles();wait([]{return amEngine->TryFinalizeLoadSoundFiles();},"The session's sounds did not load");
    for(const auto& [sound,name]:named){const auto handle=amEngine->GetSoundHandle(AmString(name.c_str()));require(handle!=nullptr,"A loaded sound has no handle");p.sounds[sound->id]={handle,sound->generation};}
}
void AudioService::mix(const AudioMix& mix){auto& p=*impl_;for(std::size_t bus=0;bus<audio_bus_count;++bus){require(mix.volume[bus]>=0&&mix.volume[bus]<=4,"Bus volume bounds");const auto found=amEngine->FindBus(AmBusID(bus+1));require(found.Valid(),"An audio bus is missing");found.SetGain(mix.volume[bus]);found.SetMute(mix.mute[bus]);p.mixed.volume[bus]=mix.volume[bus];p.mixed.mute[bus]=mix.mute[bus];}}
const AudioMix& AudioService::started_mix()const{return impl_->options.mix;}
bool AudioService::loaded(AssetId id)const{return impl_->sounds.contains(id);}
std::uint64_t AudioService::play(AssetId id,std::array<float,3> position){
    auto& p=*impl_;const auto found=p.sounds.find(id);if(found==p.sounds.end())return 0;auto channel=amEngine->Play(found->second.handle,vector(position));if(!channel.Valid())return 0;
    const auto own=p.next++;p.playing.emplace(own,Impl::Playing{channel});++p.started;return own;
}
void AudioService::move(std::uint64_t handle,std::array<float,3> position){auto& p=*impl_;if(auto found=p.playing.find(handle);found!=p.playing.end())found->second.channel.SetLocation(vector(position));}
void AudioService::gain(std::uint64_t handle,float value){auto& p=*impl_;require(value>=0&&value<=4,"Audio gain bounds");if(auto found=p.playing.find(handle);found!=p.playing.end()){found->second.gain=value;if(found->second.fade<=0)found->second.channel.SetGain(value);}}
void AudioService::stop(std::uint64_t handle,double fade_seconds){auto& p=*impl_;require(fade_seconds>=0&&fade_seconds<=60,"Audio fade bounds");if(auto found=p.playing.find(handle);found!=p.playing.end()){if(fade_seconds<=0)found->second.channel.Stop(0);else if(found->second.fade<=0){found->second.fade=fade_seconds;found->second.left=fade_seconds;}}}
bool AudioService::alive(std::uint64_t handle)const{const auto& p=*impl_;const auto found=p.playing.find(handle);return found!=p.playing.end()&&found->second.channel.Valid()&&found->second.channel.GetPlaybackState()!=eChannelPlaybackState_Stopped;}
void AudioService::listener(std::array<float,3> position,std::array<float,3> forward,std::array<float,3> up){auto& p=*impl_;p.ears.SetLocation(vector(position));p.ears.SetOrientation(Orientation(vector(forward),vector(up)));}
void AudioService::update(double seconds){
    auto& p=*impl_;require(seconds>=0&&seconds<=10,"Audio clock delta bounds");amEngine->AdvanceFrame(static_cast<AmTime>(seconds));
    for(auto at=p.playing.begin();at!=p.playing.end();){auto& held=at->second;if(!held.channel.Valid()||held.channel.GetPlaybackState()==eChannelPlaybackState_Stopped){at=p.playing.erase(at);continue;}
        if(held.fade>0){held.left-=seconds;if(held.left<=0){held.channel.Stop(0);at=p.playing.erase(at);continue;}held.channel.SetGain(held.gain*float(held.left/held.fade));}++at;}
}
AudioStats AudioService::stats()const{const auto& p=*impl_;AudioStats stats{p.options.device,p.options.device?"miniaudio":"null",p.sounds.size(),p.playing.size(),p.started};stats.bus_gain=p.mixed.volume;stats.bus_muted=p.mixed.mute;stats.limit=p.options.mix.limit;stats.ducking=p.options.mix.ducking.size();return stats;}
}
