#pragma once
#include <darkangel/action.hpp>
#include <darkangel/attributes.hpp>
#include <darkangel/combat_kit.hpp>
#include <darkangel/world.hpp>
#include <optional>

namespace darkangel {
// Native immediate-commit subset. Definitions are frozen at grant installation.
// Deferred reservations, effects/tags and targeting are not implied by this API.
struct AbilityCost {AttributeId attribute{};double amount{};};
struct AbilityDefinition {
    AssetId id;std::string generation;
    std::shared_ptr<const ActionDefinition> action;
    std::vector<AbilityCost> costs;
    std::uint32_t cooldown_group{};std::uint64_t cooldown_ticks{};
    InputEdge activate_on{InputEdge::Pressed};std::uint64_t minimum_held_us{};
    bool cancel_on_release{},interruptible{true};
};
// Shared validation/freezing for cooked definitions and runtime grant installation.
std::shared_ptr<const AbilityDefinition> freeze_ability_definition(const AbilityDefinition&,const AttributeSet&);
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
    Cooldown,Resources,OperationConflict,StaleOperation,HistoryFull,CancelDenied
};
struct AbilityRequest {
    AbilityOwnerHandle owner;std::uint64_t operation{},grant_generation{};
    CombatSlot slot{};InputEvent input;bool replace_active{};
};
struct AbilityReceipt {
    AbilityFailure failure{AbilityFailure::None};AbilityActivationHandle handle;
    bool committed{},duplicate{};
};
enum class AbilityActionReason {Started,Advanced,Completed,Cancelled,Replaced,GrantRemoved,Despawned,Death};
struct AbilityActionUpdate {
    AbilityActivationHandle handle;ActionPhase phase{};
    AbilityActionReason reason{AbilityActionReason::Completed};std::uint64_t tick{};ActionBatch batch;
};
struct AbilityAttributeValue {AttributeId id{};double value{};};
struct AbilityOwnerSnapshot {
    AbilityOwnerHandle owner;std::uint64_t tick{},grant_generation{},revision{};
    std::vector<AbilityAttributeValue> attributes;
    std::optional<AbilityActivationHandle> active;
    std::optional<ActionState> action;
    std::vector<std::pair<std::uint32_t,std::uint64_t>> cooldowns;
    std::size_t retained_operations{};
};
}
