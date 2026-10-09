#pragma once
#include <darkangel/world_session.hpp>
namespace darkangel {
struct ObserverAbilitySample {
    AbilityPublicFrame frame;double render_tick{},action_clock{};
    bool extrapolated{},held{};
};
// A bounded presentation buffer for one checked actor. Preparation pins native
// action generations; sampling has no timeline event, root or gameplay sink.
class ObserverAbility {
public:
    ObserverAbility(std::vector<AttributeDefinition>,AttributeId health,AttributeId maximum,
        std::span<const std::shared_ptr<const ActionDefinition>>);
    void push(const AbilityPublicFrame&);
    void reset(){frames_.clear();}
    ObserverAbilitySample sample(double render_tick)const;
    std::size_t size()const{return frames_.size();}
private:
    std::vector<AttributeDefinition> schema_;AttributeId health_{},maximum_{};
    std::map<AssetId,std::shared_ptr<const ActionDefinition>> actions_;
    std::deque<AbilityPublicFrame> frames_;
    double limit(const AbilityPublicFrame&)const;
};
}
