#pragma once
// Rules both halves of a scene session apply to their own state: authority to what it simulates, a client to what it
// predicts. Internal to engine/runtime.
#include <darkangel/scene_definitions.hpp>
#include <darkangel/world_session.hpp>
#include <darkangel/targeting.hpp>
#include <cmath>
#include <stdexcept>

namespace darkangel::scene_detail {
inline void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
inline SessionLimits session_limits(SceneTiming timing){SessionLimits out;out.wall_clock_timeouts=timing==SceneTiming::Independent;return out;}
// The frozen local ability behind an owner snapshot's running action.
inline const AbilityDefinition& definition(const CharacterSceneCombat* combat,const AbilityOwnerSnapshot& snapshot){
    require(combat&&snapshot.active&&snapshot.action,"Active combat correction required");for(const auto& ability:combat->abilities)if(ability->id==snapshot.ability){require(ability->generation==snapshot.ability_generation&&ability->action->id==snapshot.action_definition&&ability->action->generation==snapshot.action->generation,"Combat correction frozen generation mismatch");return *ability;}throw std::runtime_error("Combat correction missing local ability");
}
// Facing rule for abilities authored with face_movement: on the activation tick the
// actor turns to its world-space movement input (or keeps its yaw without input); afterwards
// the yaw stays where the motor has it until the action ends. Input yaw then resumes.
inline void face(MotorInput& command,double motor_yaw,bool started){
    if(!started){command.yaw=motor_yaw;return;}
    if(std::hypot(command.x,command.z)>.1)command.yaw=std::atan2(command.x,command.z);
}
// Movement restriction from statuses and death: the motor's existing lock request plus no jump.
inline void restrict(MotorInput& command,MotionRequest& request){request.lock=true;request.warp={};command.jump=false;}
// The targeting module proposes the point; the motor gates and applies the warp when it steps.
inline std::optional<std::uint64_t> lock(MotionRequest& request,const MotorState& state,const MotorInput& command,std::span<const TargetCandidate> candidates){
    if(!request.warp.active)return std::nullopt;if(auto chosen=select_soft_lock(request.warp,state,command,candidates)){request.warp.point=chosen->position;return chosen->id;}request.warp={};return std::nullopt;
}
}
