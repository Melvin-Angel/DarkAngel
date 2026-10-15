#pragma once
#include <darkangel/scene_content.hpp>
#include <darkangel/party_inventory.hpp>
#include <functional>

namespace darkangel::scene_detail {
// The host side of a scene's pickups, altars and movers: their logical state, their ledger records, and the kinematic
// boxes of movers in the authoritative physics world. SceneAuthority owns one and routes interaction requests, world
// requests and the fixed tick through it. Every decision is made from authoritative state; nothing here is replicated
// except through WorldStateView and the collision stream.
class SceneWorldObjects {
  public:
    // Who asks: the member's network identity, persistent profile, authoritative place and whether it is alive.
    struct Who {
        std::uint64_t network{};
        StableId profile;
        MotorVec position{};
        bool living{};
        bool host{};
    };
    // What the objects read from the rest of the scene.
    struct Context {
        const WorldEvents& world;
        std::function<bool(StableId)> encounter_complete;
    };
    SceneWorldObjects(std::shared_ptr<const CharacterSceneCombat>, PhysicsWorld&, WorldLedger*, PartyInventory*);
    // Answers a use of one of its objects; empty when the object is not one of them. A use can raise party travel.
    std::optional<InteractionResult> interact(const Who&, StableId object, const Context&, std::optional<SceneTransitionRequest>& travel);
    // A rest or fast travel request from a member's altar menu. Refused requests change nothing.
    InteractionResult request(const Who&, const WorldRequest&, std::optional<SceneTransitionRequest>& travel);
    // One fixed tick: world events delivered this tick start movers; movers move.
    void step(double seconds, const WorldEvents&);
    // Every object's state into the ledger.
    void capture();
    // Logical state for every client.
    void view(WorldStateView&) const;
    std::uint64_t revision() const {
        return revision_;
    }
    // Party respawn after a wipe: the checkpoint, or the party's own start (nil scene) when there is none.
    SceneTransitionRequest respawn() const;
    // Movers' current places, for debug views.
    std::vector<std::pair<StableId, MotorVec>> movers() const;

  private:
    struct Mover {
        const CharacterSceneMover* config{};
        MoverState state;
        std::uint64_t box{};
        MotorVec center{};
    };
    std::shared_ptr<const CharacterSceneCombat> combat_;
    PhysicsWorld& physics_;
    WorldLedger* ledger_{};
    PartyInventory* inventory_{};
    std::vector<bool> collected_;
    std::vector<Mover> movers_;
    std::uint64_t revision_{1};
    PersistentId persistent(StableId id) const {
        return {combat_->scene, id};
    }
    void record_pickup(std::size_t);
    void record_mover(std::size_t);
    bool met(const Who&, const Requirement&, const Context&) const;
    static bool reaches(MotorVec a, MotorVec b, double radius) {
        const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
};
} // namespace darkangel::scene_detail
