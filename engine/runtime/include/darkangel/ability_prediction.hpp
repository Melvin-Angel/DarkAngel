#pragma once
#include <darkangel/ability.hpp>

namespace darkangel {
struct AbilityPredictionInput {AbilityIntent intent;std::uint64_t parent_operation{};};
struct AbilityPredictionMotion {std::uint64_t tick{},activation{};ActionMotion local;};
// One owner view rebuilt from an authoritative bundle plus bounded pending input.
// Uses native commitment/gesture/timeline rules with all hit sinks disabled.
// Predicted activation identities are provisional; operation IDs correlate receipts.
// Copy, reconcile the candidate, prepare motor replay, then publish together.
class OwnerAbilityPrediction {
public:
    OwnerAbilityPrediction(const AbilityOwnerSnapshot&,std::vector<AttributeDefinition>,
        std::shared_ptr<const CombatKitDefinition>,const InputProfile&,
        std::span<const std::shared_ptr<const AbilityDefinition>>,std::uint64_t avatar_epoch=1);
    ~OwnerAbilityPrediction();
    OwnerAbilityPrediction(const OwnerAbilityPrediction&);
    OwnerAbilityPrediction& operator=(const OwnerAbilityPrediction&);
    OwnerAbilityPrediction(OwnerAbilityPrediction&&)noexcept;
    OwnerAbilityPrediction& operator=(OwnerAbilityPrediction&&)noexcept;
    AbilityPredictionMotion advance(std::uint64_t,std::span<const AbilityPredictionInput> = {},unsigned rate=action_tick_units);
    // Terminal acceptance is retained until exact bundle inclusion, never subtracted twice.
    void receipt(const AbilityOperationNotice&);
    // Older revisions are ignored. Any missing history/generation/capacity fails closed.
    bool reconcile(const AbilityOwnerSnapshot&);
    const AbilityOwnerSnapshot& baseline()const;
    const AbilityOwnerSnapshot& view()const;
    std::span<const AbilityPredictionMotion> replay_motion()const;
    std::size_t pending()const;
    bool needs_resync()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
