#pragma once
#include <darkangel/scene_definitions.hpp>

namespace darkangel {
// The client half of a scene session for one player: the client WorldSession, the prediction world, the owner's motor
// and ability prediction, observers and poses of every other character (the other players included), effect views, cue
// and footstep trackers, the owner's graph and pose, and the interaction prompt. It learns which characters exist from
// the replicated world and what they are from the same prepared scene content authority was built from; it never reads
// authority's state. In stage 1a its owner steps it in lockstep with authority: begin, then finish once ready.
class SceneClient {
public:
    // party names every member of the session, this client's player among them.
    SceneClient(SessionHandshake,Transport&,ConnectionHandle,StableId player,std::span<const StableId> party,const CollisionDefinition&,
        std::shared_ptr<const AnimationGraphPlan>,RigDefinition,std::string_view rig_archive,std::shared_ptr<const CharacterSceneCombat>,SceneTiming = SceneTiming::Lockstep,StableId profile={});
    ~SceneClient();
    SceneClient(const SceneClient&)=delete;
    // One round of packet work: receive, prepare the newest collision frame for prediction and acknowledge it, send.
    void receive();
    // Bootstrap: true once the first state of the player and of every character it knows has arrived; start() then
    // builds prediction and presentation from it.
    bool bootstrapped()const;
    // Diagnostic receipt for a stalled bootstrap; contains only this client's received state.
    std::string bootstrap_status()const;
    void start();
    // Independent mode: receive, reconcile older corrections, then predict/present fixed ticks. Clock debt is
    // retained when the wire lead bound prevents another step. One-shot inputs survive frames with no step.
    unsigned advance(double seconds,CharacterSceneInput);
    std::uint64_t tick()const;
    unsigned input_lead()const;
    double debt()const;
    // Combat input is routed and kept until the next begin.
    void enqueue(std::span<const InputEvent>);
    // The owner's side of a tick before authority's answer: submit the command and intents, predict.
    void begin(std::uint64_t tick,const CharacterSceneInput&);
    // Authority's state for the tick has arrived in full.
    bool ready(std::uint64_t tick);
    // Reconcile and present the tick, acknowledge it.
    void finish(std::uint64_t tick);
    // One send/receive round after finish, then authority's answers to interaction requests.
    void flush();
    void settle(std::uint64_t tick);
    StableId player()const;
    // The player's network identity; 0 until the replicated world names it.
    std::uint64_t network()const;
    // True when this client holds the character with this network identity (its own or another's).
    bool knows(std::uint64_t network)const;
    std::size_t characters()const;
    const World& presentation()const;
    const MotorState& predicted_motor()const;
    // Latest coherent confirmed state, retained while a world revision bootstraps.
    const MotorState& confirmed_motor()const;
    // Current replicated NPCs described by the locally prepared content. No authority state is consulted.
    std::vector<StableId> actors()const;
    const CharacterSceneActor* actor(StableId)const;
    std::optional<StableId> actor_encounter(StableId)const;
    const GraphState& graph()const;
    const GraphPoseInputs& graph_inputs()const;
    const std::vector<JointMatrix>& pose()const;
    const std::vector<JointMatrix>* pose(StableId actor)const;
    const GraphPoseInputs* graph_inputs(StableId actor)const;
    std::size_t pending_prediction()const;
    unsigned resynchronizations()const;
    // A member admitted to the running scene: its character is described as a party member when it arrives.
    void admit(StableId member);
    const AbilityOwnerSnapshot* ability()const;
    const AbilityOwnerSnapshot* predicted_ability()const;
    const InventoryOwnerState* inventory()const;
    bool inventory_pending()const;
    bool inventory_request(InventoryRequest);
    // The latest view of the scene's world objects and the party (downed members, revive progress, wipe, altars,
    // fast-travel destinations, checkpoint); null until the host has sent one.
    const WorldStateView* world_state()const;
    // The prepared scene content this client presents (altars, pickups, movers among it); null without combat.
    const CharacterSceneCombat* combat()const;
    // Each mover's box centre in the newest collision frame received; empty before one has arrived.
    std::vector<std::pair<StableId,MotorVec>> movers()const;
    // Rest or fast travel from an altar's menu; false when it could not be sent yet.
    bool world_request(const WorldRequest&);
    // This client's player is downed (Health 0 while the party lives on).
    bool downed()const;
    std::size_t pending_abilities()const;
    MotorVec prediction_visual_offset()const;
    ObserverAbilitySample observer(StableId,double render_tick)const;
    std::optional<ObserverAbilitySample> observer_if_ready(StableId,double render_tick)const;
    const EffectPresentation* effects(StableId)const;
    std::span<const EffectCueUpdate> effect_cues()const;
    std::span<const ActionCueUpdate> action_cues()const;
    std::span<const ActionCueCut> action_cue_cuts()const;
    std::span<const FootstepUpdate> footsteps()const;
    void present_cues(CuePlayer*);
    std::optional<StableId> identity(std::uint64_t network)const;
    const ReactionSample* reaction(StableId)const;
    std::optional<double> death(StableId)const;
    unsigned loadout()const;unsigned predicted_loadout()const;
    std::optional<StableId> soft_lock()const;
    AimSolution aim(MotorVec camera,MotorVec direction,double range,MotorVec muzzle)const;
    const std::vector<ProjectileView>& projectiles()const;
    std::optional<InteractionPrompt> interaction_prompt()const;
    const InteractionOutcome* last_interaction()const;
    const CharacterSceneProfile& profile()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
