#pragma once
#include <darkangel/scene_content.hpp>
#include <darkangel/scene_seat.hpp>
#include <darkangel/animation_graph.hpp>
#include <darkangel/ability_observer.hpp>
#include <darkangel/effect_replication.hpp>
#include <darkangel/cue_player.hpp>
#include <darkangel/inventory.hpp>

namespace darkangel {
class SceneAuthority;
class SceneClient;
struct CollisionDefinition;
// Local listen-host composition: one SceneAuthority and one SceneClient per local player, each client on its own
// in-process loopback pair, all stepped in lockstep. The accessors that name no player are the first player's, as
// before the host and client halves were split. Resources are already validated/cooked. This adapter owns no
// transport codec. Optional combat uses the same serialized WorldSession path and frozen assets.
class CharacterSceneSession {
  public:
    // companions: further local players, each with its own client. They enter with the session; at most
    // scene_party_limit players in all.
    CharacterSceneSession(SessionHandshake, std::span<const ObjectData>, StableId player, const CollisionDefinition&,
                          std::shared_ptr<const AnimationGraphPlan>, RigDefinition, std::string_view rig_archive,
                          std::optional<CharacterSceneCombat> = {}, std::span<const ScenePlayer> companions = {}, const SceneStart& = {},
                          std::span<const SceneSeat> remote = {}, SceneTiming = SceneTiming::Lockstep);
    // Independent client-only composition. The transport and prepared resources outlive this session. party lists every
    // avatar that may be a member of this scene; players() are those of them that are in the replicated world.
    CharacterSceneSession(SessionHandshake, Transport&, ConnectionHandle, StableId player, std::span<const StableId> party,
                          const CollisionDefinition&, std::shared_ptr<const AnimationGraphPlan>, RigDefinition,
                          std::string_view rig_archive, std::optional<CharacterSceneCombat> = {}, StableId profile = {});
    ~CharacterSceneSession();
    bool hosting() const;
    bool bootstrapped() const;
    void pump();
    // Host-only removal of a disconnected remote avatar; profile-owned state remains outside this scene.
    bool remove_member(StableId);
    // Host-only live admission of a remote seat; see SceneAuthority::add_member. The transport outlives this session.
    void add_member(const SceneSeat&);
    std::vector<std::pair<StableId, std::string>> take_refused();
    CharacterSceneSession(const CharacterSceneSession&) = delete;
    unsigned advance(double seconds, CharacterSceneInput);
    void step(CharacterSceneInput);
    // A companion's input for the ticks that follow: movement and aim hold until replaced; jump, interact and combat
    // events are used once.
    void input(StableId companion, CharacterSceneInput);
    // The two halves. players() lists the party in seat order; client(player) is null for an unknown player.
    std::span<const StableId> players() const;
    const SceneAuthority& authority() const;
    // Party travel a scene transition trigger raised on the latest tick, handed over once; and every persistent
    // object of the scene into the world ledger, as an unload does. Both are authority's.
    std::optional<SceneTransitionRequest> take_transition();
    void capture();
    const SceneClient* client(StableId player) const;
    SceneClient* client(StableId player);
    void remove_collision(std::uint64_t);
    const World& presentation() const;
    const MotorState& motor() const;
    const MotorState& predicted_motor() const;
    const GraphState& graph() const;
    const GraphPoseInputs& graph_inputs() const;
    const std::vector<JointMatrix>& pose() const;
    const std::vector<JointMatrix>* pose(StableId actor) const;
    const GraphPoseInputs* graph_inputs(StableId actor) const;
    std::size_t pending_prediction() const;
    unsigned resynchronizations() const;
    double debt() const;
    const AbilityOwnerSnapshot* ability() const;
    const AbilityOwnerSnapshot* predicted_ability() const;
    std::size_t pending_abilities() const;
    MotorVec prediction_visual_offset() const;
    ObserverAbilitySample observer(StableId, double render_tick) const;
    std::optional<ObserverAbilitySample> observer_if_ready(StableId, double render_tick) const;
    std::span<const AbilityCommitUpdate> commitments() const;
    const EffectPresentation* effects(StableId) const;
    std::span<const EffectCueUpdate> effect_cues() const;
    // Cue blocks the actions of the player and the actors crossed on the latest tick, and actions that ended before
    // their time. Derived on the client side from the action clocks it already holds (the owner's prediction and
    // correction, the other actors' public frames); no server timeline is read.
    std::span<const ActionCueUpdate> action_cues() const;
    std::span<const ActionCueCut> action_cue_cuts() const;
    // Steps the player and the actors took on the latest tick: the player from its predicted motor, the others from their public frames.
    std::span<const FootstepUpdate> footsteps() const;
    // A cue player to drive from every tick of this session: effect cue edges, action cues, cuts, actors leaving
    // and the tick itself. It must outlive the session or be detached with a null pointer first.
    void present_cues(CuePlayer*);
    // The scene identity behind a network identity the cue records name: the player or an actor.
    std::optional<StableId> identity(std::uint64_t network) const;
    // Current selected reaction for a prepared actor, including a suppressed candidate.
    const ReactionSample* reaction(StableId) const;
    // Death timeline tick while a prepared actor's checked Health is 0.
    std::optional<double> death(StableId) const;
    // Confirmed and predicted active loadout (equipped mask slot) of the player.
    unsigned loadout() const;
    unsigned predicted_loadout() const;
    // Soft-lock target the owner's predicted motor warped toward on the latest tick, if any.
    // Presentation only (HUD indicator); authority resolves its own warp.
    std::optional<StableId> soft_lock() const;
    // Owner-side aim intent from the camera. The ray is cast against the owner's predicted collision and the confirmed
    // public positions of the other actors, starting level with the owner so nothing between camera and owner counts.
    // muzzle is the shot's origin relative to the owner (right, up, forward). The result only shapes the owner's
    // input; authority sweeps its own world from its own state.
    AimSolution aim(MotorVec camera, MotorVec direction, double range, MotorVec muzzle) const;
    // Where the session's fixed ticks have spent their time since it was built. A measurement aid; nothing reads it for gameplay.
    const CharacterSceneProfile& profile() const;
    // Packets the server has sent the first player's client so far, for budget measurements.
    std::uint64_t server_packets() const;
    // The same count split by wire kind number.
    std::vector<std::uint64_t> server_packet_kinds() const;
    // Projectiles in flight as the client last received them. Presentation only.
    const std::vector<ProjectileView>& projectiles() const;
    // Server-driven actors in simulation order (ascending network identity).
    std::span<const StableId> actors() const;
    // Authoritative receipt of an actor's latest controller press, if it has made one.
    const AbilityReceipt* controller_receipt(StableId) const;
    // An actor's controller's description of its latest decision; empty without a controller.
    std::string controller_status(StableId) const;
    // The actor an actor's controller named on its latest decision, if any.
    std::optional<StableId> controller_target(StableId) const;
    // An actor's senses and what it perceived on the latest tick, as authority holds them. For
    // debug views in the host process only; nothing here is replicated.
    const Perception* perception(StableId) const;
    // World events and tags, as authority holds them. Emitting queues the event for the next tick, like any other.
    void emit_world_event(std::string name);
    // Fault injection for tests and the editor's gate: authority refuses the player's next pressed ability (see
    // WorldSession::refuse_next_ability). The client predicts the press as usual and is then corrected.
    void refuse_next_activation();
    const WorldEvents& world_events() const;
    // Encounters in scene order, the state of one, and for an actor that an encounter spawned, which one.
    std::span<const StableId> encounters() const;
    std::optional<EncounterStatus> encounter(StableId) const;
    std::optional<StableId> actor_encounter(StableId actor) const;
    // Trigger volumes: how often one has fired, if it exists.
    std::optional<unsigned> trigger_fired(StableId) const;
    // Interaction. The prompt is the nearest interactable in reach of the owner's predicted position that this client has
    // not seen used up; the outcome is authority's answer to the owner's latest request. interactable_used is the
    // server's count of successful uses, for debug views.
    std::optional<InteractionPrompt> interaction_prompt() const;
    const InteractionOutcome* last_interaction() const;
    std::optional<unsigned> interactable_used(StableId) const;
    // Host only: see SceneAuthority::grant_item. Empty on a guest or without inventories.
    std::optional<InventoryReceipt> grant_item(StableId member, AssetId item, unsigned quantity);
    // Host only: SceneAuthority::down_member.
    bool down_member(StableId member);
    // Name and faction of any server-driven actor, spawned enemies included.
    const CharacterSceneActor* actor(StableId) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace darkangel
