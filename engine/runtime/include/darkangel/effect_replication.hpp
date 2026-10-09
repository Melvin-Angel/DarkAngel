#pragma once
#include <darkangel/effects.hpp>
#include <map>
namespace darkangel {
// Presentation attribution survives source destruction. Private captured power
// and source statistics stay in native authority, never in this wire value.
struct EffectAttribution {std::uint64_t session_epoch{},source_network{},activation{};auto operator<=>(const EffectAttribution&)const=default;};
struct EffectReplica {
 std::uint64_t handle{};AssetId definition;std::string generation;EffectAttribution credit;
 std::uint64_t start{},end{},next_period{};bool suppressed{};
 bool operator==(const EffectReplica&)const=default;
};
struct EffectFrame {
 AbilityOwnerHandle owner;std::uint64_t world_revision{},avatar_epoch{},tick{},revision{};
 AttributeVisibility audience{AttributeVisibility::Public};std::vector<EffectReplica> effects;
};
struct EffectCueKey {std::uint64_t session_epoch{},network{},avatar_epoch{},handle{};AssetId cue;auto operator<=>(const EffectCueKey&)const=default;};
struct EffectCueState {EffectCueKey id;AbilityOwnerHandle target;EffectAttribution credit;std::uint64_t start{},end{};std::string generation,key;bool operator==(const EffectCueState&)const=default;};
enum class EffectCueEdge {Begin,Update,End};
struct EffectCueUpdate {EffectCueEdge edge;EffectCueState state;};
// Prepared, read-only current status state. It never executes damage, modifiers,
// expiry transactions or historical application events on a presentation world.
class EffectPresentation {
public:
 EffectPresentation(AttributeVisibility,const AttributeSet&,std::shared_ptr<const TagDictionary>,std::span<const std::shared_ptr<const EffectDefinition>>);
 bool push(const EffectFrame&);
 const std::optional<EffectFrame>& current()const{return current_;}
 std::vector<EffectCueState> cues()const;
 std::vector<EffectCueUpdate> drain_cues();
private:
 AttributeVisibility audience_;std::map<AssetId,std::shared_ptr<const EffectDefinition>> definitions_;std::optional<EffectFrame> current_;
 std::vector<EffectCueUpdate> cue_updates_;
 std::map<EffectCueKey,EffectCueState> cue_states(const EffectFrame&)const;
};
}
