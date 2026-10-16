#pragma once
#include <darkangel/character_motor.hpp>
#include <darkangel/world_ledger.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
// Scene-placeable world objects of M8: pickups, altars, and movers (doors, gates and movable platforms share one state
// machine). Each is a component on a scene object; the object's stable identity is its persistent identity in the world
// ledger. Definitions are authored data; the logical state they reach the ledger with is named properties with a
// policy each, never positions in mid-movement.

// A property's persistence policy as authored: {"scope":"save"|"session","resets":["scene_entry","rest","party_wipe"]}.
// Leaving it out uses the kind's default.
LedgerPolicy decode_policy(std::string_view json);
std::string encode_policy(const LedgerPolicy&);

// A requirement shared by doors, gates and anything else that can be refused: every named condition must hold.
//   item       an item the using player owns (quantity of it); consume takes them on success
//   tag        a world tag that must be held
//   encounter  an encounter of the same scene that must be completed
struct Requirement {AssetId item;unsigned quantity{1};bool consume{};std::string tag;StableId encounter;
    bool empty()const{return item==AssetId{}&&tag.empty()&&!encounter;}bool operator==(const Requirement&)const=default;};
Requirement decode_requirement(std::string_view json);

// A pickup: an item and a quantity a player collects by using it. "collected" has the policy below (default: saved,
// never reset, a one-time pickup). A collection that does not fit the player's inventory leaves it available.
struct PickupDefinition {AssetId item;unsigned quantity{1};std::string prompt{"Pick up"};double radius{2};LedgerPolicy collected{LedgerScope::Save};};
PickupDefinition decode_pickup(std::string_view json);

// An altar. Using it discovers it (once, for the whole party, kept for good) and opens the altar menu. checkpoint says when
// it becomes the party's respawn checkpoint; rest offers resting (health restored, the ledger's rest reset, the scene built
// again); fast_travel makes it a destination once discovered; save writes the save slot when the party rests here.
// The party arrives and respawns at the object's position moved by arrival (metres along its facing, then to its right).
enum class AltarCheckpoint:std::uint8_t{Never,Discover,Rest};
struct AltarDefinition {std::string name,prompt{"Altar"};double radius{2.5};AltarCheckpoint checkpoint{AltarCheckpoint::Rest};bool rest{true},fast_travel{true},save{true};double forward{1.5},right{};};
AltarDefinition decode_altar(std::string_view json);
// The point an altar's party arrives at, from its object's place and facing.
MotorVec altar_arrival(const AltarDefinition&,MotorVec position,double yaw);

// A mover: a kinematic box between its start (the object's place) and its end (start plus offset). Doors and gates are
// movers used on purpose that may be locked; platforms move on use, on a world event, or by themselves.
//   activation  interact: a player uses it; event: a world event named by event starts it; auto: it runs by itself
//   behaviour   toggle: each activation sends it to the other end; return: to the end, wait, back; loop: back and forth
//   locked      starts locked; the requirement unlocks it when a player uses it (and is consumed if configured)
//   travel      a scene transition raised when it is used and open (a door into another scene)
// Logical properties: "unlocked" (default saved, never reset) and "open" (it is at or going to its end; default for a
// door saved and reset on rest, for a platform session-only and reset on scene entry).
enum class MoverActivation:std::uint8_t{Interact,Event,Auto};
enum class MoverBehaviour:std::uint8_t{Toggle,Return,Loop};
struct MoverDefinition {
    MotorVec half{1,1.5,.25},offset{0,3,0};double speed{2},wait{2},radius{2.5};
    MoverActivation activation{MoverActivation::Interact};MoverBehaviour behaviour{MoverBehaviour::Toggle};
    std::string prompt{"Open"},locked_prompt{"Locked"},event;bool locked{},open{},door{true};Requirement requirement;
    std::optional<std::pair<AssetId,StableId>> travel;LedgerPolicy unlocked{LedgerScope::Save},opened{LedgerScope::Save,LedgerReset::Rest};
};
MoverDefinition decode_mover(std::string_view json);

