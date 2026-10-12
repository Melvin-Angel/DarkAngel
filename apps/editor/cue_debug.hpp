#pragma once
#include <darkangel/cue_player.hpp>
#include <deque>
#include <map>
#include <string>
namespace darkangel::editor_app {
// Stands in for the visual or the audio service until one exists: it records what a cue player tells it to play,
// so Play can show that the right cue would start, fade and be released at the right time. It plays nothing.
class RecordingCueSink final:public CueSink {
public:
    struct Held {std::uint64_t network{},started{};std::string key,what;bool fading{};unsigned tail{};};
    explicit RecordingCueSink(bool visual):visual_(visual){}
    void start(const CueStart& begun)override{
        const auto& what=visual_?begun.definition.visual:begun.definition.audio;held_[begun.instance]={begun.network,begun.tick,begun.key,what,false,0};++starts_;
        note("tick "+std::to_string(begun.tick)+": start "+what+" ("+begun.key+") on actor "+std::to_string(begun.network));if(started_.size()<64)started_.push_back(begun.key);
    }
    void stop(std::uint64_t instance,unsigned tail)override{auto found=held_.find(instance);if(found==held_.end())return;found->second.fading=true;found->second.tail=tail;++stops_;note("stop "+found->second.what+" ("+found->second.key+"), fade "+std::to_string(tail)+" ticks");}
    void release(std::uint64_t instance)override{if(held_.erase(instance))++releases_;}
    void clear(){held_.clear();log_.clear();started_.clear();starts_=stops_=releases_=0;}
    const std::map<std::uint64_t,Held>& held()const{return held_;}
    const std::deque<std::string>& log()const{return log_;}
    // Keys of the first cues started since the last clear, in order.
    const std::vector<std::string>& started()const{return started_;}
    unsigned starts()const{return starts_;}
    unsigned stops()const{return stops_;}
    unsigned releases()const{return releases_;}
private:
    void note(std::string line){log_.push_back(std::move(line));while(log_.size()>24)log_.pop_front();}
    bool visual_{};std::map<std::uint64_t,Held> held_;std::deque<std::string> log_;std::vector<std::string> started_;unsigned starts_{},stops_{},releases_{};
};
}
