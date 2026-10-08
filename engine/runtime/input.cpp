#include <darkangel/input.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace darkangel {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
const std::map<std::string,std::uint16_t> keys={{"Space",32},{"Escape",27},{"Enter",13},{"Tab",9},{"Left",37},{"Up",38},{"Right",39},{"Down",40},{"ShiftLeft",160},{"ShiftRight",161},{"ControlLeft",162},{"ControlRight",163},{"AltLeft",164},{"AltRight",165}};
const std::vector<std::string> mouse={"Left","Right","Middle","X1","X2"};
const std::vector<std::string> pad={"South","East","West","North","DpadUp","DpadRight","DpadDown","DpadLeft","LeftShoulder","RightShoulder","LeftThumb","RightThumb","Start","Back","LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger"};
void emit(InputBatch& batch,InputEvent event){require(batch.events.size()<4096,"Input event work limit");batch.events.push_back(event);}
}
InputControl input_control(std::string_view name){
    if(name.starts_with("Keyboard.")){auto key=std::string(name.substr(9));if(key.size()==1&&((key[0]>='A'&&key[0]<='Z')||(key[0]>='0'&&key[0]<='9')))return {InputDevice::Keyboard,static_cast<std::uint16_t>(key[0]),0};require(keys.contains(key),"Unknown keyboard control");return {InputDevice::Keyboard,keys.at(key),0};}
    if(name.starts_with("Mouse.")){auto it=std::find(mouse.begin(),mouse.end(),name.substr(6));require(it!=mouse.end(),"Unknown mouse control");return {InputDevice::Mouse,static_cast<std::uint16_t>(it-mouse.begin()),0};}
    require(name.size()>9&&name.starts_with("Gamepad")&&name[7]>='0'&&name[7]<='3'&&name[8]=='.',"Unknown input device/control");auto it=std::find(pad.begin(),pad.end(),name.substr(9));require(it!=pad.end(),"Unknown gamepad control");return {InputDevice::Gamepad,static_cast<std::uint16_t>(it-pad.begin()),static_cast<std::uint8_t>(name[7]-'0')};
}
std::string input_control_name(InputControl control){
    if(control.device==InputDevice::Keyboard){require(control.slot==0,"Keyboard device slot");for(const auto& [name,code]:keys)if(code==control.code)return "Keyboard."+name;if((control.code>='A'&&control.code<='Z')||(control.code>='0'&&control.code<='9'))return "Keyboard."+std::string(1,static_cast<char>(control.code));}
    else if(control.device==InputDevice::Mouse){require(control.slot==0&&control.code<mouse.size(),"Mouse control range");return "Mouse."+mouse[control.code];}
    else if(control.device==InputDevice::Gamepad){require(control.slot<4&&control.code<pad.size(),"Gamepad control range");return "Gamepad"+std::to_string(control.slot)+"."+pad[control.code];}
    throw std::runtime_error("Invalid input control");
}
bool input_axis(InputControl control){return control.device==InputDevice::Gamepad&&control.code>=14;}
void validate_input_profile(const InputProfile& profile){
    AssetId::parse(profile.id.text());require(!profile.actions.empty()&&profile.actions.size()<=64&&profile.bindings.size()<=128,"Input profile action/binding limit");std::map<std::uint32_t,InputActionKind> actions;std::set<std::string> names;std::set<std::uint32_t> bindings;std::set<InputControl> controls;
    for(const auto& action:profile.actions){require(action.id&&actions.emplace(action.id,action.kind).second&&!action.name.empty()&&action.name.size()<=64&&action.name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==action.name.npos&&names.insert(action.name).second,"Input action identity/name");require(action.kind==InputActionKind::Button||action.kind==InputActionKind::Axis,"Input action kind");require(action.hold_us<=10000000&&action.tap_us<=10000000&&std::isfinite(action.deadzone)&&action.deadzone>=0&&action.deadzone<1,"Input action timing/deadzone");}
    for(const auto& binding:profile.bindings){require(binding.id&&bindings.insert(binding.id).second&&actions.contains(binding.action)&&!binding.controls.empty()&&binding.controls.size()<=4,"Input binding identity/action/chord limit");require(binding.priority>=-255&&binding.priority<=255&&std::isfinite(binding.scale)&&binding.scale>=-1&&binding.scale<=1&&binding.scale!=0&&std::isfinite(binding.threshold)&&binding.threshold>=0&&binding.threshold<=1,"Input binding settings");require(actions.at(binding.action)==InputActionKind::Axis||binding.scale==1,"Button binding scale must be one");require(binding.consumption==InputConsumption::None||binding.consumption==InputConsumption::Trigger||binding.consumption==InputConsumption::All,"Input consumption policy");std::set<InputControl> unique;for(auto control:binding.controls){input_control_name(control);require(unique.insert(control).second,"Repeated control in chord");controls.insert(control);}if(binding.controls.size()>1){for(auto control:binding.controls)if(control.device==InputDevice::Gamepad)for(auto other:binding.controls)require(other.device!=InputDevice::Gamepad||other.slot==control.slot,"Chord spans gamepad seats");}}
    require(controls.size()<=128,"Input control inventory limit");
}
InputProfile decode_input_source(std::string_view bytes){
    using Json=nlohmann::json;require(bytes.size()<=65536,"Input source byte limit");unsigned work{};std::vector<std::set<std::string>> keys_seen;
    auto data=Json::parse(bytes,[&](int depth,Json::parse_event_t e,Json& v){require(depth<=8&&++work<=8192,"Input source work limit");if(e==Json::parse_event_t::object_start)keys_seen.emplace_back();if(e==Json::parse_event_t::key)require(keys_seen.back().insert(v.get<std::string>()).second,"Duplicate input field");if(e==Json::parse_event_t::object_end)keys_seen.pop_back();return true;});
    require(data.is_object()&&data.size()==5&&data.at("schema")==1&&data.at("kind")=="input","Input source schema");InputProfile profile;profile.id=AssetId::parse(data.at("asset").get<std::string>());
    auto integer=[](const Json& v,std::uint64_t maximum){require(v.is_number_unsigned()&&v.get<std::uint64_t>()<=maximum,"Input unsigned integer range");return v.get<std::uint64_t>();};
    require(data.at("actions").is_array()&&data.at("actions").size()<=64&&data.at("bindings").is_array()&&data.at("bindings").size()<=128,"Input source array limit");
    for(const auto& source:data.at("actions")){require(source.is_object()&&source.size()==6,"Input action schema");auto kind=source.at("type").get<std::string>();require(kind=="button"||kind=="axis","Input source action kind");profile.actions.push_back({static_cast<std::uint32_t>(integer(source.at("id"),UINT32_MAX)),source.at("name").get<std::string>(),kind=="button"?InputActionKind::Button:InputActionKind::Axis,integer(source.at("hold_ms"),10000)*1000,integer(source.at("tap_ms"),10000)*1000,source.at("deadzone").get<float>()});}
    for(const auto& source:data.at("bindings")){require(source.is_object()&&source.size()==7&&source.at("controls").is_array()&&source.at("controls").size()<=4,"Input binding schema");InputBinding binding;binding.id=static_cast<std::uint32_t>(integer(source.at("id"),UINT32_MAX));binding.action=static_cast<std::uint32_t>(integer(source.at("action"),UINT32_MAX));require(source.at("priority").is_number_integer()&&(source.at("priority").is_number_unsigned()?source.at("priority").get<std::uint64_t>()<=255:(source.at("priority").get<std::int64_t>()>=-255&&source.at("priority").get<std::int64_t>()<=255)),"Input priority integer");binding.priority=source.at("priority").get<int>();binding.scale=source.at("scale").get<float>();binding.threshold=source.at("threshold").get<float>();auto consume=source.at("consume").get<std::string>();require(consume=="none"||consume=="trigger"||consume=="all","Input source consumption policy");binding.consumption=consume=="all"?InputConsumption::All:consume=="trigger"?InputConsumption::Trigger:InputConsumption::None;for(const auto& control:source.at("controls"))binding.controls.push_back(input_control(control.get<std::string>()));profile.bindings.push_back(std::move(binding));}
    validate_input_profile(profile);return profile;
}
struct InputManager::Impl {
    InputProfile profile;std::map<InputControl,float> physical;std::map<std::uint32_t,InputState> states;std::map<std::uint32_t,std::uint64_t> starts;std::map<std::uint32_t,bool> suppressed;std::set<std::uint32_t> previous_bindings;std::uint64_t time{};bool enabled{};
    explicit Impl(InputProfile source):profile(std::move(source)){validate_input_profile(profile);for(const auto& action:profile.actions)states[action.id]={};std::sort(profile.actions.begin(),profile.actions.end(),[](const auto& a,const auto& b){return a.id<b.id;});for(const auto& binding:profile.bindings){suppressed[binding.id]=false;for(auto control:binding.controls)physical[control]=0;}std::sort(profile.bindings.begin(),profile.bindings.end(),[](const auto& a,const auto& b){if(a.priority!=b.priority)return a.priority>b.priority;if(a.controls.size()!=b.controls.size())return a.controls.size()>b.controls.size();return a.id<b.id;});}
    bool matches(const InputBinding& binding) const{for(std::size_t i=0;i<binding.controls.size();++i){auto value=std::abs(physical.at(binding.controls[i]));if(value==0||value<(i? .5f:binding.threshold))return false;}return true;}
    void evaluate(std::uint64_t now,bool allow,const std::set<InputControl>& cancellations,InputBatch& batch){
        std::set<InputControl> claimed;std::set<std::uint32_t> active;std::map<std::uint32_t,bool> matched;std::set<std::uint32_t> cancelled_actions;
        for(const auto& binding:profile.bindings)if(previous_bindings.contains(binding.id))for(auto control:binding.controls)if(cancellations.contains(control))cancelled_actions.insert(binding.action);
        for(const auto& binding:profile.bindings){bool match=matches(binding);matched[binding.id]=match;if(!match)suppressed[binding.id]=false;if(!allow&&match)suppressed[binding.id]=true;}
        // Consuming chords win before observational bindings, in priority/length/stable-ID order.
        if(allow)for(const auto& binding:profile.bindings)if(binding.consumption!=InputConsumption::None&&matched[binding.id]&&!suppressed[binding.id]){bool conflict=false;for(auto control:binding.controls)conflict|=claimed.contains(control);if(conflict){suppressed[binding.id]=true;cancelled_actions.insert(binding.action);continue;}active.insert(binding.id);if(binding.consumption==InputConsumption::Trigger)claimed.insert(binding.controls.front());else for(auto control:binding.controls)claimed.insert(control);}
        if(allow)for(const auto& binding:profile.bindings)if(binding.consumption==InputConsumption::None&&matched[binding.id]&&!suppressed[binding.id]){bool conflict=false;for(auto control:binding.controls)conflict|=claimed.contains(control);if(conflict){suppressed[binding.id]=true;cancelled_actions.insert(binding.action);}else active.insert(binding.id);}
        std::map<std::uint32_t,float> values;for(const auto& binding:profile.bindings)if(active.contains(binding.id)){auto kind=states.contains(binding.action)?std::find_if(profile.actions.begin(),profile.actions.end(),[&](const auto& a){return a.id==binding.action;})->kind:InputActionKind::Button;float value=kind==InputActionKind::Button?1:physical.at(binding.controls.front())*binding.scale;if(kind==InputActionKind::Button)values[binding.action]=1;else values[binding.action]+=value;}
        for(const auto& action:profile.actions){auto& state=states.at(action.id);float value=std::clamp(values[action.id],-1.f,1.f);if(std::abs(value)<=action.deadzone)value=0;bool down=value!=0;auto held=state.down?now-starts[action.id]:0;
            if(state.down&&!down){bool cancelled=!allow||cancelled_actions.contains(action.id);emit(batch,{action.id,InputEdge::Released,now,held,state.value,cancelled});if(!cancelled&&action.kind==InputActionKind::Button&&held<=action.tap_us)emit(batch,{action.id,InputEdge::Tapped,now,held,state.value,false});}
            else if(!state.down&&down){starts[action.id]=now;held=0;emit(batch,{action.id,InputEdge::Pressed,now,0,value,false});}
            state={down,value,down?held:0};
        }
        previous_bindings=std::move(active);
    }
};
InputManager::InputManager(InputProfile profile):impl_(std::make_unique<Impl>(std::move(profile))){}
InputManager::~InputManager()=default;
InputBatch InputManager::update(std::uint64_t now,std::span<const InputSample> samples,bool enabled){
    auto& old=*impl_;require(now>=old.time&&samples.size()<=256,"Input timestamp/sample limit");std::uint64_t previous=old.time;for(const auto& sample:samples){require(old.physical.contains(sample.control)&&sample.time_us>=previous&&sample.time_us<=now&&std::isfinite(sample.value)&&sample.value>=-1&&sample.value<=1,"Invalid input sample/timestamp");if(!input_axis(sample.control))require(sample.value==0||sample.value==1,"Button input must be zero or one");previous=sample.time_us;}
    auto candidate=std::make_unique<Impl>(old);InputBatch batch;
    if(!enabled)candidate->evaluate(samples.empty()?now:samples.front().time_us,false,{},batch);
    std::size_t at{};while(at<samples.size()){auto time=samples[at].time_us;std::set<InputControl> group,cancellations;do{const auto& sample=samples[at++];candidate->physical[sample.control]=sample.value;group.insert(sample.control);if(sample.cancelled)cancellations.insert(sample.control);}while(at<samples.size()&&samples[at].time_us==time&&!group.contains(samples[at].control));candidate->evaluate(time,enabled,cancellations,batch);}
    candidate->evaluate(now,enabled,{},batch);for(const auto& action:candidate->profile.actions){const auto& state=candidate->states.at(action.id);if(state.down&&state.held_us>=action.hold_us)emit(batch,{action.id,InputEdge::Hold,now,state.held_us,state.value,false});}
    candidate->time=now;candidate->enabled=enabled;impl_=std::move(candidate);return batch;
}
InputBatch InputManager::rebind(InputProfile profile,std::uint64_t now,std::span<const InputSample> physical){
    require(profile.id==impl_->profile.id&&profile.actions.size()==impl_->profile.actions.size(),"Rebinding preserves profile/action identities");for(const auto& action:profile.actions){auto old=std::find_if(impl_->profile.actions.begin(),impl_->profile.actions.end(),[&](const auto& a){return a.id==action.id;});require(old!=impl_->profile.actions.end()&&old->name==action.name&&old->kind==action.kind,"Rebinding cannot reinterpret an action identity");}
    require(now>=impl_->time,"Input rebind time reversed");auto candidate=std::make_unique<Impl>(std::move(profile));std::set<InputControl> snapshot;for(const auto& sample:physical)require(sample.time_us==now&&snapshot.insert(sample.control).second,"Rebind requires one current snapshot per control");require(snapshot.size()==candidate->physical.size(),"Rebind control snapshot incomplete");InputManager probe(candidate->profile);probe.impl_=std::move(candidate);probe.update(now,physical,false);InputBatch batch;for(const auto& [id,state]:impl_->states)if(state.down)emit(batch,{id,InputEdge::Released,now,now-impl_->starts.at(id),state.value,true});impl_=std::move(probe.impl_);return batch;
}
InputState InputManager::state(std::uint32_t action) const{auto it=impl_->states.find(action);require(it!=impl_->states.end(),"Unknown input action ID");return it->second;}
const InputProfile& InputManager::profile() const{return impl_->profile;}
std::vector<InputControl> InputManager::controls() const{std::vector<InputControl> result;for(const auto& [control,value]:impl_->physical)result.push_back(control);return result;}
}
