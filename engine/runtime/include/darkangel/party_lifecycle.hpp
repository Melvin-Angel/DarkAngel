#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/world_objects.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace darkangel {
// The party's rules for being downed, revived and wiped.
//   revive_range    how near a reviver must stand, metres
//   revive_seconds  how long Interact must be held
//   revive_health   the share of maximum Health a revived member gets back, above 0 and at most 1
//   wipe_seconds    how long the wipe presentation lasts before the party respawns
//   hold_grace      how long without a reviver's request still counts as holding (network jitter), seconds
struct PartyRules {double revive_range{2.5},revive_seconds{3},revive_health{.5},wipe_seconds{3},hold_grace{.25};};
void validate_party_rules(const PartyRules&);

// Host-only, server-authoritative state of each party member: Alive or Downed, revive progress, and the wipe. It knows
// members by network identity and reads nothing but what step() is given; SceneAuthority applies its decisions (blocks a
// downed member's input, restores Health on revive, raises the respawn travel on a wipe).
//   - A member whose Health reaches 0 is Downed. With nobody else connected and alive, that is a wipe at once.
//   - A living member holding Interact on a downed one within range advances its revive; any interruption (release,
//     out of range, the reviver downed, the target gone) resets it. Two revivers share one progress, and the revive
//     completes once.
//   - A wipe: every connected member is Downed. Disconnected members do not count and cannot prevent it.
class PartyLifecycleCoordinator {
public:
    explicit PartyLifecycleCoordinator(PartyRules={});
    struct Member {std::uint64_t network{};MotorVec position{};double health{};bool connected{true};};
    struct Outcome {std::vector<std::uint64_t> downed,revived;bool wipe{};};
    // A reviver asks, on this tick, to revive the target (it holds Interact on it).
    void hold(std::uint64_t reviver,std::uint64_t target);
    // One fixed tick: health and places as they are after the tick's damage.
    Outcome step(std::span<const Member>,double seconds);
    PartyMemberState state(std::uint64_t network)const;
    double progress(std::uint64_t network)const;
    bool wiped()const{return wiped_;}
    // Seconds since the wipe began; the owner respawns the party once it reaches the rules' wipe time.
    double wipe_elapsed()const{return wipe_elapsed_;}
    bool respawn_due()const{return wiped_&&wipe_elapsed_>=rules_.wipe_seconds;}
    const PartyRules& rules()const{return rules_;}
    // Members leaving the session.
    void forget(std::uint64_t network);
    std::vector<WorldPartyMember> view()const;
private:
    struct State {PartyMemberState state{};double progress{},since{};std::map<std::uint64_t,double> holders;};
    PartyRules rules_;std::map<std::uint64_t,State> members_;std::map<std::uint64_t,std::uint64_t> requests_;bool wiped_{};double wipe_elapsed_{};
};
}
