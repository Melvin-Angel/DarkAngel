#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/combat_kit.hpp>
#include <memory>
#include <optional>

namespace darkangel {
// Server-side controller seam for a non-player actor. A controller only sees a bounded
// view of authoritative state and only asks for the operations a player intent carries:
// move, face and press a kit slot. The session validates the command and runs it through
// the ordinary motor and ability request paths, so a decision can never bypass ability
// validation, costs, cooldowns or hit resolution.
struct ControllerView {
    std::uint64_t tick{};
    MotorState self;bool alive{},busy{};
    // The actor this controller may act against; absent when there is none.
    std::optional<MotorState> opponent;bool opponent_alive{};
};
struct ControllerCommand {
    // World-space movement input, length at most 1.
    double move_x{},move_z{};
    // Requested yaw. When a slot is pressed on the same tick it applies only if the press is accepted.
    std::optional<double> face;
    std::optional<CombatSlot> press;
};
class ActorController {
public:
    virtual ~ActorController()=default;
    virtual ControllerCommand decide(const ControllerView&)=0;
};
// Scripted training attacker: presses one slot on a fixed schedule while both actors are
// alive, the opponent is within range and its own action is over. The first controller
// behind the seam; no decision logic beyond that.
struct ScriptedAttack {CombatSlot slot{CombatSlot::Light};std::uint64_t first_tick{60},interval{150};double range{2.5};bool face_opponent{true};};
std::shared_ptr<ActorController> make_scripted_attacker(ScriptedAttack);
}
