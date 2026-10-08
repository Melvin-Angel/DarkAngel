#pragma once
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <darkangel/input.hpp>
#include <map>
namespace darkangel::editor_app {
struct NativeInputFrame {std::uint64_t time_us{};std::vector<InputSample> samples;bool overflow{};};
class NativeInput {
public:
    explicit NativeInput(std::span<const InputControl>);
    ~NativeInput();
    void message(UINT,WPARAM,LPARAM);
    NativeInputFrame poll(bool hardware=true);
    bool gamepad_available() const{return get_state_!=nullptr;}
private:
    using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    HMODULE module_{};GetState get_state_{};
    std::map<InputControl,float> values_;std::vector<InputSample> queued_;bool overflow_{};std::uint64_t time_{};
    void record(InputControl,float,bool cancelled=false);
};
}
