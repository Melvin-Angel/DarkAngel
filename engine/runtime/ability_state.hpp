#pragma once
#include <darkangel/ability.hpp>
#include <map>
#include <set>
#include <tuple>

namespace darkangel {
// Owned only by WorldSession, never a parallel world or client authority.
class AbilityState {
public:
    AbilityState(AbilityOwnerHandle,std::uint64_t tick,std::vector<AttributeDefinition>,AttributeId health,AttributeId maximum_health);
    std::vector<AbilityActionUpdate> equip(std::shared_ptr<const CombatKitDefinition>,const InputProfile&,std::span<const std::shared_ptr<const AbilityDefinition>>);
    AbilityFailure can_activate(const AbilityRequest&)const;
    std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> request(const AbilityRequest&);
    void begin_tick(std::uint64_t);
    std::vector<AbilityActionUpdate> finish_tick(unsigned,bool gameplay_hits=true);
    // Only a disposable owner prediction state may restore this prepared bundle.
    void restore_prediction(const AbilityOwnerSnapshot&);
    std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> request_wire(const AbilityIntent&,std::optional<AbilityFailure> forced={},std::optional<AbilityFailure> commitment_failure={});
    std::vector<AbilityActionUpdate> advance(std::uint64_t tick,unsigned rate);
    std::pair<AbilityFailure,std::vector<AbilityActionUpdate>> cancel(AbilityActivationHandle,AbilityActionReason);
    std::vector<AbilityActionUpdate> disconnect();
    void retire(std::uint64_t through);
    AbilityOwnerSnapshot snapshot()const;
    std::vector<AbilityAttributeValue> attribute_values()const;
    struct PendingHit {AbilityActivationHandle handle;std::shared_ptr<const AbilityDefinition> definition;AbilityMelee profile;ActionInterval interval;std::uint64_t tick{};};
    std::vector<PendingHit> pending_hits;
    bool remember_hit(const PendingHit&,std::uint64_t target,std::uint64_t epoch);
    std::vector<AbilityActionUpdate> damage(double);
    Health health()const;
    AbilityOwnerHandle owner()const{return owner_;}
private:
    struct Execution {
        CombatSlot slot;std::uint64_t grant_generation;
        std::shared_ptr<const AbilityDefinition> definition;ActionTimeline timeline;
    };
    struct Record {AbilityRequest request;AbilityReceipt receipt;std::optional<AbilityIntent> intent;};
    struct HeldInput {bool active{};std::uint64_t pressed{},released{},duration{};bool hold_sent{};};
    std::array<HeldInput,combat_slot_count> held_{};std::array<InputActionDefinition,combat_slot_count> input_{};std::uint64_t held_generation_{};
    AbilityOwnerHandle owner_;std::uint64_t tick_{},revision_{},next_activation_{1},highest_operation_{},retired_through_{};
    std::set<std::tuple<std::uint64_t,unsigned,unsigned,std::uint64_t,std::uint64_t>> hits_;
    AttributeSet attributes_;AttributeId health_,maximum_health_;
    std::optional<CombatKitInstance> kit_;
    std::array<std::shared_ptr<const AbilityDefinition>,combat_slot_count> grants_;
    std::array<bool,combat_slot_count> rearm_{};
    std::optional<Execution> active_;
    std::map<std::uint32_t,std::uint64_t> cooldowns_;
    std::map<std::uint64_t,Record> records_;
    AbilityFailure validate(const AbilityRequest&)const;
    std::vector<AbilityActionUpdate> stop(AbilityActionReason);
};
}
