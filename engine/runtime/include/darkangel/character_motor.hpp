#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string_view>
#include <string>
#include <vector>
#include <array>
namespace darkangel {
// Process-wide diagnostic counters for the private Jolt allocator hooks.
struct PhysicsAllocationCounters {std::uint64_t allocations{},reallocations{},requested_bytes{},frees{};};
void track_physics_allocations(bool);PhysicsAllocationCounters physics_allocation_counters();
// Meters, seconds, Y-up right-handed. Tick is always 1/60 second.
struct MotorVec {double x{},y{},z{}; auto operator<=>(const MotorVec&) const=default;};
struct MotorInput {std::uint64_t sequence{},tick{},epoch{1};double x{},z{},yaw{};bool jump{},crouch{};};
struct MotionRequest {MotorVec root{},impulse{};std::uint64_t action{};bool lock{};};
struct MotorCommand {MotorInput input;MotionRequest motion;};
struct MotorState {
    std::uint64_t tick{},sequence{},epoch{1},topology{1},support{},action{};
    // Movement memory excludes root requests: X/Z support-relative on ground,
    // world-space in air; Y is native world velocity (gravity/impulses/support).
    MotorVec position{},velocity{},momentum{},desired{},achieved{},support_local{},support_velocity{};
    double yaw{};std::uint32_t coyote{},jump_buffer{};bool grounded{},crouched{};
};
void validate_motor_input(const MotorInput&);
void validate_motor_state(const MotorState&);
struct CollisionBox {
    std::uint64_t id{};MotorVec center{},half{1,1,1},velocity{},angular{};double yaw{},roll{};bool moving{};
    bool dynamic{};double mass{50};
    // All zero selects authored yaw/roll. Captures retain full normalized rotation.
    std::array<double,4> rotation{};
    bool sensor{};
};
enum CollisionLayer : unsigned {StaticCollision=1,KinematicCollision=2,DynamicCollision=4,CharacterCollision=8,SensorCollision=16,AllCollision=31};
struct QueryFilter {unsigned layers{StaticCollision|KinematicCollision|DynamicCollision|CharacterCollision};std::uint64_t owner{};bool sensors{},backfaces{};};
struct CollisionHit {
    std::uint64_t identity{};double fraction{};bool character{};
    std::uint64_t world{},tick{},topology{},generation{},epoch{};
    // Authored keys within the collision product, never Jolt identifiers.
    std::uint32_t subshape{},material{};MotorVec point{},normal{};bool sensor{};
};
struct CollisionQuery {std::vector<CollisionHit> hits;bool overflow{};};
enum class SensorPhase {Begin,End};
struct SensorEvent {std::uint64_t world{},tick{},token{},sensor{},other{},other_generation{},epoch{};bool character{};SensorPhase phase{};};
// Tick snapshots of authoritative characters, never independently simulated in replay.
struct CollisionActor {std::uint64_t id{},epoch{};MotorVec foot{},velocity{};double yaw{};bool crouched{};};
struct CollisionMeshData;
class CollisionGeometry {
public:
    explicit CollisionGeometry(const CollisionMeshData&);~CollisionGeometry();
    CollisionGeometry(const CollisionGeometry&)=delete;
    const CollisionMeshData& definition()const;
private:struct Impl;std::unique_ptr<Impl> impl_;friend class PhysicsWorld;friend class CharacterMotor;
};
struct CollisionMesh {std::uint64_t id{};std::shared_ptr<const CollisionGeometry> geometry;};
struct CollisionDefinition;
struct CollisionFrame {std::uint64_t tick{},topology{};std::vector<CollisionBox> boxes;std::vector<CollisionActor> actors;std::vector<CollisionMesh> meshes;};
// Debt is retained. A frame cannot enlarge the step or silently skip a tick.
class SimulationClock {
public:
    unsigned advance(double seconds);double debt() const{return debt_;}std::uint64_t tick() const{return tick_;}
private:double debt_{};std::uint64_t tick_{};
};
class CharacterMotor;
class PhysicsWorld {
public:
    enum class Mode {Authoritative,Prediction,Replay};
    explicit PhysicsWorld(Mode=Mode::Authoritative);~PhysicsWorld();PhysicsWorld(const PhysicsWorld&)=delete;
    Mode mode()const;
    bool gameplay_queries_ready()const; // authoritative tick after every motor post_physics

