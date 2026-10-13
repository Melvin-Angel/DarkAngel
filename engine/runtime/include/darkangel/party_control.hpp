#pragma once
#include <darkangel/transport.hpp>
#include <darkangel/world.hpp>
#include <darkangel/asset_id.hpp>
#include <memory>
#include <optional>
#include <span>

namespace darkangel {
// Party identities survive scene replacement; avatars are the authored/runtime identities of the current scene.
struct PartyMember {StableId profile,avatar;unsigned seat{};bool operator==(const PartyMember&)const=default;};
struct PartyDestination {AssetId scene;StableId entry;SessionHandshake hello;std::vector<PartyMember> members;std::vector<std::string> endpoints;};
struct PartyOffer {std::uint64_t transition{};PartyDestination destination;std::string endpoint;};
enum class PartyPhase {Joining,Joined,Preparing,Ready,Running,Failed,Left};
struct PartyDrop {unsigned seat{};StableId profile;std::string reason;};
struct PartyControlLimits {double join_seconds{30},ready_seconds{30};};
// A control connection is separate from the gameplay Transport, so it outlives every WorldSession epoch.
// Callers own the transports. GNS still carries gameplay through WorldSession; these messages carry no gameplay.
class PartyHost {
public:
    explicit PartyHost(StableId profile,PartyControlLimits={});~PartyHost();
    PartyHost(const PartyHost&)=delete;
    // Before scene entry, or while a scene runs: a guest that joins a running party waits for admit().
    void attach(unsigned seat,Transport&,ConnectionHandle);
    void pump(double seconds);
    bool joined()const;
    std::vector<PartyMember> members()const;
    // One whole-party scene offer. Every joined profile must occur exactly once, at its assigned seat.
    std::uint64_t prepare(PartyDestination);
    void local_ready(AssetId scene,const SessionHandshake& actual);
    bool ready()const;
    bool resume();
    // A failed transition holds control until the caller offers the rebuilt source with a fresh epoch.
    void abort(std::string reason);
    // Live admission into the running scene: the joined guest at this seat is offered the active destination with its
    // avatar and gameplay endpoint, the others get the grown roster, and it is resumed alone once it reports ready.
    std::uint64_t admit(unsigned seat,StableId avatar,std::string endpoint);
    void expel(unsigned seat,std::string reason);
    // The running party stands still while the host prepares travel; released when travel is cancelled. A new offer ends it.
    void hold(bool held);
    PartyPhase phase()const;
    std::vector<PartyDrop> drain_drops();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class PartyGuest {
public:
    PartyGuest(StableId profile,unsigned seat,Transport&,ConnectionHandle);~PartyGuest();
    PartyGuest(const PartyGuest&)=delete;
    void pump();
    PartyPhase phase()const;
    const std::optional<PartyOffer>& offer()const;
    bool held()const;
    // Only call after local assets, collision and the SceneClient's bootstrap are prepared.
    bool ready(AssetId scene,const SessionHandshake& actual);
    void failed(std::string reason);
    void leave();
    const std::string& error()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
