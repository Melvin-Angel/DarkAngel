#pragma once
#include <darkangel/action.hpp>
#include <darkangel/effect_replication.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
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
//   key, name       the cue key the asset answers to, and its display name (both empty for a definition made in code)
struct CueDefinition {AssetId id;std::string visual,audio;unsigned tail_ticks{30};bool predictable{};std::string key,name;AssetId visual_asset,audio_asset;
    // Native primitives beside the effect: a weapon trail emitted while the cue is live, a decal laid under the actor when it starts.
    std::string trail,decal;AssetId trail_asset,decal_asset;
    // What the visual side presents: an effect, a trail or a decal.
    bool visible()const{return !visual.empty()||!trail.empty()||!decal.empty();}};
// The native cue source (.dacue): schema 1, kind "cue", asset, name, key, visual, audio, tail_ticks, predictable.
// Optional: trail and decal (paths of a .datrail and a .dadecal). At least one of visual, audio, trail and decal is named. Throws on anything else. A cooked cue also carries visual_asset and audio_asset,
// the identities the cooker resolved the two names to (an empty string where nothing is named).
CueDefinition decode_cue(std::string_view);
// Whose action a cue block belongs to, as this client sees it. Another actor's action is Observed: confirmed public
// state, all of its cues play. The owner's own action arrives twice, from its prediction and from authority's
// correction: a predictable cue plays from the predicted one and every other cue from the confirmed one, so each
// plays once.
enum class CueTiming {Observed,OwnerPredicted,OwnerConfirmed};
// A cue block an actor's action crossed on a tick, and an action that ended before its time (interrupted, cancelled,
// or predicted and then rejected), whose cues are to be cut.
struct ActionCueUpdate {std::uint64_t network{},avatar_epoch{};CueTiming timing{};ActionEvent event;};
struct ActionCueCut {std::uint64_t network{},activation{};};
// Derives cue block crossings from the action state a client already holds for each actor: once a tick it is shown
// each actor's running action (identity and clock) and reports the cue blocks the clock passed since the tick
// before. It never reads a server timeline. A clock that moves backwards (a replayed prediction) reports nothing
// twice. An action first seen more than four ticks in is taken as already running and its earlier cues are skipped.
class ActionCueTracker {
public:
    explicit ActionCueTracker(std::span<const std::shared_ptr<const ActionDefinition>>);
    // One actor's action on this tick, or none. An action whose definition was not prepared is ignored.
    void observe(std::uint64_t network,std::uint64_t avatar_epoch,CueTiming,AssetId definition,const std::optional<ActionState>&);
    // The actor left: forget its actions without reporting anything.
    void retire(std::uint64_t network);
    std::vector<ActionCueUpdate> drain_cues();
    std::vector<ActionCueCut> drain_cuts();
private:
    struct Held {std::uint64_t avatar_epoch{},activation{},clock{};unsigned rate{};std::shared_ptr<const ActionDefinition> definition;};
    void finish(std::uint64_t network,CueTiming,const Held&);
    void markers(std::uint64_t network,CueTiming,const Held&,std::uint64_t from,std::uint64_t to,bool inclusive);
    std::map<AssetId,std::shared_ptr<const ActionDefinition>> actions_;std::map<std::pair<std::uint64_t,CueTiming>,Held> held_;
    std::vector<ActionCueUpdate> cues_;std::vector<ActionCueCut> cuts_;
};
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
// The cue key of a footstep. A step is a moment, not a block of an action or an edge of an effect.
inline constexpr const char* footstep_cue_key="Footstep";
// One step an actor took, counted per actor from one.
struct FootstepUpdate {std::uint64_t network{},avatar_epoch{},serial{};};
// Derives footsteps from where an actor stands each tick: a step every stride of ground covered while grounded.
// Time in the air covers no ground, and a jump in position of more than a metre in one tick (a teleport, a correction)
// is not walking. Client-side presentation; it reads positions the client already holds.
class FootstepTracker {
public:
    explicit FootstepTracker(double stride=.9);
    void observe(std::uint64_t network,std::uint64_t avatar_epoch,double x,double z,bool grounded);
    void retire(std::uint64_t network);
    std::vector<FootstepUpdate> drain();
private:
    struct Walk {std::uint64_t avatar_epoch{},serial{};double x{},z{},covered{};};
    double stride_{};std::map<std::uint64_t,Walk> walks_;std::vector<FootstepUpdate> steps_;
};
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
    // For the owner's own action the timing chooses which of its two streams a cue plays from (see CueTiming).
    void action(std::uint64_t network,std::uint64_t avatar_epoch,const ActionEvent&,std::uint64_t tick,CueTiming=CueTiming::Observed);
    // A moment of an actor that is neither of the above (a footstep): a one-shot by key. serial tells one moment of an
    // actor from the next, so the same moment fed twice plays once.
    void moment(std::uint64_t network,std::uint64_t avatar_epoch,std::string_view key,std::uint64_t serial,std::uint64_t tick);
    // Authority rejected an activation, or the action was cut short: every cue started for it is cut with the short rejection tail.
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
