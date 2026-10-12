#pragma once
#include <darkangel/assets.hpp>
#include <string>
namespace darkangel {
// What a WAV file holds. Sound sources are 16 or 24 bit PCM or 32 bit float, mono or stereo, 8 to 96 kHz.
struct SoundFormat {unsigned channels{},rate{},bits{};bool floating{};std::size_t frames{};};
SoundFormat read_sound_format(std::string_view wave);
// A cooked sound: the validated WAV bytes. generation is the product digest.
struct CookedSound {AssetId id;std::string wave;SoundFormat format;std::string generation;};
CookedSound load_cooked_sound(const std::filesystem::path& registry,const std::filesystem::path& cas,AssetId);
}
