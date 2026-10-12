#pragma once
#include <darkangel/cue_player.hpp>
#include <darkangel/audio.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
namespace darkangel::editor_app {
// The audio side of presentation cues: a cue's audio names a sound asset by its source path. A cue that starts
// plays the sound at its actor; a cue stopped after it has run for a while fades the sound by the cue's tail. A
// one-shot cue is stopped on the tick it starts, and its sound simply plays out. Everything it is told also goes
// to an optional second sink (the recorder behind the Cues tab). Without a service it only forwards.
class AudioCueSink final:public CueSink {
public:
    using Locate=std::function<std::optional<std::array<float,3>>(std::uint64_t network)>;
    explicit AudioCueSink(CueSink* also):also_(also){}
    void bind(AudioService* service,const std::map<std::string,AssetId>* sounds,Locate locate){held_.clear();missing_=0;service_=service;sounds_=sounds;locate_=std::move(locate);}
    void start(const CueStart& begun)override{
        if(also_)also_->start(begun);if(!service_||!sounds_||!locate_)return;const auto sound=sounds_->find(begun.definition.audio);
        if(sound==sounds_->end()||!service_->loaded(sound->second)){++missing_;return;}const auto at=locate_(begun.network);if(!at)return;
        if(const auto handle=service_->play(sound->second,*at))held_[begun.instance]={handle,begun.network,frame_};
    }
    void stop(std::uint64_t instance,unsigned tail)override{
        if(also_)also_->stop(instance,tail);const auto found=held_.find(instance);if(found==held_.end()||!service_)return;
        if(frame_>found->second.started+2)service_->stop(found->second.handle,double(tail)/60.);
    }
    void release(std::uint64_t instance)override{if(also_)also_->release(instance);held_.erase(instance);}
    // Once a presented frame: sounds move with their actors.
    void follow(){++frame_;if(!service_||!locate_)return;for(const auto& [instance,held]:held_){(void)instance;if(const auto at=locate_(held.network))service_->move(held.handle,*at);}}
    // Cues whose audio names no loaded sound since the last bind.
    unsigned missing()const{return missing_;}
private:
    struct Held {std::uint64_t handle{},network{},started{};};
    CueSink* also_{};AudioService* service_{};const std::map<std::string,AssetId>* sounds_{};Locate locate_;std::map<std::uint64_t,Held> held_;std::uint64_t frame_{};unsigned missing_{};
};
}
