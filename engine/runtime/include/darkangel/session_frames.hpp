#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/ability.hpp>
#include <darkangel/world.hpp>
#include <cstddef>
#include <cstdint>
namespace darkangel {
// Plain state the world session replicates: what scene sessions, observers and presentation read without the session.
struct AbilityCorrection {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityOwnerSnapshot ability;};
// The wire frame carries the ability snapshot and the avatar epoch, not the motor state: a client pairs it with the
// character's state from the motor stream. motor is that state at the frame's tick once the client holds it, otherwise
// the newest state it has.
struct AbilityPublicFrame {std::uint64_t network{},session_epoch{},world_revision{};MotorState motor;AbilityPublicSnapshot ability;std::uint64_t avatar_epoch{};};
// What a public ability snapshot says apart from its clock: attributes, tags, loadout and the running action. Two
// snapshots with the same content differ only in tick and revision, and in the clock of a running action by exactly
// its rate for each tick between them: an action is in the content by its identity, rate, phase and the origin of its
// clock (clock minus rate times tick). Server and client compute it with this one function.
std::uint64_t public_ability_content(const AbilityPublicSnapshot&);
// A held snapshot brought to a later tick whose index entry has the same content: the tick, and a running action's
// clock advanced by its rate.
AbilityPublicSnapshot public_ability_at(const AbilityPublicSnapshot&,std::uint64_t tick);
// The server's per-tick index of public ability state: for each character, the tick its state stands at and its
// content. A client holding a frame with the same content knows that frame is current without receiving it again.
struct PublicIndexEntry {std::uint64_t tick{},content{};};
// A projectile in flight as presentation sees it (velocity in metres per second). Authority owns the flight
// and its hit; this is public state and decides nothing.
struct ProjectileView {std::uint64_t id{};MotorVec position{},velocity{};};
// Projectiles one session flies at once; a launch beyond it is dropped.
inline constexpr std::size_t projectile_capacity=16;
// A client's request to use a placed interactable, as the server received it from the peer that owns that character,
// and the server's answer to it (an InteractionResult number). The request claims nothing; the host resolves it.
struct InteractionRequest {std::uint64_t network{};StableId object;};
struct InteractionNotice {std::uint64_t network{};StableId object;std::uint8_t result{};};
}
