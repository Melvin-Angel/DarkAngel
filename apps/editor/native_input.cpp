#include "native_input.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace darkangel::editor_app {
namespace {
std::uint64_t clock_us(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
constexpr WORD buttons[]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_LEFT_SHOULDER,XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB,XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_BACK};
float stick(SHORT value,SHORT deadzone){float magnitude=std::abs(static_cast<float>(value));if(magnitude<=deadzone)return 0;return std::copysign(std::clamp((magnitude-deadzone)/(32767.f-deadzone),0.f,1.f),static_cast<float>(value));}
float trigger(BYTE value){return value<=XINPUT_GAMEPAD_TRIGGER_THRESHOLD?0.f:float(value-XINPUT_GAMEPAD_TRIGGER_THRESHOLD)/(255-XINPUT_GAMEPAD_TRIGGER_THRESHOLD);}
float gamepad(const XINPUT_GAMEPAD& pad,unsigned code){if(code<14)return pad.wButtons&buttons[code]?1.f:0.f;switch(code){case 14:return stick(pad.sThumbLX,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);case 15:return stick(pad.sThumbLY,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);case 16:return stick(pad.sThumbRX,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);case 17:return stick(pad.sThumbRY,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);case 18:return trigger(pad.bLeftTrigger);case 19:return trigger(pad.bRightTrigger);}return 0;}
}
NativeInput::NativeInput(std::span<const InputControl> controls){for(auto control:controls)values_[control]=0;module_=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module_)module_=LoadLibraryExW(L"xinput9_1_0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(module_)get_state_=reinterpret_cast<GetState>(GetProcAddress(module_,"XInputGetState"));}
NativeInput::~NativeInput(){if(module_)FreeLibrary(module_);}
void NativeInput::record(InputControl control,float value,bool cancelled){auto found=values_.find(control);if(found==values_.end()||found->second==value)return;found->second=value;time_=std::max(time_,clock_us());if(queued_.size()<256)queued_.push_back({control,value,time_,cancelled});else overflow_=true;}
void NativeInput::message(UINT message,WPARAM wparam,LPARAM lparam){
    if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){auto key=static_cast<UINT>(wparam);if(key==VK_SHIFT)key=MapVirtualKeyW((static_cast<UINT>(lparam)>>16)&255,MAPVK_VSC_TO_VK_EX);if(key==VK_CONTROL)key=(lparam&(1<<24))?VK_RCONTROL:VK_LCONTROL;if(key==VK_MENU)key=(lparam&(1<<24))?VK_RMENU:VK_LMENU;record({InputDevice::Keyboard,static_cast<std::uint16_t>(key),0},message==WM_KEYDOWN||message==WM_SYSKEYDOWN?1.f:0.f);return;}
    switch(message){case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:record({InputDevice::Mouse,0,0},1);break;case WM_LBUTTONUP:record({InputDevice::Mouse,0,0},0);break;case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:record({InputDevice::Mouse,1,0},1);break;case WM_RBUTTONUP:record({InputDevice::Mouse,1,0},0);break;case WM_MBUTTONDOWN:record({InputDevice::Mouse,2,0},1);break;case WM_MBUTTONUP:record({InputDevice::Mouse,2,0},0);break;case WM_XBUTTONDOWN:record({InputDevice::Mouse,static_cast<std::uint16_t>(GET_XBUTTON_WPARAM(wparam)==XBUTTON1?3:4),0},1);break;case WM_XBUTTONUP:record({InputDevice::Mouse,static_cast<std::uint16_t>(GET_XBUTTON_WPARAM(wparam)==XBUTTON1?3:4),0},0);break;}
}
NativeInputFrame NativeInput::poll(bool hardware){
    XINPUT_STATE pads[4]{};bool connected[4]{};if(hardware)for(unsigned i=0;i<4;++i)connected[i]=get_state_&&get_state_(i,&pads[i])==ERROR_SUCCESS;
    constexpr int mouse_keys[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
    if(hardware)for(const auto& [control,value]:values_){if(control.device==InputDevice::Keyboard)record(control,(GetAsyncKeyState(control.code)&0x8000)?1.f:0.f);else if(control.device==InputDevice::Mouse)record(control,(GetAsyncKeyState(mouse_keys[control.code])&0x8000)?1.f:0.f);else record(control,connected[control.slot]?gamepad(pads[control.slot].Gamepad,control.code):0,!connected[control.slot]);}
    auto now=std::max(time_,clock_us());NativeInputFrame frame{now,{},overflow_};if(overflow_){for(const auto& [control,value]:values_)frame.samples.push_back({control,value,now,true});}else frame.samples=std::move(queued_);queued_.clear();overflow_=false;time_=now;return frame;
}
}
