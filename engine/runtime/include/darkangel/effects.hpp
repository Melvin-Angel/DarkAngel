#pragma once
#include <darkangel/ability.hpp>
#include <darkangel/tags.hpp>
#include <functional>
#include <stdexcept>
namespace darkangel {
enum class EffectLifetime {Instant,Finite,UntilRemoved};
enum class EffectStack {Independent,RefreshPerSource};
enum class EffectOngoingPolicy {Suppress,Remove};
struct EffectCueBinding {AssetId id;std::string key;};
struct EffectDefinition {
 AssetId id;std::string generation;EffectLifetime lifetime{EffectLifetime::Finite};EffectStack stacking{};
 std::uint64_t duration_ticks{},period_ticks{};bool execute_on_apply{},remove_on_death{true},remove_with_source{},interrupt_action{};
 std::uint32_t evaluator{};std::vector<TagId> tags;TagRequirement application,ongoing;
 EffectOngoingPolicy ongoing_policy{};
 std::vector<AttributeModifier> modifiers;
 AttributeVisibility visibility{AttributeVisibility::Owner};
 // Persistent presentation identities, resolved by the game's cue adapter.
 std::vector<EffectCueBinding> cues;
 // Optional presentation-only reaction: one frozen full-body Composer action,
 // sampled from the checked effect-instance clock. Its blocks never execute and
 // finishing it never ends the effect; duration and legality stay with the effect.
 std::shared_ptr<const ActionDefinition> reaction;
};
std::shared_ptr<const EffectDefinition> freeze_effect_definition(const EffectDefinition&,const AttributeSet&,const TagDictionary&);
// Captured attribution outlives the source avatar. It contains no live pointer.
struct EffectCredit {std::uint64_t session_epoch{},source_network{},activation{};double power{};std::vector<AbilityAttributeValue> source_attributes;};
struct EffectHandle {std::uint64_t value{};AbilityOwnerHandle owner;auto operator<=>(const EffectHandle&)const=default;};
struct EffectSnapshot {EffectHandle handle;AssetId definition;std::string generation;EffectCredit credit;std::uint64_t start{},end{},next_period{};bool suppressed{};};
struct EffectContext {const EffectCredit& credit;std::uint64_t tick{};std::span<const AbilityAttributeValue> target_attributes;const OwnedTags& target_tags;};
using EffectEvaluator=std::function<std::vector<ResourceDelta>(const EffectContext&)>;
struct EffectExecution {EffectHandle handle;EffectCredit credit;std::uint64_t tick{};std::vector<ResourceDelta> applied;};
struct EffectOutcome {AbilityOwnerHandle target;EffectExecution execution;};
// Value state owned by AbilityState. The caller prepares a copy and publishes
// attributes, effects, tags, death cleanup and execution results together.
class OwnedEffects {
public:
 explicit OwnedEffects(std::shared_ptr<const TagDictionary>,std::uint64_t tick=0);
 std::pair<EffectHandle,std::vector<EffectExecution>> apply(const EffectDefinition&,EffectCredit,std::uint64_t tick,AttributeSet&,const EffectEvaluator&);
 std::vector<EffectExecution> advance(std::uint64_t tick,AttributeSet&,const std::function<EffectEvaluator(std::uint32_t)>&,AttributeId health=0);
 void remove(EffectHandle,AttributeSet&);
 void cleanse(const TagRequirement&,AttributeSet&);
 void death(AttributeSet&);
 void source_destroyed(std::uint64_t session,std::uint64_t network,AttributeSet&);
 std::vector<EffectSnapshot> snapshot()const;
 std::vector<EffectSnapshot> snapshot(AttributeVisibility)const;
 const OwnedTags& tags()const{return tags_;}
 // A reserved contributor, disjoint from effect IDs and restored aggregates.
 void set_action_tags(std::span<const TagId>,AttributeSet&);
 std::vector<TagId> external_tags(AttributeVisibility audience)const;
 void restore_owner_tags(const ActorTagSnapshot& source){if(!active_.empty())throw std::invalid_argument("Owner tag restore cannot replace live effects");tags_.restore_snapshot(source,AttributeVisibility::Owner);action_tags_.clear();}
private:
 struct Active {EffectSnapshot state;std::shared_ptr<const EffectDefinition> definition;std::vector<AttributeModifier> modifiers;};
 OwnedTags tags_;std::vector<Active> active_;std::uint64_t next_{1},modifier_sequence_{1},tick_{};
 std::vector<std::uint64_t> modifier_owners_;
 std::vector<TagId> action_tags_;
 void contributions(AttributeSet&);
 EffectExecution execute(const Active&,std::uint64_t,AttributeSet&,const EffectEvaluator&)const;
 void erase(EffectHandle,AttributeSet&);
};
}
