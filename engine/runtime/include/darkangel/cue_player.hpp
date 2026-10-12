#pragma once
#include <darkangel/action.hpp>
#include <darkangel/effect_replication.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Presentation cues: the one seam between the moments the runtime already publishes (effect cue edges, action cue
// blocks, rejected activations, actors leaving) and whatever shows or sounds them. A cue player owns the lifecycle
// of live cue instances and forwards it to two sinks, one visual and one audio. It is client-side presentation: it
// reads confirmed or checked public state, decides nothing, and runs the same with no sinks (headless, no device).
//   visual, audio   what the sinks play; an empty name means that sink is not used for this cue
//   tail_ticks      how long an instance may fade after it is stopped before it is released
//   predictable     an owner may start it from its own prediction; it is cut if the activation is rejected
struct CueDefinition {AssetId id;std::string visual,audio;unsigned tail_ticks{30};bool predictable{};};
// What a sink is told when an instance starts. instance is unique within the player and never reused.
struct CueStart {std::uint64_t instance{},network{},tick{};std::string key;CueDefinition definition;};
class CueSink {
public:
    virtual ~CueSink()=default;
    virtual void start(const CueStart&)=0;
    // The instance ended; it may fade for at most tail_ticks and is then released.
    virtual void stop(std::uint64_t instance,unsigned tail_ticks)=0;
    // The instance is gone: free everything held for it.
    virtual void release(std::uint64_t instance)=0;
};
// live: instances the player holds at once, fading ones included. tail_ticks: the longest fade any cue gets.
// rejected_tail_ticks: the fade of a cue whose activation authority rejected.
struct CueLimits {std::size_t live{128};unsigned tail_ticks{180},rejected_tail_ticks{6};};
// Why an instance exists, for debug views.
struct CueInstanceInfo {std::uint64_t instance{},network{},started{},stopping_until{};std::string key;bool action{};};
class CuePlayer {
public:
    // The resolver turns a cue key (and the effect's cue asset, when there is one) into what to play; no answer
    // means the key has no presentation and is counted in unresolved().
    using Resolver=std::function<std::optional<CueDefinition>(std::string_view key,AssetId cue)>;
    CuePlayer(Resolver,CueSink* visual,CueSink* audio,CueLimits={});
    ~CuePlayer();
    CuePlayer(const CuePlayer&)=delete;CuePlayer& operator=(const CuePlayer&)=delete;
    // A persistent effect's cue: Begin starts it once, End stops it with its tail, Update changes nothing here.
    void effect(const EffectCueUpdate&,std::uint64_t tick);
    // A cue block of an actor's running action: Begin starts, End stops, Marker plays a one-shot. Other block kinds are ignored.
    void action(std::uint64_t network,std::uint64_t avatar_epoch,const ActionEvent&,std::uint64_t tick);
    // Authority rejected an activation: every cue started for it is cut with the short rejection tail.
    void rejected(std::uint64_t network,std::uint64_t activation,std::uint64_t tick);
    // An actor left, or its avatar changed: its instances are released at once, without a tail.
    void retire(std::uint64_t network);
    // Once per presented tick: releases instances whose tail is over.
    void advance(std::uint64_t tick);
    std::size_t live()const;
    std::uint64_t dropped()const{return dropped_;}
    std::uint64_t unresolved()const{return unresolved_;}
    std::vector<CueInstanceInfo> instances()const;
private:
    struct Source {std::uint64_t session_epoch{},network{},avatar_epoch{},handle{},activation{};unsigned block{},loop{};AssetId cue;bool action{};auto operator<=>(const Source&)const=default;};
    struct Instance {std::uint64_t id{},started{},until{};std::string key;bool visual{},audio{},stopping{};};
    bool start(const Source&,std::string_view key,AssetId cue,std::uint64_t tick);
    void stop(std::map<Source,Instance>::iterator,unsigned tail,std::uint64_t tick);
    void release(std::map<Source,Instance>::iterator);
    Resolver resolver_;CueSink* visual_{};CueSink* audio_{};CueLimits limits_;std::map<Source,Instance> instances_;std::uint64_t next_{1},dropped_{},unresolved_{};
};
}
