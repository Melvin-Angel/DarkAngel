#pragma once
#include <darkangel/cue_player.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// Native presentation primitives (decision 018): weapon trails and decals. Client-side only; they decide nothing,
// are bounded, and are built into plain coloured quads a renderer draws. Four vertices to a quad: 0 and 1 are one
// edge, 2 and 3 the opposite edge (0 beside 2, 1 beside 3).
struct PrimitiveVertex {std::array<float,3> position{};std::array<float,4> color{};};

// The native weapon trail source (.datrail): schema 1, kind "trail", asset, name, socket (a rig socket or joint),
// base and tip (offsets from that joint, in its own space, metres), lifetime_ticks (1 to 120), max_points (2 to 64),
// color [r,g,b,a] (0 to 1). Throws on anything else.
struct TrailDefinition {AssetId id;std::string name,socket;std::array<float,3> base{},tip{};unsigned lifetime_ticks{12},max_points{24};std::array<float,4> color{1,1,1,.6f};};
TrailDefinition decode_trail(std::string_view);
// The native decal source (.dadecal): schema 1, kind "decal", asset, name, size (diameter, 0.05 to 10 metres),
// lifetime_ticks (1 to 3600), fade_ticks (0 to lifetime_ticks), color [r,g,b,a]. A soft disc; no texture yet.
struct DecalDefinition {AssetId id;std::string name;float size{1};unsigned lifetime_ticks{600},fade_ticks{120};std::array<float,4> color{0,0,0,.5f};};
DecalDefinition decode_decal(std::string_view);

// Weapon trails: while a trail emits, the host gives it the two socket points once a tick; the strip between
// consecutive pairs fades by age. A pair further than break_distance from the one before (a teleport, a correction,
// a new avatar) breaks the strip, so a trail never spans the map.
class WeaponTrails {
public:
    static constexpr float break_distance=3.f;
    struct Request {std::uint64_t instance{},network{};const TrailDefinition* definition{};};
    explicit WeaponTrails(std::size_t limit=32);
    // Opens emission for an instance (a cue instance). A second begin for a held instance is ignored. A full set
    // drops the new trail and counts it.
    bool begin(std::uint64_t instance,std::uint64_t network,const TrailDefinition&);
    // Emission ends; what is drawn fades out and the trail is then forgotten.
    void end(std::uint64_t instance);
    // An actor left: its trails go at once.
    void retire(std::uint64_t network);
    // The trails that want a sample this tick.
    std::vector<Request> emitting()const;
    void sample(std::uint64_t instance,std::uint64_t tick,std::array<float,3> base,std::array<float,3> tip);
    // Drops points older than their lifetime and trails that have ended and faded.
    void advance(std::uint64_t tick);
    void build(std::uint64_t tick,std::vector<PrimitiveVertex>& out)const;
    std::size_t live()const{return trails_.size();}
    std::size_t points()const;
    unsigned dropped()const{return dropped_;}
    unsigned breaks()const{return breaks_;}
private:
    struct Point {std::uint64_t tick{};std::array<float,3> base{},tip{};bool first{};};
    struct Trail {std::uint64_t network{};TrailDefinition definition;std::vector<Point> points;bool emitting{true};};
    std::map<std::uint64_t,Trail> trails_;std::size_t limit_;unsigned dropped_{},breaks_{};
};

// Decals: a bounded pool of soft discs laid on a surface. A full pool gives the place of its oldest decal to a new one.
class DecalPool {
public:
    explicit DecalPool(std::size_t limit=64);
    void spawn(const DecalDefinition&,std::array<float,3> position,std::array<float,3> normal,std::uint64_t tick);
    void advance(std::uint64_t tick);
    void build(std::uint64_t tick,std::vector<PrimitiveVertex>& out)const;
    void clear(){decals_.clear();}
    std::size_t live()const{return decals_.size();}
    unsigned replaced()const{return replaced_;}
private:
    struct Decal {DecalDefinition definition;std::array<float,3> position{},normal{};std::uint64_t born{};};
    std::vector<Decal> decals_;std::size_t limit_;unsigned replaced_{};
};
}
