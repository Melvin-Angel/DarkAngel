#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/world_events.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// An encounter is authored on a scene object. It starts once, on a world event or when the
// player first stands inside its bounds, and then spawns NPCs in waves (the next wave when
// the current one is dead) or on a timer (one at a time, with a cap on how many are alive).
// Enemies appear at the encounter's own spawn points or at random points inside its bounds.
// It can emit world events and hold a world tag while it runs. Server-only.
struct EncounterWave {AssetId npc;unsigned count{};};
enum class EncounterMode:std::uint8_t{Waves,Timed};
enum class EncounterPlacement:std::uint8_t{Points,Random};
struct EncounterDefinition {
    // Exactly one trigger: a world event name, or the player entering the bounds.
    std::string trigger_event;bool trigger_enter{};
    EncounterMode mode{};std::vector<EncounterWave> waves;
    AssetId timed_npc;double interval_seconds{};unsigned max_alive{},total{};
    EncounterPlacement placement{};
    // Optional world event names and an optional world tag held while running.
    std::string started,wave_cleared,completed,tag;
    // How many enemies it spawns over its whole life.
    unsigned enemies()const;
    // The most that can be alive at once.
    unsigned concurrent()const;
    // Every NPC definition it can spawn, without repeats, in first-use order.
    std::vector<AssetId> npcs()const;
};
constexpr unsigned encounter_wave_limit=8,encounter_enemy_limit=64,encounter_volume_limit=16,encounter_point_limit=32;
void validate_encounter(const EncounterDefinition&);
// Decodes and validates the "encounter" component of a scene object.
EncounterDefinition decode_encounter(std::string_view json);
// A box centred on a scene object and turned by its yaw. An encounter's bounds are the union of its volumes.
struct EncounterVolume {MotorVec center{};double yaw{};MotorVec half{};};
struct EncounterPoint {MotorVec position{};double yaw{};};
bool encounter_contains(std::span<const EncounterVolume>,MotorVec point);
// A trigger volume: a placed box that emits a world event when the player walks into it.
// once: only the first entry emits; otherwise every entry does (leaving re-arms it).
struct TriggerDefinition {std::string event;bool once{true};MotorVec half{};};
// Decodes and validates the "trigger" component of a scene object.
TriggerDefinition decode_trigger(std::string_view json);
class WorldTrigger {
public:
    WorldTrigger(TriggerDefinition,MotorVec center,double yaw);
    const TriggerDefinition& definition()const{return definition_;}
    const EncounterVolume& volume()const{return volume_;}
    // One tick: emits on the tick the player is first found inside. Returns true when it emitted.
    bool step(WorldEvents& world,MotorVec player,bool player_alive);
    bool inside()const{return inside_;}
    unsigned fired()const{return fired_;}
private:TriggerDefinition definition_;EncounterVolume volume_;bool inside_{};unsigned fired_{};
};
// One enemy to bring into the session: index counts the encounter's spawns from 0.
struct EncounterSpawn {AssetId npc;unsigned index{};MotorVec position{};double yaw{};};
enum class EncounterPhase:std::uint8_t{Waiting,Running,Complete};
struct EncounterStatus {EncounterPhase phase{};unsigned wave{},waves{},spawned{},total{},alive{};};
// Pure decision logic for one encounter; the session carries out what it returns.
class EncounterDirector {
public:
    // Height of the first static ground at or below (x, from, z), if any.
    using Ground=std::function<std::optional<double>(double x,double from,double z)>;
    // True when a living character stands within reach of the point.
    using Occupied=std::function<bool(MotorVec)>;
    EncounterDirector(EncounterDefinition,std::vector<EncounterVolume>,std::vector<EncounterPoint>,std::uint64_t seed);
    const EncounterDefinition& definition()const{return definition_;}
    std::span<const EncounterVolume> volumes()const{return volumes_;}
    // One tick. alive is how many of this encounter's enemies are alive now; every spawn
    // returned earlier counts as spawned. Emits its events and sets its tag on the world.
    std::vector<EncounterSpawn> step(std::uint64_t tick,WorldEvents& world,MotorVec player,unsigned alive,const Ground& ground,const Occupied& occupied);
    EncounterStatus status()const;
private:
    EncounterSpawn place(AssetId npc,const Ground& ground,const Occupied& occupied,std::span<const EncounterSpawn> placed);
    std::vector<EncounterSpawn> wave(const Ground& ground,const Occupied& occupied);
    EncounterDefinition definition_;std::vector<EncounterVolume> volumes_;std::vector<EncounterPoint> points_;
    std::uint64_t random_{};EncounterPhase phase_{};unsigned wave_{},spawned_{},alive_{};std::uint64_t spawned_tick_{},next_spawn_{};
};
}
