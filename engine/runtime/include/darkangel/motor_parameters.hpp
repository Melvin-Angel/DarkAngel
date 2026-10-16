#pragma once
#include <cstdint>
namespace darkangel {
// Authored locomotion constants one motor simulates with (R3 Movement Profile). A default value is exactly the
// legacy built-in motor. Host, owner prediction and replay must construct their motors from the same frozen value;
// a session never changes it in place.
struct MotorParameters {
    double walk_speed_mps{5},crouch_speed_mps{2},acceleration_mps2{30},jump_velocity_mps{7},gravity_mps2{20};
    std::uint32_t coyote_ticks{6},jump_buffer_ticks{6};
    auto operator<=>(const MotorParameters&) const=default;
};
// Frozen valid ranges (inclusive). Speeds stay far below the per-tick collision sweep the bounded contact
// collector was qualified with; timers fit the replicated motor state bound below.
struct MotorParameterRange {double minimum,maximum;};
inline constexpr MotorParameterRange motor_walk_speed_range{0.5,12},motor_crouch_speed_range{0.25,12},
    motor_acceleration_range{1,120},motor_jump_velocity_range{0,20},motor_gravity_range{1,60};
inline constexpr std::uint32_t motor_timer_tick_limit=30; // coyote and jump buffer: 0..30 ticks (half a second)
// Throws std::runtime_error when a value is not finite or outside its range.
void validate_motor_parameters(const MotorParameters&);
}
