#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
namespace darkangel {
// Meters, seconds, Y-up right-handed. Tick is always 1/60 second.
struct MotorVec {double x{},y{},z{}; auto operator<=>(const MotorVec&) const=default;};
struct MotorInput {std::uint64_t sequence{},tick{},epoch{1};double x{},z{},yaw{};bool jump{},crouch{};};
struct MotionRequest {MotorVec root{},impulse{};std::uint64_t action{};bool lock{};};
struct MotorCommand {MotorInput input;MotionRequest motion;};
struct MotorState {
    std::uint64_t tick{},sequence{},epoch{1},topology{1},support{},action{};
    MotorVec position{},velocity{},momentum{},desired{},achieved{},support_local{},support_velocity{};
    double yaw{};std::uint32_t coyote{},jump_buffer{};bool grounded{},crouched{};
};
void validate_motor_input(const MotorInput&);
void validate_motor_state(const MotorState&);
struct CollisionBox {std::uint64_t id{};MotorVec center{},half{1,1,1},velocity{},angular{};double yaw{},roll{};bool moving{};};
struct CollisionHit {std::uint64_t identity{};double fraction{};bool character{};};
struct CollisionQuery {std::vector<CollisionHit> hits;bool overflow{};};
struct CollisionFrame {std::uint64_t tick{},topology{};std::vector<CollisionBox> boxes;};
// Debt is retained. A frame cannot enlarge the step or silently skip a tick.
class SimulationClock {
public:
    unsigned advance(double seconds);double debt() const{return debt_;}std::uint64_t tick() const{return tick_;}
private:double debt_{};std::uint64_t tick_{};
};
class PhysicsWorld {
public:
    PhysicsWorld();~PhysicsWorld();PhysicsWorld(const PhysicsWorld&)=delete;
    void add(CollisionBox);void remove(std::uint64_t);void set_platform(std::uint64_t,MotorVec velocity,MotorVec angular={});
    void load_cooked(std::string_view);
    void step();CollisionFrame capture() const;void load(const CollisionFrame&);
    std::uint64_t tick() const;std::uint64_t topology() const;
    CollisionQuery overlap(MotorVec center,double radius,unsigned limit=16) const;
    CollisionQuery sweep(MotorVec from,MotorVec delta,double radius,unsigned limit=16) const;
    bool ray(MotorVec from,MotorVec delta,std::uint64_t& identity) const;
private:struct Impl;std::unique_ptr<Impl> impl_;friend class CharacterMotor;
};
class CharacterMotor {
public:
    CharacterMotor(PhysicsWorld&,MotorVec foot,std::uint64_t identity=0);~CharacterMotor();CharacterMotor(const CharacterMotor&)=delete;
    MotorState step(const MotorInput&,const MotionRequest& = {});
    void post_physics();bool needs_resync() const;const MotorState& state() const;void restore(const MotorState&);bool teleport(MotorVec foot);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
enum class ReplayResult {Applied,MissingHistory,TopologyMismatch,WorkLimit,Discontinuity,Invalid};
class CollisionHistory {
public:
    void retain(CollisionFrame);const CollisionFrame* find(std::uint64_t) const;
private:std::deque<CollisionFrame> frames_;
};
// All work happens in a disposable Jolt world. Failure leaves output unchanged.
ReplayResult replay_motor(const MotorState& baseline,std::span<const MotorInput>,const CollisionHistory&,MotorState& output);
ReplayResult replay_motor(const MotorState&,std::span<const MotorCommand>,const CollisionHistory&,MotorState&);
class OwnerPrediction {
public:
    OwnerPrediction(PhysicsWorld&,CharacterMotor&);
    MotorState predict(const MotorInput&,const MotionRequest& = {});
    ReplayResult reconcile(const MotorState&);
    bool needs_resync()const{return resync_;}std::size_t pending()const{return commands_.size();}
    MotorVec visual_offset()const{return visual_offset_;}
private:PhysicsWorld& world_;CharacterMotor& motor_;CollisionHistory history_;std::deque<MotorCommand> commands_;bool resync_{};MotorVec visual_offset_{};
};
class ObserverMotor {
public:void push(MotorState);MotorState sample(double tick) const;std::size_t size()const{return frames_.size();}
private:std::deque<MotorState> frames_;
};
}
