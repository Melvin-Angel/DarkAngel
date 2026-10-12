#pragma once
#include <darkangel/presentation_primitives.hpp>
#include <functional>
#include <map>
#include <optional>
namespace darkangel::editor_app {
// The native-primitive side of presentation cues: a cue's trail emits while the cue is live (the host samples the
// weapon socket once a presented tick), and a cue's decal is laid under its actor when the cue starts. It stands in
// front of another sink (the effect sink's recorder) and forwards everything it is told.
class PrimitiveCueSink final:public CueSink {
public:
    // Where an actor stands (its feet), by network identity; nothing when the actor is not presented.
    using Locate=std::function<std::optional<std::array<float,3>>(std::uint64_t network)>;
    explicit PrimitiveCueSink(CueSink* also):also_(also){}
    void bind(const std::map<AssetId,TrailDefinition>* trail_assets,const std::map<AssetId,DecalDefinition>* decal_assets,Locate feet){trails=WeaponTrails{};decals.clear();missing_=0;trail_assets_=trail_assets;decal_assets_=decal_assets;feet_=std::move(feet);}
    void start(const CueStart& begun)override{
        if(also_)also_->start(begun);
        if(trail_assets_&&begun.definition.trail_asset!=AssetId{}){if(const auto found=trail_assets_->find(begun.definition.trail_asset);found!=trail_assets_->end())trails.begin(begun.instance,begun.network,found->second);else ++missing_;}
        if(decal_assets_&&begun.definition.decal_asset!=AssetId{}){const auto found=decal_assets_->find(begun.definition.decal_asset);if(found==decal_assets_->end())++missing_;else if(feet_)if(const auto at=feet_(begun.network))decals.spawn(found->second,*at,{0,1,0},begun.tick);}
    }
    void stop(std::uint64_t instance,unsigned tail)override{if(also_)also_->stop(instance,tail);}
    // The cue is over: the trail stops emitting and what is drawn of it fades out.
    void release(std::uint64_t instance)override{if(also_)also_->release(instance);trails.end(instance);}
    // Cues whose trail or decal is not loaded since the last bind.
    unsigned missing()const{return missing_;}
    WeaponTrails trails;DecalPool decals;
private:
    CueSink* also_{};const std::map<AssetId,TrailDefinition>* trail_assets_{};const std::map<AssetId,DecalDefinition>* decal_assets_{};Locate feet_;unsigned missing_{};
};
}
