#include <darkangel/audio_director.hpp>
#include <algorithm>
#include <cmath>
namespace darkangel {
AudioDirector::AudioDirector(AudioService& service,AudioMix base):service_(service),base_(std::move(base)){apply();}
AudioDirector::~AudioDirector(){if(music_handle_)service_.stop(music_handle_,0);for(const auto& [sound,handle]:ambient_)service_.stop(handle,0);service_.mix(base_);}
void AudioDirector::mix(const AudioMix& base){base_=base;apply();}
void AudioDirector::scene(std::optional<AudioProfile> profile){scene_=std::move(profile);apply();}
void AudioDirector::push(std::uint64_t key,const AudioProfile& profile){if(layered(key))return;layers_.emplace_back(key,profile);apply();}
void AudioDirector::pop(std::uint64_t key){const auto before=layers_.size();std::erase_if(layers_,[&](const auto& layer){return layer.first==key;});if(layers_.size()!=before)apply();}
bool AudioDirector::layered(std::uint64_t key)const{return std::any_of(layers_.begin(),layers_.end(),[&](const auto& layer){return layer.first==key;});}
AudioDirectorStats AudioDirector::stats()const{const AudioProfile* top=layers_.empty()?(scene_?&*scene_:nullptr):&layers_.back().second;return {top?top->id:AssetId{},music_handle_&&service_.alive(music_handle_)?music_:AssetId{},layers_.size(),ambient_.size(),missing_};}
void AudioDirector::apply(){
    const AudioProfile* top=layers_.empty()?(scene_?&*scene_:nullptr):&layers_.back().second;missing_=0;
    // Music: the top profile's. The track that is leaving fades out over the leaving-to profile's crossfade time while the new one starts.
    const AssetId wanted=top?top->music:AssetId{};const float fade=top?top->crossfade_seconds:fade_;
    if(wanted!=music_||(wanted!=AssetId{}&&!service_.alive(music_handle_))){if(music_handle_)service_.stop(music_handle_,fade);music_handle_=0;music_=wanted;if(wanted!=AssetId{}){music_handle_=service_.play(wanted,{0,0,0});if(!music_handle_)++missing_;}}
    if(music_handle_&&top)service_.gain(music_handle_,top->music_volume);fade_=fade;
    // Ambience: every held profile's loops; a loop two profiles name plays once, at the volume of the upper one.
    std::map<AssetId,float> loops;if(scene_)for(const auto& loop:scene_->ambient)loops[loop.sound]=loop.volume;for(const auto& [key,profile]:layers_)for(const auto& loop:profile.ambient)loops[loop.sound]=loop.volume;
    for(auto at=ambient_.begin();at!=ambient_.end();)if(!loops.contains(at->first)){service_.stop(at->second,fade);at=ambient_.erase(at);}else ++at;
    for(const auto& [sound,volume]:loops){auto held=ambient_.find(sound);if(held==ambient_.end()||!service_.alive(held->second)){const auto handle=service_.play(sound,{0,0,0});if(!handle){++missing_;if(held!=ambient_.end())ambient_.erase(held);continue;}held=ambient_.insert_or_assign(sound,handle).first;}service_.gain(held->second,volume);}
    // Bus offsets of the top profile, over the project mixer.
    AudioMix effective=base_;if(top)for(std::size_t index=0;index<audio_bus_count;++index)effective.volume[index]=std::clamp(base_.volume[index]*std::pow(10.f,top->bus_offsets_db[index]/20.f),0.f,4.f);
    service_.mix(effective);
}
}
