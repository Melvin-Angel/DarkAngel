#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/animation.hpp>
#include <darkangel/joint_mask.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
namespace darkangel {
// One tick is 1024 action units. Global simulation continues during rate-zero hitstop.
inline constexpr unsigned action_tick_units=1024;
enum class ActionBlockKind {Cue,HitWindow,Invulnerability,MovementLock,ComboWindow,Commit,Vfx,Audio,Gameplay,Projectile,Aoe,Hitbox};
enum class ActionPhase {Active,Completed,Cancelled};
enum class ActionEdge {End,Marker,Begin};
// Version 1 payload uses the authoritative socket-relative swept-sphere query.
struct ActionHit {std::array<double,3> offset{};double radius{.5},power{};std::uint32_t evaluator{1},damage_type{1};std::string socket;};
struct ActionProjectilePreparation {unsigned emergence_block{},move_block{},release_block{};std::array<double,3> offset{};double rise{};std::string socket;double ready_pitch_degrees{};};
struct ActionProjectile {double speed{20},gravity{},range{30};unsigned lifetime{120};AssetId visual_model{};std::optional<ActionProjectilePreparation> preparation{};};
struct EffectDefinition;
// Direct typed references. The asset loader resolves immutable definitions;
// editor source locators never enter runtime state.
struct ActionEffect {AssetId asset;double power{};std::shared_ptr<const EffectDefinition> definition;};
struct ActionBlock {unsigned id{},track{},begin{},end{};ActionBlockKind kind{};std::string key;std::optional<ActionHit> hit{};std::optional<ActionProjectile> projectile{};unsigned area_period{};std::vector<ActionEffect> effects{};};
struct ActionClipBinding {ClipDefinition clip;std::string archive_generation;bool motor_root{};};
struct ActionDefinition {AssetId id;std::string generation;unsigned duration{},loops{},priority{};bool upper_body{};std::vector<ActionBlock> blocks;std::optional<ActionClipBinding> motion;std::optional<JointMaskDefinition> mask;};
ActionDefinition decode_action_source(std::string_view);
// A presentation-only one-shot: full-body, one loop, a bound clip without root
// ownership and cue blocks only. Presentation consumers never execute its blocks.
bool presentation_only_action(const ActionDefinition&);
// Pure timeline-local root request. The owning motor applies collision and
// publishes achieved motion; neither clip sampling nor this helper moves actors.
struct ActionMotion {ClipMotion root;bool movement_lock{};};
ActionMotion action_motion_between(const ActionDefinition&,std::uint64_t from_clock,std::uint64_t to_clock);
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
