#pragma once
#include <darkangel/scene_content.hpp>
#include <darkangel/collision_asset.hpp>
#include <darkangel/animation_graph.hpp>
#include <darkangel/transport.hpp>
#include <darkangel/inventory.hpp>
#include <functional>
#include <memory>

namespace darkangel {
class CharacterSceneSession;
// A place the party can arrive at in a prepared scene: a Scene Entry Point object's identity, label, position and facing.
struct SceneEntry {
    StableId id;
    std::string name;
    MotorVec position{};
    double yaw{};
};
// Everything one scene's session is built from, already loaded and validated: what SceneAuthority and SceneClient
// take, plus the scene's entry points. host is whatever the owner of Play keeps alive with the scene (models,
// navigation, audio); the coordinator only carries it.
struct PreparedScene {
    AssetId scene;
    SessionHandshake hello;
    std::vector<ObjectData> objects;
    StableId player;
    CollisionDefinition collision;
    std::shared_ptr<const AnimationGraphPlan> plan;
    RigDefinition rig;
    std::string rig_archive;
    std::optional<CharacterSceneCombat> combat;
    std::vector<SceneEntry> entries;
    std::shared_ptr<void> host;
};
// Turns a scene asset into a PreparedScene. Implemented by the editor, later by the game. Loading may take several
// frames: begin starts it, poll reports progress and a status text until it is ready or has failed, take hands the
// result over, cancel abandons it. One load at a time.
enum class SceneLoad : std::uint8_t { Working, Ready, Failed };
struct SceneLoadStatus {
    SceneLoad state{};
    double progress{};
    std::string text;
};
class SceneProvider {
  public:
    virtual ~SceneProvider() = default;
    virtual void begin(AssetId scene) = 0;
    virtual SceneLoadStatus poll() = 0;
    virtual std::unique_ptr<PreparedScene> take() = 0;
    virtual void cancel() = 0;
};
// The phases of one transition, in order. From Requested to Resumed the party's input is held and the loading screen shows.
//   Requested      accepted; nothing has happened yet
//   Preparing      the provider loads the destination; the source scene keeps running
//   Validating     the destination is the scene asked for, the entry point exists in it, the party fits its capacity
//   Capturing      the world ledger takes the source scene's persistent state
//   Swapping       the source session is destroyed and the destination session is built with the ledger and the party
//   Placing        the party stands at the entry point
//   AwaitingReady  every client holds the destination's first state
//   Resumed        control returns
enum class TransitionPhase : std::uint8_t { Idle, Requested, Preparing, Validating, Capturing, Swapping, Placing, AwaitingReady, Resumed };
const char* transition_phase_text(TransitionPhase);
// entry: the destination entry point, or nil for the scene's own start. reason is for logs.
// reset and save: see SceneTransitionRequest. A nil scene is refused; a session's respawn with no checkpoint is mapped
// to origin() before it is requested.
struct TransitionRequest {
    AssetId scene;
    StableId entry;
    std::string reason;
    std::uint8_t reset{};
    bool save{};
};
struct TransitionStatus {
    TransitionPhase phase{};
    double progress{};
    std::string text;
};
// How the latest transition ended. arrived: the party is in the destination. Otherwise failure says why, and the
// party is in the scene it left: untouched when the failure came before the swap, rebuilt from the ledger when it
// came during it (rebuilt true).
struct TransitionOutcome {
    bool arrived{};
    AssetId scene;
    StableId entry;
    std::string failure;
    bool rebuilt{};
    std::uint8_t reset{};
    bool save{};
};
// Owns the running scene session of a Play and replaces it when the party travels. One transition at a time; party
// travel is whole-party by definition. What crosses a transition is the party and the world ledger, nothing else.
// The session is never left with no scene: a failure before the swap cancels, a failure during it builds the source
// scene again. If that fails too, Play cannot continue and fatal() says so.
// Additive loading later is another kind of request through this same coordinator; nothing here assumes one scene is
// the whole world, except that one session is one scene.
class SceneTransitionCoordinator {
  public:
    // companions: the party members beyond the first, in seat order. The provider and the ledger outlive the coordinator.
    SceneTransitionCoordinator(SceneProvider&, WorldLedger&, std::vector<StableId> companions = {});
    ~SceneTransitionCoordinator();
    StableId inventory_profile; // optional persistent local profile; party hosts supply their roster mapping
    SceneTransitionCoordinator(const SceneTransitionCoordinator&) = delete;
    // Starts Play in a scene: the party at the entry point, or at the scene's own start.
    void start(std::unique_ptr<PreparedScene>, StableId entry = {});
    // Ends Play: the scene's state goes into the ledger and the session is destroyed.
    void stop();
    CharacterSceneSession* session() const;
    const PreparedScene* scene() const;
    // The scene and entry Play began with: where a party with no checkpoint respawns.
    std::pair<AssetId, StableId> origin() const;
    // The party's persistent inventories when the coordinator owns them (in-process Play); null otherwise.
    PartyInventory* inventory() const;
    // Inventories restored from a save, used instead of fresh starting inventories. Before start().
    void restore_inventory(std::vector<InventorySnapshot>);
    // Asks for party travel. False, and nothing changes, while another transition is running or no scene is.
    bool request(TransitionRequest);
    // Once per frame, after the session's ticks: takes travel the session's own triggers raised, then advances the
    // running transition by one phase step.
    void update(double seconds);
    // True from Requested to Resumed: the owner of Play holds the party's input and shows the loading screen.
    bool busy() const;
    TransitionStatus status() const;
    // The phases the latest transition entered, in order, and how it ended (empty while it runs or before the first).
    std::span<const TransitionPhase> phases() const;
    const std::optional<TransitionOutcome>& outcome() const;
    bool fatal() const;
    // The owner of Play drops what hangs off the session before it is destroyed, and builds it again once a session
    // exists (the destination, or the source after a failed swap). An exception from loaded fails the swap.
    std::function<void()> unloading, loaded;
    // Optional network construction: null means still working; an exception rebuilds the source.
    // Resources and external transports are owned by the caller and must outlive the returned session.
    std::function<std::unique_ptr<CharacterSceneSession>(PreparedScene&, StableId, const SceneStart&, std::span<const ScenePlayer>)>
        build_session;
    // The external party's acknowledgement barrier replaces the default in-process check.
    std::function<bool(CharacterSceneSession&)> ready;
    std::function<void()> resumed;
    std::function<void(std::string_view)> cancelled;
    std::function<std::string()> construction_text;
    std::function<std::size_t()> party_size;
    // The loading screen stays at least this long, so a fast load does not flash.
    double minimum_seconds{.3};
    // Fault injection for tests and gates: the next swap fails after the source session is gone.
    void fail_next_swap();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace darkangel
