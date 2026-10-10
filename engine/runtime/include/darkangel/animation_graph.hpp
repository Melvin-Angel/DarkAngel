#pragma once
#include <darkangel/animation.hpp>
#include <darkangel/tags.hpp>
#include <cstdint>

namespace darkangel {
enum class GraphNodeKind { Clip, Blend1D, Blend2D, TagSelect };
enum class GraphParameter { Speed, Forward, Lateral };
struct GraphPoint { std::uint32_t input{}; float x{}, y{}; };
struct GraphNode {
    std::uint32_t id{};
    GraphNodeKind kind{};
    GraphParameter parameter{GraphParameter::Speed};
    std::shared_ptr<const AnimationClip> clip;
    std::vector<GraphPoint> points;
    // Explicit triangles reference point indices; no runtime triangulation.
    std::vector<std::array<unsigned,3>> triangles;
    TagRequirement requirements;
};
struct GraphParameters { float speed{}, forward{}, lateral{},playback_rate{1};const ActorTagSnapshot* tags{};AttributeVisibility tag_audience{AttributeVisibility::Owner}; };
struct GraphState { std::uint64_t tick{}; double phase{}; std::string generation; };
struct GraphPoseInputs {
    // Descriptors borrow the frozen clips; keep the instance/plan alive while
    // consuming them. They never transfer root-motion ownership to the motor.
    std::array<PoseLayer,4> layers{};
    unsigned count{};
    std::span<const PoseLayer> span() const { return {layers.data(), count}; }
};
// Initial native compiled profile: clip, 1D and explicit triangle-based 2D
// selectors, one normalized locomotion cycle, up to32 catalogue clips and four simultaneous layers. No runtime reflection or mapping.
// Asset authoring, markers, transitions and action/additive slots remain separate.
class AnimationGraphPlan {
public:
    AnimationGraphPlan(std::string generation, std::uint32_t root, std::vector<GraphNode>,std::shared_ptr<const TagDictionary> = {});
    ~AnimationGraphPlan();
    const TagDictionary* tag_dictionary()const;
    AnimationGraphPlan(const AnimationGraphPlan&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class AnimationGraphInstance;
};
class AnimationGraphInstance {
public:
    explicit AnimationGraphInstance(std::shared_ptr<const AnimationGraphPlan>);
    GraphPoseInputs evaluate(GraphParameters) const;
    GraphPoseInputs advance(std::uint64_t tick, GraphParameters);
    const GraphState& state() const { return state_; }
    const TagDictionary* tag_dictionary()const{return plan_->tag_dictionary();}
    void restore(const GraphState&);
private:
    std::shared_ptr<const AnimationGraphPlan> plan_;
    GraphState state_;
};
}
