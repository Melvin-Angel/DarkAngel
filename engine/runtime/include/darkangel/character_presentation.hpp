#pragma once
#include <darkangel/animation_graph.hpp>
#include <darkangel/ability_observer.hpp>

namespace darkangel {
// Prepared public presentation only. Samples existing graph/action clocks and
// never enters a timeline, emits cues, requests root movement or executes gameplay.
class ObservedCharacterPose {
public:
    ObservedCharacterPose(std::shared_ptr<const AnimationGraphPlan>,RigDefinition,std::string_view rig_archive,
        std::span<const std::shared_ptr<const ActionDefinition>>,
        std::map<AssetId,std::shared_ptr<const AnimationClip>>,
        std::shared_ptr<const TagDictionary> = {});
    void sample(const AbilityPublicFrame&);
    const std::vector<JointMatrix>& matrices()const{return matrices_;}
    const GraphPoseInputs& inputs()const{return inputs_;}
    const GraphState& graph_state()const{return graph_.state();}
private:
    AnimationGraphInstance graph_;RigPose rig_;
    std::map<AssetId,std::shared_ptr<const ActionDefinition>> actions_;
    std::map<AssetId,std::shared_ptr<const AnimationClip>> clips_;
    std::shared_ptr<const TagDictionary> tags_;
    std::vector<JointMatrix> matrices_;GraphPoseInputs inputs_;
    std::uint64_t network_{},session_epoch_{},motor_epoch_{};
};
}