// The running state of one mover: where it is between its ends (0 start, 1 end) and where it is going. Deterministic
// from fixed ticks; the box's velocity follows it.
class MoverState {
public:
    explicit MoverState(MoverDefinition);
    const MoverDefinition& definition()const{return definition_;}
    bool unlocked()const{return unlocked_;}bool open()const{return target_>0;}double at()const{return at_;}bool moving()const{return at_!=target_;}
    // Activation: the next end to go to. Locked movers refuse.
    bool activate();
    void unlock(){unlocked_=true;}
    // Ledger state: placed at its end at once (the transform is reconstructed, never saved mid-movement).
    void restore(bool unlocked,bool open);
    // One fixed tick of seconds; returns the box's velocity for the tick.
    MotorVec step(double seconds);
    MotorVec position(MotorVec start)const{return {start.x+definition_.offset.x*at_,start.y+definition_.offset.y*at_,start.z+definition_.offset.z*at_};}
private:MoverDefinition definition_;double at_{},target_{},waited_{};bool unlocked_{};
};

// What every member's client is told about the scene's world objects and the party: a small reliable snapshot, sent when
// it changes. It carries logical state only; collision and movement of movers reach clients through the collision stream.
enum class PartyMemberState:std::uint8_t{Alive,Downed};
struct WorldPartyMember {std::uint64_t network{};PartyMemberState state{};double revive{};bool operator==(const WorldPartyMember&)const=default;};
struct WorldDestination {AssetId scene;StableId altar;std::string name;bool operator==(const WorldDestination&)const=default;};
// R6.2: the host's confirmed state of one screen puzzle: presses matched so far out of its length, and whether it is solved.
struct WorldPuzzleState {StableId object;unsigned progress{},length{};bool solved{};bool operator==(const WorldPuzzleState&)const=default;};
struct WorldStateView {
    std::uint64_t revision{};std::vector<StableId> collected,discovered,unlocked,open;std::vector<WorldPartyMember> members;
    std::vector<WorldDestination> destinations;std::optional<WorldDestination> checkpoint;bool wiped{};double wipe{};
    std::vector<WorldPuzzleState> puzzles; // R6.2; written only when the scene has puzzles
    bool operator==(const WorldStateView&)const=default;};
std::string encode_world_state(const WorldStateView&);
WorldStateView decode_world_state(std::string_view);
// A client's request about the party's world beyond interactions: rest at an altar or fast travel from it, or (R6.2) one
// press on a screen puzzle (altar: the puzzle object, value: the press 1-9). The host validates every request.
enum class WorldRequestKind:std::uint8_t{Rest,Travel,Puzzle};
struct WorldRequest {WorldRequestKind kind{};StableId altar;WorldDestination destination;unsigned value{};bool operator==(const WorldRequest&)const=default;};
// One press on a puzzle as the host applies it: the next expected press advances it; a wrong press starts it over; a solved
// puzzle takes no more presses. Returns whether this press solved it.
bool apply_puzzle_press(const std::vector<unsigned>& sequence,WorldPuzzleState& state,unsigned press);
std::string encode_world_request(const WorldRequest&);
WorldRequest decode_world_request(std::string_view);
// The owner-service channel both travel on.
constexpr std::uint32_t world_state_channel=0x574f524c;
// Owner-channel payload conversion for the text encodings above.
std::vector<std::byte> world_bytes(std::string_view);
std::string world_text(std::span<const std::byte>);

// Party-level ledger records: the checkpoint and discovered altars. Nil scene: they belong to the party, not a scene.
PersistentId party_checkpoint_id();
std::optional<WorldDestination> read_checkpoint(const WorldLedger&);
void write_checkpoint(WorldLedger&,const WorldDestination&);
// Every discovered fast-travel altar in every scene, in ledger order.
std::vector<WorldDestination> fast_travel_destinations(const WorldLedger&);
void record_altar(WorldLedger&,AssetId scene,StableId altar,const AltarDefinition&);
bool altar_discovered(const WorldLedger&,AssetId scene,StableId altar);
// R6.2: a solved puzzle is a save-scoped ledger record of its scene object.
void record_puzzle_solved(WorldLedger&,AssetId scene,StableId object);
bool puzzle_solved(const WorldLedger&,AssetId scene,StableId object);
}
