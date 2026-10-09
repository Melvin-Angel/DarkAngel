#pragma once
#include <darkangel/action.hpp>
#include <darkangel/attributes.hpp>
#include <darkangel/tags.hpp>
#include <darkangel/combat_kit.hpp>
#include <darkangel/world.hpp>
#include <optional>

namespace darkangel {
// Native immediate-commit subset. Definitions are frozen at grant installation.
// Deferred reservations and targeting are not implied by this API.
struct AbilityCost {AttributeId attribute{};double amount{};};
struct AbilityActionTag {unsigned block{};TagId tag{};};
struct AbilityMelee {
    unsigned block{};std::array<double,3> offset{};double radius{},power{};
    std::uint32_t evaluator{},damage_type{};
};
struct AbilityDefinition {
    AssetId id;std::string generation;
    std::shared_ptr<const ActionDefinition> action;
    std::vector<AbilityCost> costs;
    // Initial explicit motor-relative sphere profile; animated socket sweeps follow.
    std::vector<AbilityMelee> melee;
    std::uint32_t cooldown_group{};std::uint64_t cooldown_ticks{};
    InputEdge activate_on{InputEdge::Pressed};std::uint64_t minimum_held_us{};
    bool cancel_on_release{},interruptible{true};
    AssetId tag_registry;std::string tag_generation;TagRequirement requirements;
    // Typed interval bindings. Their contribution belongs to this execution.
    std::vector<AbilityActionTag> action_tags;
};
// Shared validation/freezing for cooked definitions and runtime grant installation.
std::shared_ptr<const AbilityDefinition> freeze_ability_definition(const AbilityDefinition&,const AttributeSet&,const TagDictionary* =nullptr);
struct AbilityOwnerHandle {
    EntityHandle entity;std::uint64_t session_epoch{},network{};
    auto operator<=>(const AbilityOwnerHandle&)const=default;
};
struct AbilityActivationHandle {
    AbilityOwnerHandle owner;std::uint64_t activation{};
    auto operator<=>(const AbilityActivationHandle&)const=default;
};
enum class AbilityFailure {
    None,InvalidRequest,StaleGrant,Unassigned,InputIgnored,Dead,Busy,
    Cooldown,Resources,OperationConflict,StaleOperation,HistoryFull,CancelDenied,InputExpired,AvatarMismatch,TagRequirements
};
struct AbilityIntent {
    std::uint64_t network{},operation{},tick{},avatar_epoch{},grant_generation{};
    CombatSlot slot{};InputEdge edge{};bool cancelled{},replace_active{};
    auto operator<=>(const AbilityIntent&)const=default;
};
struct AbilityRequest {
    AbilityOwnerHandle owner;std::uint64_t operation{},grant_generation{};
    CombatSlot slot{};InputEvent input;bool replace_active{};
};
struct AbilityReceipt {
    AbilityFailure failure{AbilityFailure::None};AbilityActivationHandle handle;
    bool committed{},duplicate{};
    std::uint64_t operation{},tick{},inclusion_revision{};
};
struct AbilityOperationNotice {std::uint64_t network{};AbilityReceipt receipt;bool terminal{true};};
enum class AbilityActionReason {Started,Advanced,Completed,Cancelled,Replaced,GrantRemoved,Despawned,Death,Disconnected,InputLost};
struct AbilityActionUpdate {
    AbilityActivationHandle handle;ActionPhase phase{};
    AbilityActionReason reason{AbilityActionReason::Completed};std::uint64_t tick{};ActionBatch batch;
};
struct AbilityAttributeValue {AttributeId id{};double value{};};
struct AbilitySnapshotOperation {std::uint64_t operation{};AbilityFailure failure{};std::uint64_t activation{};bool committed{};};
struct AbilityInputSnapshot {bool active{},hold_sent{},rearm{};std::uint64_t pressed{},released{},duration{};};
// Public actor state has no grants, input, operation history, costs or cooldowns.
struct AbilityPublicSnapshot {
    AbilityOwnerHandle owner;std::uint64_t tick{},revision{};
    AttributeId health_attribute{},maximum_health_attribute{};
    std::vector<AbilityAttributeValue> attributes;
    std::optional<AbilityActivationHandle> active;std::optional<ActionState> action;
    AssetId action_definition;
    ActorTagSnapshot tags;
};
struct AbilityOwnerSnapshot {
    AbilityOwnerHandle owner;std::uint64_t tick{},grant_generation{},revision{};
    std::vector<AbilityAttributeValue> attributes;
    std::optional<AbilityActivationHandle> active;
    std::optional<ActionState> action;
    std::vector<std::pair<std::uint32_t,std::uint64_t>> cooldowns;
    std::size_t retained_operations{};
    std::uint64_t highest_operation{},retired_through{};
    AttributeId health_attribute{},maximum_health_attribute{};
    std::vector<AbilitySnapshotOperation> operations;
    AssetId ability,action_definition;std::string ability_generation;CombatSlot active_slot{};
    std::uint64_t next_activation{1};
    std::array<AbilityInputSnapshot,combat_slot_count> input;
    ActorTagSnapshot tags;
    // Owner-only provenance: these exact tags have no external contributor.
    // The remaining aggregate is restored before the frozen action is rebuilt.
    std::vector<TagId> action_only_tags;
};
}
