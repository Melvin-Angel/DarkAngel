#pragma once
#include <darkangel/transport.hpp>
#include <darkangel/world.hpp>
#include <darkangel/character_motor.hpp>

namespace darkangel {
// One party member's place in a scene session: its identity and the connection its client holds. The first seat is the
// scene's player object; a later seat's character is created at its position.
struct SceneSeat {
    StableId id;
    Transport* transport{};
    ConnectionHandle peer;
    MotorVec position{};
    double yaw{};
    StableId profile;
};
} // namespace darkangel
