#pragma once
#include <darkangel/audio.hpp>
#include <map>
#include <optional>
#include <vector>
namespace darkangel {
struct AudioDirectorStats {AssetId profile,music;std::size_t layers{},ambient{},missing{};};
// Which music and ambience play, from audio profiles. One profile belongs to the scene; others are layered over it while
// something lasts (an encounter) and taken off again. The top profile's music plays and its bus offsets apply; the ambient
// loops of every held profile play together. It knows nothing about gameplay: the host pushes and pops. Sounds must
// already be loaded into the service; a profile sound that is not loaded is counted and skipped.
class AudioDirector {
public:
    AudioDirector(AudioService&,AudioMix base);
    ~AudioDirector();
    AudioDirector(const AudioDirector&)=delete;AudioDirector& operator=(const AudioDirector&)=delete;
    // The project mixer changed: volumes and mutes are applied at once, under the top profile's offsets.
    void mix(const AudioMix& base);
    void scene(std::optional<AudioProfile>);
    // Layers are a stack; a key already on it is left where it is.
    void push(std::uint64_t key,const AudioProfile&);
    void pop(std::uint64_t key);
    bool layered(std::uint64_t key)const;
    AudioDirectorStats stats()const;
private:
    void apply();
    AudioService& service_;AudioMix base_;std::optional<AudioProfile> scene_;std::vector<std::pair<std::uint64_t,AudioProfile>> layers_;
    AssetId music_;std::uint64_t music_handle_{};std::map<AssetId,std::uint64_t> ambient_;std::size_t missing_{};float fade_{2};
};
}
