#pragma once
#include <darkangel/sound_assets.hpp>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// The project's buses. Master is the parent of the other five; a sound plays on one of the five.
enum class AudioBus:std::uint8_t {Master,Music,Ambient,Player,Npc,Ui};
constexpr std::size_t audio_bus_count=6;
const char* audio_bus_name(AudioBus);
std::optional<AudioBus> audio_bus(std::string_view);

// One effect on a sound (the audio engine takes one per sound). Parameters are in the order of the kind's table.
struct SoundEffectParameter {const char* name;float least,most,initial;};
struct SoundEffectKind {const char* kind;const char* label;std::vector<SoundEffectParameter> parameters;};
std::span<const SoundEffectKind> sound_effect_kinds();
struct SoundEffect {std::string kind{"none"};std::vector<float> parameters;bool operator==(const SoundEffect&)const=default;};
// How a sound is played: fixed when it is loaded into the audio service.
struct SoundSettings {AudioBus bus{AudioBus::Player};float gain{1},pitch{1};bool loop{},positioned{true};float range{40};SoundEffect effect;bool operator==(const SoundSettings&)const=default;};

// The native sound definition (.dasound): schema 1, kind "sound", asset, name, wave (the WAV asset), bus, gain, pitch, loop,
// positioned, range, effect {kind, parameters {name: value}}, and in a source its locator sources.wave. Throws on anything else.
struct SoundDefinitionAsset {AssetId id,wave;std::string name;SoundSettings settings;};
SoundDefinitionAsset decode_sound_definition(std::string_view);

// The project mixer (.damix): schema 1, kind "mix", asset, buses {name: {volume, mute}} for all six, limit_each_sound,
// threshold_db, ducking [{when, lower, to, fade_in, fade_out}]. The limiter and the ducking rules are read when the audio
// service starts; volumes and mutes may change while it runs.
struct AudioDuck {AudioBus when{AudioBus::Ui},lower{AudioBus::Music};float to{.5f},fade_in{.2f},fade_out{.5f};bool operator==(const AudioDuck&)const=default;};
struct AudioMix {std::array<float,audio_bus_count> volume{1,1,1,1,1,1};std::array<bool,audio_bus_count> mute{};bool limit{};float threshold_db{-1};std::vector<AudioDuck> ducking;
    // True when the other mix differs only in what may change while the service runs.
    bool same_structure(const AudioMix& other)const{return limit==other.limit&&threshold_db==other.threshold_db&&ducking==other.ducking;}
    bool operator==(const AudioMix&)const=default;};
struct AudioMixAsset {AssetId id;AudioMix mix;};
AudioMixAsset decode_audio_mix(std::string_view);

// An audio profile (.daaudioprofile): schema 1, kind "audio_profile", asset, name, music (a sound definition or null),
// music_volume, ambient [{sound, volume}] (at most eight), bus_offsets_db {name: decibels} for all six, crossfade_seconds,
// and in a source the locators sources.sounds {id: path}.
struct AudioProfile {struct Loop {AssetId sound;float volume{1};};AssetId id;std::string name;AssetId music;float music_volume{1};std::vector<Loop> ambient;std::array<float,audio_bus_count> bus_offsets_db{};float crossfade_seconds{2};
    std::vector<AssetId> sounds()const;};
AudioProfile decode_audio_profile(std::string_view);

// Cooked products, read from a verified registry. A sound definition brings its WAV; a profile brings its definitions.
struct CookedSoundDefinition {SoundDefinitionAsset definition;std::string generation;CookedSound wave;};
CookedSoundDefinition load_cooked_sound_definition(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
AudioMixAsset load_cooked_audio_mix(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
struct CookedAudioProfile {AudioProfile profile;std::vector<CookedSoundDefinition> sounds;};
CookedAudioProfile load_cooked_audio_profile(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
