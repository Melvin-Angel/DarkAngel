#pragma once
#include <darkangel/asset_id.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace darkangel {
// One tick is 1024 action units. Global simulation continues during rate-zero hitstop.
inline constexpr unsigned action_tick_units=1024;
enum class ActionBlockKind {Cue,HitWindow,Invulnerability,MovementLock,ComboWindow};
enum class ActionPhase {Active,Completed,Cancelled};
enum class ActionEdge {End,Marker,Begin};
struct ActionBlock {unsigned id{},track{},begin{},end{};ActionBlockKind kind{};std::string key;};
struct ActionDefinition {AssetId id;std::string generation;unsigned duration{},loops{},priority{};bool upper_body{};std::vector<ActionBlock> blocks;};
ActionDefinition decode_action_source(std::string_view);
struct ActionState {std::uint64_t activation{},tick{},clock{};unsigned rate{action_tick_units};ActionPhase phase{ActionPhase::Active};bool entered{};std::string generation;};
struct ActionEvent {std::uint64_t activation{},time{};unsigned block{},track{},loop{};ActionEdge edge{};ActionBlockKind kind{};std::string key;auto operator<=>(const ActionEvent&)const=default;};
struct ActionInterval {unsigned block{},loop{},from{},to{};ActionBlockKind kind{};};
struct ActionBatch {std::vector<ActionEvent> events;std::vector<ActionInterval> traversed;};
// Per-activation timeline foundation. Slot/channel arbitration and ability validation
// are owning framework responsibilities; this object never applies damage or costs.
class ActionTimeline {
public:
    ActionTimeline(std::shared_ptr<const ActionDefinition>,std::uint64_t activation,std::uint64_t tick);
    ActionBatch enter();ActionBatch advance(std::uint64_t tick,unsigned rate=action_tick_units);
    ActionBatch cancel();void restore(const ActionState&);
    const ActionState& state()const{return state_;}
    const ActionDefinition& definition()const{return *definition_;}
private:std::shared_ptr<const ActionDefinition> definition_;ActionState state_;
};
// Bounded presentation deduplication owned by the presentation consumer, surviving
// timeline restore/replay. Full means explicit failure, never forget a recent cue.
class ActionCueLedger {
public:bool accept(const ActionEvent&);void retire(std::uint64_t activation);
private:std::vector<ActionEvent> seen_;
};
}
