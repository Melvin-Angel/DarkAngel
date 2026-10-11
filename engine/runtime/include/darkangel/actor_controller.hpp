#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/combat_kit.hpp>
#include <darkangel/perception.hpp>
#include <darkangel/world.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace darkangel {
// Server-side controller seam for a non-player actor. A controller only sees a bounded
// view of authoritative state and only asks for the operations a player intent carries:
// move, face and press a kit slot. The session validates the command and runs it through
// the ordinary motor and ability request paths, so a decision can never bypass ability
// validation, costs, cooldowns or hit resolution.
struct ControllerView {
    std::uint64_t tick{};
    MotorState self;bool alive{},busy{};std::uint32_t faction{};
    // What the actor's senses perceive, the player included, in ascending network identity:
    // the actors detected on this tick and the lost ones it still remembers (detected false,
    // last known state). An actor it does not perceive is not here. Valid only for the
    // duration of decide().
    std::span<const PerceivedActor> perceived;
};
// The nearest live actor of another faction among those detected on this tick; the earlier
// entry wins an exact tie. Faction decides who is hostile, senses decide who is noticed.
const PerceivedActor* nearest_hostile(const ControllerView&);
struct ControllerCommand {
    // World-space movement input, length at most 1.
    double move_x{},move_z{};
    // Requested yaw. When a slot is pressed on the same tick it applies only if the press is accepted.
    std::optional<double> face;
    std::optional<CombatSlot> press;
    // The actor this decision is aimed at, if any. It must be detected in the view. The
    // session records it as the actor's chosen target for shared targeting; it grants nothing.
    std::optional<StableId> target;
};
class ActorController {
public:
    virtual ~ActorController()=default;
    virtual ControllerCommand decide(const ControllerView&)=0;
    // Short description of the latest decision for debug readouts. Presentation only.
    virtual std::string status()const{return {};}
};
// Scripted training attacker: presses one slot on a fixed schedule while it and its nearest
// hostile are alive, the hostile is within range and its own action is over. The first
// controller behind the seam; no decision logic beyond that.
struct ScriptedAttack {CombatSlot slot{CombatSlot::Light};std::uint64_t first_tick{60},interval{150};double range{2.5};bool face_opponent{true};};
std::shared_ptr<ActorController> make_scripted_attacker(ScriptedAttack);
}
