#pragma once
#include <darkangel/audio_assets.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
namespace darkangel {
// device: false runs the whole service on Amplitude's silent driver (no audio device, a no-audio flag, a headless
// run); sounds still start, last their length and end, so nothing above the service changes.
// work: a directory the service may write its generated project into; it makes and removes its own folder there.
// schemas: the directory with Amplitude's compiled schemas; empty means the SDK this build was configured with.
// mix: the project mixer. Its limiter and ducking rules are fixed for the life of the service; volumes and mutes are not.
struct AudioOptions {bool device{true};std::filesystem::path work,schemas;AudioMix mix;};
// One playable sound: a WAV with how it is played. id is what play() is asked for (the sound definition's identity, or
// the WAV's own when it is played with default settings); generation changes whenever the WAV or the settings do.
struct AudioSound {AssetId id;std::string generation;const CookedSound* wave{};SoundSettings settings;};
struct AudioStats {bool device{};std::string driver;std::size_t sounds{},playing{};std::uint64_t started{};std::array<float,audio_bus_count> bus_gain{};std::array<bool,audio_bus_count> bus_muted{};bool limit{};std::size_t ducking{};};
// Sounds through the Amplitude engine. Client-side presentation only; a process has at most one. Sounds are
// loaded from cooked products as a set and played by handle at a world position.
class AudioService {
public:
    explicit AudioService(AudioOptions);
    ~AudioService();
    AudioService(const AudioService&)=delete;AudioService& operator=(const AudioService&)=delete;
    // Makes these sounds playable. A sound already loaded in the same generation is left alone; a new generation of
    // an asset replaces the one that is played from then on. Sounds are not unloaded while the service lives.
    void load(std::span<const CookedSound>);
    // The same for sounds with settings: bus, gain, pitch, loop, positioned or flat, range and one effect.
    void load(std::span<const AudioSound>);
    // Bus volumes and mutes, at once. The limiter and ducking of the mix the service started with stay as they are.
    void mix(const AudioMix&);
    const AudioMix& started_mix()const;
    bool loaded(AssetId)const;
    // Starts a sound at a world position; zero when the asset is not loaded or no channel is free.
    std::uint64_t play(AssetId,std::array<float,3> position);
    void move(std::uint64_t handle,std::array<float,3> position);
    // A volume for this one playing sound, over its definition's gain (0 to 4).
    void gain(std::uint64_t handle,float value);
    // Fades the sound out over this many seconds of update() time and ends it; zero ends it at once.
    void stop(std::uint64_t handle,double fade_seconds);
    bool alive(std::uint64_t handle)const;
    void listener(std::array<float,3> position,std::array<float,3> forward,std::array<float,3> up);
    void update(double seconds);
    AudioStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
