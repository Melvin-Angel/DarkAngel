#pragma once
#include <darkangel/ability.hpp>
#include <map>

namespace darkangel {
// Owned only by WorldSession, never a parallel world or client authority.
class AbilityState {
public:
    AbilityState(AbilityOwnerHandle,std::uint64_t tick,std::vector<AttributeDefinition>,AttributeId health,AttributeId maximum_health);
    std::vector<AbilityActionUpdate> equip(std::shared_ptr<const CombatKitDefinition>,const InputProfile&,std::span<const std::shared_ptr<const AbilityDefinition>>);
    AbilityFailure can_activate(const AbilityRequest&)const;
    std::pair<AbilityReceipt,std::vector<AbilityActionUpdate>> request(const AbilityRequest&);
    std::vector<AbilityActionUpdate> advance(std::uint64_t tick,unsigned rate);
    std::pair<AbilityFailure,std::vector<AbilityActionUpdate>> cancel(AbilityActivationHandle,AbilityActionReason);
    void retire(std::uint64_t through);
    AbilityOwnerSnapshot snapshot()const;
    Health health()const;
    AbilityOwnerHandle owner()const{return owner_;}
private:
    struct Execution {
        CombatSlot slot;std::uint64_t grant_generation;
        std::shared_ptr<const AbilityDefinition> definition;ActionTimeline timeline;
    };
    struct Record {AbilityRequest request;AbilityReceipt receipt;};
    AbilityOwnerHandle owner_;std::uint64_t tick_{},revision_{},next_activation_{1},highest_operation_{},retired_through_{};
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
