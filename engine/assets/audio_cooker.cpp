#include "assets_internal.hpp"
#include <darkangel/audio_assets.hpp>
#include <darkangel/hash.hpp>
#include <algorithm>
#include <cmath>
#include <set>
namespace darkangel {
namespace {
using namespace assets_detail;
constexpr std::array<const char*,audio_bus_count> bus_names{"master","music","ambient","player","npc","ui"};
void fields(const Json& value,std::initializer_list<const char*> needed,std::initializer_list<const char*> optional,const char* error){
    require(value.is_object(),error);std::size_t found{};for(auto field:needed){require(value.contains(field),error);++found;}for(auto field:optional)if(value.contains(field))++found;require(found==value.size(),error);
}
AssetId reference(const Json& value,const char* error){require(value.is_string(),error);const auto id=AssetId::parse(value.get<std::string>());require(id!=AssetId{},error);return id;}
float number(const Json& value,float least,float most,const char* error){require(value.is_number(),error);const auto read=value.get<double>();require(std::isfinite(read)&&read>=least&&read<=most,error);return float(read);}
bool flag(const Json& value,const char* error){require(value.is_boolean(),error);return value.get<bool>();}
AudioBus bus(const Json& value,bool allow_master,const char* error){require(value.is_string(),error);const auto found=audio_bus(value.get<std::string>());require(found&&(allow_master||*found!=AudioBus::Master),error);return *found;}
std::string display(const Json& value,const char* error){require(value.is_string(),error);auto name=value.get<std::string>();require(!name.empty()&&name.size()<=64,error);return name;}
// One cooked registry, read once; every product is checked against its digest and its own identity.
struct Records {
    std::map<AssetId,Json> records;std::filesystem::path cas;
    Records(const std::filesystem::path& registry,const std::filesystem::path& storage):cas(storage){const auto manifest_held=registry_json(registry);const auto& manifest=*manifest_held;require(manifest.at("schema")==1&&manifest.at("assets").is_array()&&manifest.at("assets").size()<=runtime_package_product_limit,"Audio registry schema/limit");for(const auto& record:manifest.at("assets"))require(records.emplace(AssetId::parse(record.at("id").get<std::string>()),record).second,"Duplicate audio registry UUID");}
    std::pair<std::string,std::string> load(AssetId id,const char* kind,const char* extension)const{const auto found=records.find(id);require(found!=records.end(),"An audio product is missing from the registry");const auto& record=found->second;require(record.at("kind")==kind&&record.at("extension")==extension,"Audio product type mismatch");
        const auto hash=record.at("sha256").get<std::string>();require(hash.size()==64&&hash.find_first_not_of("0123456789abcdef")==hash.npos,"Audio product digest");auto bytes=read(cas/(hash+"."+extension),65536);require(sha256(bytes)==hash,"Audio product hash mismatch");return {std::move(bytes),hash};}
};
CookedSoundDefinition sound_definition(const Records& records,const std::filesystem::path& registry,AssetId id){
    auto [bytes,hash]=records.load(id,"sound-def","sound.json");CookedSoundDefinition cooked;cooked.definition=decode_sound_definition(bytes);require(cooked.definition.id==id,"Cooked sound definition identity mismatch");
    cooked.wave=load_cooked_sound(registry,records.cas,cooked.definition.wave);cooked.generation=sha256(hash+cooked.wave.generation);return cooked;
}
}
const char* audio_bus_name(AudioBus value){return bus_names[std::min<std::size_t>(std::size_t(value),audio_bus_count-1)];}
std::optional<AudioBus> audio_bus(std::string_view name){for(std::size_t index=0;index<audio_bus_count;++index)if(name==bus_names[index])return AudioBus(index);return std::nullopt;}
std::span<const SoundEffectKind> sound_effect_kinds(){
    static const std::vector<SoundEffectKind> kinds{
        {"none","None",{}},
        {"low_pass","Low pass",{{"frequency",20,20000,1000},{"resonance",.1f,20,.707f}}},
        {"high_pass","High pass",{{"frequency",20,20000,1000},{"resonance",.1f,20,.707f}}},
        {"delay","Delay (echo)",{{"seconds",.01f,2,.3f},{"decay",0,.95f,.4f}}},
        {"lofi","Lo-fi",{{"sample_rate",100,22000,8000},{"bits",1,16,8}}},
        {"compressor","Compressor",{{"threshold_db",-60,0,-18},{"ratio",1,20,4},{"makeup_db",0,24,0}}}};
    return kinds;
}
SoundDefinitionAsset decode_sound_definition(std::string_view text){
    require(text.size()<=8192,"Sound definition size bound");const auto value=json(text,8192);fields(value,{"schema","kind","asset","name","wave","bus","gain","pitch","loop","positioned","range","effect"},{"sources"},"Sound definition fields");
    require(value.at("schema")==1&&value.at("kind")=="sound","Sound definition schema/type");SoundDefinitionAsset sound;sound.id=reference(value.at("asset"),"Sound definition identity");sound.wave=reference(value.at("wave"),"A sound definition names its WAV asset");require(sound.wave!=sound.id,"A sound definition is not its own WAV");
    sound.name=display(value.at("name"),"Sound definition name bounds (1 to 64 characters)");auto& settings=sound.settings;settings.bus=bus(value.at("bus"),false,"A sound plays on music, ambient, player, npc or ui");
    settings.gain=number(value.at("gain"),0,4,"Sound gain bounds (0 to 4)");settings.pitch=number(value.at("pitch"),.25f,4,"Sound pitch bounds (0.25 to 4)");settings.loop=flag(value.at("loop"),"Sound loop flag type");settings.positioned=flag(value.at("positioned"),"Sound positioned flag type");settings.range=number(value.at("range"),1,500,"Sound range bounds (1 to 500 metres)");
    const auto& effect=value.at("effect");fields(effect,{"kind","parameters"},{},"A sound's effect is {kind, parameters}");require(effect.at("kind").is_string()&&effect.at("parameters").is_object(),"A sound's effect is {kind, parameters}");settings.effect.kind=effect.at("kind").get<std::string>();
    const auto kinds=sound_effect_kinds();const auto kind=std::find_if(kinds.begin(),kinds.end(),[&](const auto& known){return settings.effect.kind==known.kind;});require(kind!=kinds.end(),"Unknown sound effect kind");
    for(const auto& [name,unused]:effect.at("parameters").items())require(std::any_of(kind->parameters.begin(),kind->parameters.end(),[&](const auto& known){return name==known.name;}),"A sound effect parameter does not belong to its kind");
    for(const auto& known:kind->parameters)settings.effect.parameters.push_back(effect.at("parameters").contains(known.name)?number(effect.at("parameters").at(known.name),known.least,known.most,"A sound effect parameter is out of its range"):known.initial);
    if(value.contains("sources")){fields(value.at("sources"),{"wave"},{},"A sound definition's locator is sources.wave");require(value.at("sources").at("wave").is_string(),"A sound definition's locator is sources.wave");}
    return sound;
}
AudioMixAsset decode_audio_mix(std::string_view text){
    require(text.size()<=8192,"Mixer size bound");const auto value=json(text,8192);fields(value,{"schema","kind","asset","buses","limit_each_sound","threshold_db","ducking"},{},"Mixer fields");require(value.at("schema")==1&&value.at("kind")=="mix","Mixer schema/type");
    AudioMixAsset asset;asset.id=reference(value.at("asset"),"Mixer identity");const auto& buses=value.at("buses");require(buses.is_object()&&buses.size()==audio_bus_count,"A mixer lists master, music, ambient, player, npc and ui");
    for(std::size_t index=0;index<audio_bus_count;++index){require(buses.contains(bus_names[index]),"A mixer lists master, music, ambient, player, npc and ui");const auto& entry=buses.at(bus_names[index]);fields(entry,{"volume","mute"},{},"A mixer bus is {volume, mute}");asset.mix.volume[index]=number(entry.at("volume"),0,2,"Bus volume bounds (0 to 2)");asset.mix.mute[index]=flag(entry.at("mute"),"Bus mute flag type");}
    asset.mix.limit=flag(value.at("limit_each_sound"),"Limiter flag type");asset.mix.threshold_db=number(value.at("threshold_db"),-40,0,"Limiter threshold bounds (-40 to 0 dB)");
    const auto& ducking=value.at("ducking");require(ducking.is_array()&&ducking.size()<=8,"A mixer has at most eight ducking rules");
    for(const auto& entry:ducking){fields(entry,{"when","lower","to","fade_in","fade_out"},{},"A ducking rule is {when, lower, to, fade_in, fade_out}");AudioDuck duck;duck.when=bus(entry.at("when"),false,"A ducking rule names two of the five buses");duck.lower=bus(entry.at("lower"),false,"A ducking rule names two of the five buses");require(duck.when!=duck.lower,"A bus does not duck itself");
        duck.to=number(entry.at("to"),0,1,"Ducking gain bounds (0 to 1)");duck.fade_in=number(entry.at("fade_in"),0,10,"Ducking fade bounds (0 to 10 seconds)");duck.fade_out=number(entry.at("fade_out"),0,10,"Ducking fade bounds (0 to 10 seconds)");
        require(std::none_of(asset.mix.ducking.begin(),asset.mix.ducking.end(),[&](const auto& known){return known.when==duck.when&&known.lower==duck.lower;}),"Two ducking rules for the same pair of buses");asset.mix.ducking.push_back(duck);}
    return asset;
}
std::vector<AssetId> AudioProfile::sounds()const{std::vector<AssetId> all;if(music!=AssetId{})all.push_back(music);for(const auto& loop:ambient)if(std::find(all.begin(),all.end(),loop.sound)==all.end())all.push_back(loop.sound);return all;}
AudioProfile decode_audio_profile(std::string_view text){
    require(text.size()<=16384,"Audio profile size bound");const auto value=json(text,16384);fields(value,{"schema","kind","asset","name","music","music_volume","ambient","bus_offsets_db","crossfade_seconds"},{"sources"},"Audio profile fields");require(value.at("schema")==1&&value.at("kind")=="audio_profile","Audio profile schema/type");
    AudioProfile profile;profile.id=reference(value.at("asset"),"Audio profile identity");profile.name=display(value.at("name"),"Audio profile name bounds (1 to 64 characters)");if(!value.at("music").is_null())profile.music=reference(value.at("music"),"A profile's music is a sound definition or null");
    profile.music_volume=number(value.at("music_volume"),0,2,"Music volume bounds (0 to 2)");const auto& ambient=value.at("ambient");require(ambient.is_array()&&ambient.size()<=8,"A profile has at most eight ambient loops");
    for(const auto& entry:ambient){fields(entry,{"sound","volume"},{},"An ambient loop is {sound, volume}");AudioProfile::Loop loop{reference(entry.at("sound"),"An ambient loop names a sound definition"),number(entry.at("volume"),0,2,"Ambient volume bounds (0 to 2)")};require(std::none_of(profile.ambient.begin(),profile.ambient.end(),[&](const auto& known){return known.sound==loop.sound;}),"An ambient loop is listed twice");profile.ambient.push_back(loop);}
    const auto& offsets=value.at("bus_offsets_db");require(offsets.is_object()&&offsets.size()==audio_bus_count,"A profile gives an offset for master, music, ambient, player, npc and ui");for(std::size_t index=0;index<audio_bus_count;++index){require(offsets.contains(bus_names[index]),"A profile gives an offset for master, music, ambient, player, npc and ui");profile.bus_offsets_db[index]=number(offsets.at(bus_names[index]),-60,12,"Bus offset bounds (-60 to +12 dB)");}
    profile.crossfade_seconds=number(value.at("crossfade_seconds"),0,30,"Crossfade bounds (0 to 30 seconds)");
    if(value.contains("sources")){fields(value.at("sources"),{"sounds"},{},"A profile's locators are sources.sounds");const auto& located=value.at("sources").at("sounds");require(located.is_object(),"A profile's locators are sources.sounds");const auto named=profile.sounds();require(located.size()==named.size(),"A profile gives one locator for each sound it names");for(const auto id:named)require(located.contains(id.text())&&located.at(id.text()).is_string(),"A profile gives one locator for each sound it names");}
    return profile;
}
namespace assets_detail {
// A sound definition carries its WAV: the definition product requires the sound product, cooked here from the located file.
Import import_sound_definition(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,8192);auto data=json(bytes,8192);const auto sound=decode_sound_definition(bytes);require(data.contains("sources"),"A sound definition source locates its WAV in sources.wave");
    const auto wave=within(root,data.at("sources").at("wave").get<std::string>());require(wave.extension()==".wav","A sound definition's WAV is a .wav file of the project");auto sidecar=wave;sidecar+=".daimport";require(std::filesystem::is_regular_file(wave)&&std::filesystem::is_regular_file(sidecar),"A sound definition names a WAV that is not in the project");
    const auto sidecar_text=read(sidecar,1024*1024);const auto metadata=json(sidecar_text);require(metadata.at("schema")==1&&metadata.at("importer")=="sound-wav-v1"&&AssetId::parse(metadata.at("id").get<std::string>())==sound.wave,"A sound definition and the WAV it locates disagree");
    auto imported=import_sound(root,wave,{{"$source",sound.wave}},inspect);Import result;result.inputs=std::move(imported.inputs);result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.inputs[sidecar.lexically_relative(root).generic_string()]=sha256(sidecar_text);
    result.identities={sound.id,sound.wave};result.product_count=2;
    if(!inspect){require(ids.size()==1&&ids.at("$source")==sound.id,"Sound definition source UUID mismatch");data.erase("sources");result.products.push_back({sound.id,"sound-def","sound.json",data.dump(),{sound.wave}});for(auto& product:imported.products)result.products.push_back(std::move(product));}
    return result;
}
Import import_audio_mix(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,8192);const auto mix=decode_audio_mix(bytes);Import result;result.identities.push_back(mix.id);result.product_count=1;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);
    if(!inspect){require(ids.size()==1&&ids.at("$source")==mix.id,"Mixer source UUID mismatch");result.products.push_back({mix.id,"mix","mix.json",json(bytes,8192).dump(),{}});}return result;
}
// A profile carries its sound definitions, and through them their WAVs.
Import import_audio_profile(const std::filesystem::path& root,const std::filesystem::path& source,const std::map<std::string,AssetId>& ids,bool inspect){
    const auto bytes=read(source,16384);auto data=json(bytes,16384);const auto profile=decode_audio_profile(bytes);const auto named=profile.sounds();require(named.empty()||data.contains("sources"),"An audio profile source locates its sounds in sources.sounds");
    Import result;result.inputs[source.lexically_relative(root).generic_string()]=sha256(bytes);result.identities.push_back(profile.id);std::map<AssetId,Product> products;std::set<AssetId> closure;
    for(const auto id:named){const auto path=within(root,data.at("sources").at("sounds").at(id.text()).get<std::string>());require(path.extension()==".dasound"&&std::filesystem::is_regular_file(path),"An audio profile names a sound definition that is not in the project");
        auto imported=import_sound_definition(root,path,{{"$source",id}},inspect);require(decode_sound_definition(read(path,8192)).id==id,"An audio profile and the sound definition it locates disagree");
        for(const auto& [input,hash]:imported.inputs){const auto [found,inserted]=result.inputs.emplace(input,hash);require(inserted||found->second==hash,"An audio profile's source changed during cook");}
        for(const auto dependency:imported.identities){require(dependency!=profile.id,"Audio profile closure identity conflict");closure.insert(dependency);}
        for(auto& product:imported.products){const auto [found,inserted]=products.emplace(product.id,product);require(inserted||(found->second.kind==product.kind&&found->second.bytes==product.bytes),"An audio profile's sounds disagree about a shared product");}}
    result.identities.insert(result.identities.end(),closure.begin(),closure.end());result.product_count=1+closure.size();
    if(!inspect){require(ids.size()==1&&ids.at("$source")==profile.id,"Audio profile source UUID mismatch");data.erase("sources");result.products.push_back({profile.id,"audio-profile","audio-profile.json",data.dump(),named});for(auto& [id,product]:products)result.products.push_back(std::move(product));}
    return result;
}
}
CookedSoundDefinition load_cooked_sound_definition(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){return sound_definition(Records(registry,cas),registry,id);}
AudioMixAsset load_cooked_audio_mix(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){const Records records(registry,cas);auto mix=decode_audio_mix(records.load(id,"mix","mix.json").first);require(mix.id==id,"Cooked mixer identity mismatch");return mix;}
CookedAudioProfile load_cooked_audio_profile(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId id){
    const Records records(registry,cas);CookedAudioProfile cooked;cooked.profile=decode_audio_profile(records.load(id,"audio-profile","audio-profile.json").first);require(cooked.profile.id==id,"Cooked audio profile identity mismatch");
    for(const auto sound:cooked.profile.sounds())cooked.sounds.push_back(sound_definition(records,registry,sound));return cooked;
}
}