    bool needs_resync()const;bool sleeping(std::uint64_t)const;
    void apply_impulse(std::uint64_t,MotorVec);
    void add(CollisionBox);void remove(std::uint64_t);void set_platform(std::uint64_t,MotorVec velocity,MotorVec angular={});
    void add_mesh(CollisionMesh);void load_scene(const CollisionDefinition&);
    void load_cooked(std::string_view);
    bool update_prediction(const CollisionFrame&,CharacterMotor&);
    void step();CollisionFrame capture() const;void load(const CollisionFrame&,std::uint64_t replay_owner=0);
    std::uint64_t tick() const;std::uint64_t topology() const;
    CollisionQuery overlap(MotorVec center,double radius,unsigned limit=16,QueryFilter={}) const;
    CollisionQuery sweep(MotorVec from,MotorVec delta,double radius,unsigned limit=16,QueryFilter={}) const;
    CollisionQuery query_ray(MotorVec from,MotorVec delta,QueryFilter={})const;
    bool valid_hit(const CollisionHit&)const;
    std::string material_key(const CollisionHit&)const;
    // After all motor post_physics calls. One bounded, atomic sensor event batch
    // per authoritative tick. Sleep does not imply exit; cancellation uses token.
    void finish_tick();std::vector<SensorEvent> take_sensor_events();
    bool ray(MotorVec from,MotorVec delta,std::uint64_t& identity) const;
private:struct Impl;std::unique_ptr<Impl> impl_;friend class CharacterMotor;
};
class CharacterMotor {
public:
    CharacterMotor(PhysicsWorld&,MotorVec foot,std::uint64_t identity=0,bool crouched=false);~CharacterMotor();CharacterMotor(const CharacterMotor&)=delete;
    MotorState step(const MotorInput&,const MotionRequest& = {});
    std::uint64_t identity() const;
    void post_physics();bool needs_resync() const;const MotorState& state() const;void restore(const MotorState&);bool teleport(MotorVec foot);
private:struct Impl;std::unique_ptr<Impl> impl_;friend class PhysicsWorld;
};
enum class ReplayResult {Applied,MissingHistory,TopologyMismatch,WorkLimit,Discontinuity,Invalid};
class CollisionHistory {
public:
    void retain(CollisionFrame);const CollisionFrame* find(std::uint64_t) const;
private:std::deque<CollisionFrame> frames_;
};
// All work happens in a disposable Jolt world. Failure leaves output unchanged.
// Multi-character history requires an explicit owner identity. Single-character
// histories infer it for existing callers; ambiguity fails without changing output.
ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorInput>,const CollisionHistory&,MotorState& output,std::uint64_t owner=0);
ReplayResult replay_motor(const MotorState&,std::span<const MotorCommand>,const CollisionHistory&,MotorState&,std::uint64_t owner=0);
class OwnerPrediction {
public:
    OwnerPrediction(PhysicsWorld&,CharacterMotor&);
    MotorState predict(const MotorInput&,const MotionRequest& = {});
    ReplayResult reconcile(const MotorState&);
    ReplayResult reconcile(const MotorState&,const CollisionHistory& authoritative_history);
    // Recompute root/action requests after coherent ability correction. Inputs must
    // match retained commands; validation/replay failure publishes no candidate.
    ReplayResult reconcile(const MotorState&,const CollisionHistory&,std::span<const MotorCommand> regenerated);
    bool needs_resync()const{return resync_||world_.needs_resync()||motor_.needs_resync();}std::size_t pending()const{return commands_.size();}
    MotorVec visual_offset()const{return visual_offset_;}
private:PhysicsWorld& world_;CharacterMotor& motor_;CollisionHistory history_;std::deque<MotorCommand> commands_;bool resync_{};MotorVec visual_offset_{};
};
class ObserverMotor {
public:void push(MotorState);MotorState sample(double tick) const;std::size_t size()const{return frames_.size();}
private:std::deque<MotorState> frames_;
};
}
