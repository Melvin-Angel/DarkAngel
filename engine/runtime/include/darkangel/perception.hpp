#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/world.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace darkangel {
// NPC sensing. Senses are authored on the NPC definition; each spawned actor owns one
// Perception built from them. Sensing is server-only: the session feeds it authoritative
// state once per tick and supplies the line of sight query, so this module does no physics.
//   Distance     an actor within range of the NPC's position, in any direction, through walls
//   Vision       a cone from the eye along the actor's yaw: within range, within the horizontal
//                and vertical half angles (degrees), and with line of sight
//   LineOfSight  an actor within range in any direction, with line of sight
// Line of sight runs from the eye (position raised by eye_height) to the subject's body point.
enum class SensorKind:std::uint8_t{Distance,Vision,LineOfSight};
struct SensorDefinition {SensorKind kind{};double range{},horizontal{},vertical{};};
constexpr std::size_t sensor_limit=8;
// The height above an actor's position that sensing and NPC aim treat as its body.
constexpr double perception_body_height=1.0;
// No sensors and not everything: the NPC perceives nothing. everything detects every actor
// each tick and excludes sensors. A lost actor is remembered for forget_seconds.
struct SensesDefinition {bool everything{};double eye_height{1.6},forget_seconds{5};std::vector<SensorDefinition> sensors;};
void validate_senses(const SensesDefinition&);
// Another actor as sensing is given it. Faction is the session-local number.
struct SensedSubject {StableId id;std::uint32_t faction{};MotorState motor;bool alive{};};
// An actor an NPC perceives. Faction is a session-local number: equal numbers are the same
// faction, different numbers are hostile. While detected, motor and alive are current; once
// lost they are the last state it was detected in. visible is line of sight on this tick.
// since is the tick the current detection began (kept once lost); seen is the last tick it was detected.
struct PerceivedActor {StableId id;std::uint32_t faction{};MotorState motor;bool alive{},detected{},visible{};std::uint64_t since{},seen{};};
// True when nothing static lies between the two points.
using SightQuery=std::function<bool(MotorVec from,MotorVec to)>;
class Perception {
public:
    explicit Perception(SensesDefinition);
    const SensesDefinition& senses()const{return senses_;}
    // One pass over the subjects in the order given: at most one sight query per subject.
    // A dead self perceives nothing and forgets everything.
    void sense(std::uint64_t tick,const MotorState& self,bool alive,std::span<const SensedSubject> subjects,const SightQuery& sight);
    // Detected and remembered subjects, in subject order.
    std::span<const PerceivedActor> actors()const{return actors_;}
private:SensesDefinition senses_;std::vector<PerceivedActor> actors_,previous_;
};
}
