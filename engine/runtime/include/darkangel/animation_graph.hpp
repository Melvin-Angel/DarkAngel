#pragma once
#include <darkangel/animation.hpp>
#include <darkangel/tags.hpp>
#include <cstdint>
#include <cmath>
#include <map>
#include <stdexcept>
#include <variant>

namespace darkangel {
enum class GraphNodeKind { Clip, Blend1D, Blend2D, TagSelect };
enum class GraphParameter { Speed, Forward, Lateral };
enum class GraphParameterType { Float, Integer, Boolean };
enum class GraphCompareOperator { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
enum class GraphConditionKind { Always, HasTag, Compare, Not, All, Any, None, Xor };
using GraphParameterValue = std::variant<float, std::int32_t, bool>;
struct GraphParameterDefinition { std::string name; GraphParameterType type{GraphParameterType::Float}; GraphParameterValue default_value{0.f}; };
struct GraphParameterStore {
    std::map<std::string, GraphParameterValue> values;
    void set_float(std::string_view name,float value){if(!std::isfinite(value)||std::abs(value)>1000000)throw std::runtime_error("Animation float parameter bounds");values[std::string(name)]=value;}
    void set_int(std::string_view name,std::int32_t value){values[std::string(name)]=value;}
    void set_bool(std::string_view name,bool value){values[std::string(name)]=value;}
    float get_float(std::string_view name,float fallback=0)const{auto it=values.find(std::string(name));return it==values.end()?fallback:std::holds_alternative<float>(it->second)?std::get<float>(it->second):fallback;}
    std::int32_t get_int(std::string_view name,std::int32_t fallback=0)const{auto it=values.find(std::string(name));return it==values.end()?fallback:std::holds_alternative<std::int32_t>(it->second)?std::get<std::int32_t>(it->second):fallback;}
    bool get_bool(std::string_view name,bool fallback=false)const{auto it=values.find(std::string(name));return it==values.end()?fallback:std::holds_alternative<bool>(it->second)?std::get<bool>(it->second):fallback;}
};
struct GraphCondition {
    GraphConditionKind kind{GraphConditionKind::Always};
    TagId tag{};
    std::string parameter;
    GraphParameterType parameter_type{GraphParameterType::Float};
    GraphCompareOperator comparison{GraphCompareOperator::Equal};
    GraphParameterValue value{0.f};
    std::vector<GraphCondition> children;
};
struct GraphStateDefinition { std::uint32_t id{}; std::string name; std::uint32_t root{}; };
struct GraphTransitionDefinition {
    std::uint32_t id{}; std::uint32_t from{}; std::uint32_t to{}; unsigned priority{}; unsigned crossfade_ticks{};
    bool has_exit_time{}; float exit_time{}; GraphCondition condition;
};
struct GraphMachineDefinition {
    std::vector<GraphParameterDefinition> parameters;
    std::vector<GraphStateDefinition> states;
    std::vector<GraphTransitionDefinition> transitions;
    std::uint32_t entry{};
};
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
struct GraphParameters { float speed{}, forward{}, lateral{},playback_rate{1};const ActorTagSnapshot* tags{};AttributeVisibility tag_audience{AttributeVisibility::Owner};const GraphParameterStore* values{}; };
struct GraphState { std::uint64_t tick{}; double phase{}; std::string generation; std::uint32_t active_state{}; std::uint32_t previous_state{}; unsigned transition_ticks{}; unsigned transition_total{}; };
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
    AnimationGraphPlan(std::string generation, GraphMachineDefinition, std::vector<GraphNode>,std::shared_ptr<const TagDictionary> = {});
    ~AnimationGraphPlan();
    const TagDictionary* tag_dictionary()const;
    unsigned active_layer_bound()const;
    AnimationGraphPlan(const AnimationGraphPlan&)=delete;
    const std::vector<GraphParameterDefinition>& parameters()const;
    const GraphMachineDefinition* machine()const;
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
    GraphPoseInputs evaluate() const;
    GraphPoseInputs advance(std::uint64_t tick);
    void set_float(std::string_view name,float value);
    void set_int(std::string_view name,std::int32_t value);
    void set_bool(std::string_view name,bool value);
    const GraphParameterStore& parameter_store()const{return parameters_;}
    const std::vector<GraphParameterDefinition>& parameter_definitions()const{return plan_->parameters();}
    const GraphState& state() const { return state_; }
    unsigned active_layer_bound()const{return plan_->active_layer_bound();}
    const TagDictionary* tag_dictionary()const{return plan_->tag_dictionary();}
    void restore(const GraphState&);
private:
    std::shared_ptr<const AnimationGraphPlan> plan_;
    GraphState state_;
    GraphParameterStore parameters_;
};
}
