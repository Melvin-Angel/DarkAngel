#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/combat_kit.hpp>
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
// Another actor in the session as a controller sees it. Faction is a session-local number:
// equal numbers are the same faction, different numbers are hostile.
struct ControllerActor {StableId id;std::uint32_t faction{};MotorState motor;bool alive{};};
struct ControllerView {
    std::uint64_t tick{};
    MotorState self;bool alive{},busy{};std::uint32_t faction{};
    // Every other actor in the session, the player included, in ascending network identity.
    // Valid only for the duration of decide().
    std::span<const ControllerActor> others;
};
// The nearest live actor of another faction; the earlier entry wins an exact tie. Stands in
// for target selection until NPC sensing exists.
const ControllerActor* nearest_hostile(const ControllerView&);
struct ControllerCommand {
    // World-space movement input, length at most 1.
    double move_x{},move_z{};
    // Requested yaw. When a slot is pressed on the same tick it applies only if the press is accepted.
    std::optional<double> face;
    std::optional<CombatSlot> press;
    // The actor this decision is aimed at, if any. It must be one of the view's others. The
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
