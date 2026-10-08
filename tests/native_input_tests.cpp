#include "native_input.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
using namespace darkangel;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
unsigned count(const InputBatch& batch,unsigned action,InputEdge edge){unsigned result{};for(const auto& event:batch.events)if(event.action==action&&event.edge==edge)++result;return result;}
}
int main(){try{
    std::ifstream file(std::filesystem::path(DAE_SOURCE_DIR)/"content/input/royal_player.dainput");std::string source{std::istreambuf_iterator<char>(file),{}};InputManager manager(decode_input_source(source));auto controls=manager.controls();darkangel::editor_app::NativeInput backend(controls);
    backend.message(WM_KEYDOWN,'W',0);backend.message(WM_KEYDOWN,'W',0);backend.message(WM_KEYUP,'W',0);auto raw=backend.poll(false);auto batch=manager.update(raw.time_us,raw.samples);require(count(batch,1,InputEdge::Pressed)==1&&count(batch,1,InputEdge::Released)==1,"Win32 repeat/short key edges");
    backend.message(WM_LBUTTONDOWN,0,0);backend.message(WM_LBUTTONUP,0,0);raw=backend.poll(false);batch=manager.update(raw.time_us,raw.samples);require(count(batch,8,InputEdge::Pressed)==1&&count(batch,8,InputEdge::Tapped)==1,"Win32 mouse tap edges");
    for(unsigned i=0;i<300;++i){backend.message(WM_KEYDOWN,'W',0);backend.message(WM_KEYUP,'W',0);}raw=backend.poll(false);require(raw.overflow&&raw.samples.size()==controls.size(),"Win32 overflow did not return bounded cancellation snapshot");batch=manager.update(raw.time_us,raw.samples,false);require(!manager.state(1).down&&!count(batch,1,InputEdge::Tapped),"Win32 overflow retained movement/tap");
    std::cout<<"Native Win32 keyboard/mouse edge capture, repeat rejection, between-frame taps and bounded overflow cancellation passed; XInput API="<<backend.gamepad_available()<<" (hardware not exercised)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
