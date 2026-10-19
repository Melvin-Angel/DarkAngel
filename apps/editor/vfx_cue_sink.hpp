#pragma once
#include <darkangel/cue_player.hpp>
#include <darkangel/vfx.hpp>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
namespace darkangel::editor_app {
// The visual side of presentation cues: a cue's visual names an effect asset by its source path, and the cue
// player's start, stop and release become play, stop spawning and remove on the visual effect service. An effect
// follows its actor while it lives. Everything it is told also goes to an optional second sink (the recorder
// behind the Cues tab). Without a service it only forwards.
class VfxCueSink final:public CueSink {
public:
    struct Anchor {std::array<float,3> position{};float yaw{};};
    // Where an actor's effects attach, by network identity; nothing when the actor is not presented.
    using Locate=std::function<std::optional<Anchor>(std::uint64_t network,std::string_view socket)>;
    explicit VfxCueSink(CueSink* also):also_(also){}
    void bind(VfxService* service,const std::map<std::string,AssetId>* effects,Locate locate){clear();service_=service;effects_=effects;locate_=std::move(locate);}
    void start(const CueStart& begun)override{
        if(also_)also_->start(begun);if(!service_||!effects_||!locate_||begun.definition.visual.empty())return;const auto effect=effects_->find(begun.definition.visual);
        if(effect==effects_->end()||!service_->loaded(effect->second)){++missing_;return;}const auto at=locate_(begun.network,begun.definition.socket);if(!at)return;
        if(const auto handle=service_->play(effect->second,position(*at,begun.definition.offset),begun.definition.scale))held_[begun.instance]={handle,begun.network,frame_,begun.definition.socket,begun.definition.offset};
    }
    void stop(std::uint64_t instance,unsigned tail)override{
        if(also_)also_->stop(instance,tail);const auto found=held_.find(instance);if(found==held_.end()||!service_)return;
        // A one-shot is stopped on the tick it starts: it plays out and is removed on release. A lasting effect stops spawning and fades.
        if(frame_>found->second.started+2)service_->stop(found->second.handle);
    }
    void release(std::uint64_t instance)override{if(also_)also_->release(instance);const auto found=held_.find(instance);if(found==held_.end())return;if(service_)service_->kill(found->second.handle);held_.erase(found);}
    // Once a presented frame: effects move with their actors.
    void follow(){++frame_;if(!service_||!locate_)return;for(const auto& [instance,held]:held_){(void)instance;if(const auto at=locate_(held.network,held.socket))service_->move(held.handle,position(*at,held.offset));}}
    void clear(){if(service_)for(const auto& [instance,held]:held_){(void)instance;service_->kill(held.handle);}held_.clear();missing_=0;}
    std::size_t playing()const{return held_.size();}
    // Cues whose visual names no loaded effect since the last bind.
    unsigned missing()const{return missing_;}
private:
    static std::array<float,3> position(const Anchor& anchor,const std::array<float,3>& offset){const float c=std::cos(anchor.yaw),s=std::sin(anchor.yaw);return {anchor.position[0]+c*offset[0]+s*offset[2],anchor.position[1]+offset[1],anchor.position[2]-s*offset[0]+c*offset[2]};}
    struct Held {std::uint64_t handle{},network{},started{};std::string socket;std::array<float,3> offset{};};
    CueSink* also_{};VfxService* service_{};const std::map<std::string,AssetId>* effects_{};Locate locate_;std::map<std::uint64_t,Held> held_;std::uint64_t frame_{};unsigned missing_{};
};
}
