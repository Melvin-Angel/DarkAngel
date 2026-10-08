#pragma once
#include <darkangel/asset_id.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace darkangel {
enum class InputDevice {Keyboard,Mouse,Gamepad};
struct InputControl {InputDevice device{};std::uint16_t code{};std::uint8_t slot{};auto operator<=>(const InputControl&) const=default;};
InputControl input_control(std::string_view); // stable Keyboard.*, Mouse.* and GamepadN.* names
std::string input_control_name(InputControl);
bool input_axis(InputControl);
enum class InputActionKind {Button,Axis};
enum class InputConsumption {None,Trigger,All};
struct InputActionDefinition {std::uint32_t id{};std::string name;InputActionKind kind{};std::uint64_t hold_us{},tap_us{200000};float deadzone{};};
struct InputBinding {std::uint32_t id{},action{};std::vector<InputControl> controls;InputConsumption consumption{};int priority{};float scale{1},threshold{.5f};};
struct InputProfile {AssetId id;std::vector<InputActionDefinition> actions;std::vector<InputBinding> bindings;};
InputProfile decode_input_source(std::string_view);
void validate_input_profile(const InputProfile&);
struct InputSample {InputControl control;float value{};std::uint64_t time_us{};bool cancelled{};};
enum class InputEdge {Pressed,Hold,Released,Tapped};
struct InputEvent {std::uint32_t action{};InputEdge edge{};std::uint64_t time_us{},held_us{};float value{};bool cancelled{};};
struct InputState {bool down{};float value{};std::uint64_t held_us{};};
struct InputBatch {std::vector<InputEvent> events;};
// Local intent only. Action events never bypass authoritative ability/motor validation.
// Timestamps are monotonic microseconds. Identical-time changes to different controls
// resolve together; repeated changes to one control retain their order (short taps).
class InputManager {
public:
    explicit InputManager(InputProfile);
    ~InputManager();
    InputManager(const InputManager&)=delete;
    InputManager& operator=(const InputManager&)=delete;
    InputBatch update(std::uint64_t now_us,std::span<const InputSample>,bool enabled=true);
    InputBatch rebind(InputProfile,std::uint64_t now_us,std::span<const InputSample> current_controls);
    InputState state(std::uint32_t action) const;
    const InputProfile& profile() const;
    std::vector<InputControl> controls() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
