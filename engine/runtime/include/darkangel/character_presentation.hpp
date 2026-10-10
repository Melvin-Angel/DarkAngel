#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/ability_observer.hpp>

namespace darkangel {
// Prepared complementary local-joint weights. One action plus at most three
// locomotion layers; pose sampling never owns motor root movement.
class ActionPoseMixer {
public:
    ActionPoseMixer(const AnimationGraphPlan&,const RigDefinition&,std::span<const std::shared_ptr<const ActionDefinition>>);
    const std::vector<JointMatrix>& sample(RigPose&,const GraphPoseInputs&,const ActionDefinition&,const AnimationClip&,double tick)const;
private:
    struct Weights {std::string generation;std::vector<float> action,locomotion;};
    std::map<AssetId,Weights> weights_;
};
enum class ReactionSuppression {None,ActiveAction,Dead};
// Lifecycle identity of one checked effect instance on one avatar. Refreshing an
// instance keeps its handle and onset; only a new instance is a new reaction.
struct ReactionKey {std::uint64_t session_epoch{},network{},avatar_epoch{},handle{};auto operator<=>(const ReactionKey&)const=default;};
struct ReactionSample {ReactionKey key;AssetId effect;const ActionDefinition* action{};const AnimationClip* clip{};std::uint64_t start{};double tick{};ReactionSuppression suppressed{};};
// Prepared read-only selection of at most one visible full-body reaction from the
// current checked effect instances. Stateless: the clip clock is the pose tick minus
// the replicated onset, so duplicates, refreshes and late joins never replay a
// finished one-shot. It executes no blocks, requests no root and cancels nothing.
// Order: highest action priority, then latest onset, then highest effect handle.
class ReactionPresentation {
public:
    ReactionPresentation(const RigDefinition&,std::span<const std::shared_ptr<const EffectDefinition>>,const std::map<AssetId,std::shared_ptr<const AnimationClip>>&);
    bool empty()const{return reactions_.empty();}
    std::optional<ReactionSample> select(const EffectFrame*,std::uint64_t network,std::uint64_t session_epoch,std::uint64_t avatar_epoch,std::uint64_t tick,bool alive,bool acting)const;
private:
    struct Prepared {std::string generation;std::shared_ptr<const ActionDefinition> action;std::shared_ptr<const AnimationClip> clip;};
    std::map<AssetId,Prepared> reactions_;
};
// Prepared public presentation only. Samples existing graph/action clocks and
// never enters a timeline, emits cues, requests root movement or executes gameplay.
class ObservedCharacterPose {
public:
    ObservedCharacterPose(std::shared_ptr<const AnimationGraphPlan>,RigDefinition,std::string_view rig_archive,
        std::span<const std::shared_ptr<const ActionDefinition>>,
        std::map<AssetId,std::shared_ptr<const AnimationClip>>,
        std::shared_ptr<const TagDictionary> = {},std::span<const std::shared_ptr<const EffectDefinition>> effects={});
    // The optional checked public effect frame supplies reaction instance clocks.
    void sample(const AbilityPublicFrame&,const EffectFrame* effects=nullptr);
    const std::optional<ReactionSample>& reaction()const{return reaction_;}
    const std::vector<JointMatrix>& matrices()const{return matrices_;}
    const GraphPoseInputs& inputs()const{return inputs_;}
    const GraphState& graph_state()const{return graph_.state();}
private:
    AnimationGraphInstance graph_;RigPose rig_;std::unique_ptr<ActionPoseMixer> mixer_;
    std::map<AssetId,std::shared_ptr<const ActionDefinition>> actions_;
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips_;
    std::shared_ptr<const TagDictionary> tags_;
    std::vector<JointMatrix> matrices_;GraphPoseInputs inputs_;
    std::unique_ptr<ReactionPresentation> reactions_;std::optional<ReactionSample> reaction_;
    std::uint64_t network_{},session_epoch_{},motor_epoch_{};
};
}
