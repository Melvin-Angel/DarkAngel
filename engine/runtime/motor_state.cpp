#include <darkangel/character_motor.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace darkangel {
    namespace {
        void check(bool b,const char* s){
            if(!b)throw std::runtime_error(s);
        }
        bool finite(double n){
            return std::isfinite(n)&&std::abs(n)<=100000;
        }
        bool valid(MotorVec v){
            return finite(v.x)&&finite(v.y)&&finite(v.z);
        }
    }
    void validate_motor_input(const MotorInput& i){
        check((i.sequence>0||(i.x==0&&i.z==0&&!i.jump))&&i.tick>0&&i.epoch>0&&finite(i.x)&&finite(i.z)&&i.x*i.x+i.z*i.z<=1.00001&&finite(i.yaw)&&finite(i.pitch)&&std::abs(i.pitch)<=1.5,"Invalid motor input");
    }
    std::optional<MotionWarpResult> resolve_motion_warp(const MotionWarp& warp,const MotorState& state,const MotorInput& input){
        if(!warp.active)return std::nullopt;
        check(valid(warp.point)&&finite(warp.range)&&warp.range>0&&warp.range<=30&&finite(warp.cone)&&warp.cone>0&&warp.cone<=3.1415927&&finite(warp.stop)&&warp.stop>=0&&warp.stop<=30&&finite(warp.step)&&warp.step>=0&&warp.step<=.5,"Motion warp bounds");
        const double dx=warp.point.x-state.position.x,dz=warp.point.z-state.position.z,distance=std::hypot(dx,dz);
        if(distance<1e-6||distance>warp.range||std::abs(warp.point.y-state.position.y)>warp.range)return std::nullopt;
        const double toward=std::atan2(dx,dz),centre=!warp.fresh?state.yaw:std::hypot(input.x,input.z)>.1?std::atan2(input.x,input.z):input.yaw;
        // Later ticks only keep tracking what the actor already faces; they never acquire a
        // target the fresh tick turned down.
        const double cone=warp.fresh?warp.cone:std::min(warp.cone,.35);
        if(std::abs(std::remainder(toward-centre,6.283185307179586))>cone)return std::nullopt;
        const double travel=std::clamp(distance-warp.stop,0.,warp.step);
        return MotionWarpResult{toward,{dx/distance*travel,0,dz/distance*travel}};
    }
    void validate_motor_state(const MotorState& s){
        check(s.epoch&&s.topology&&finite(s.yaw)&&finite(s.pitch)&&std::abs(s.pitch)<=1.5&&s.coyote<=motor_timer_tick_limit&&s.jump_buffer<=motor_timer_tick_limit,"Invalid motor state header");
        for(auto a:{
            s.position,s.velocity,s.momentum,s.desired,s.achieved,s.support_local,s.support_velocity
        })check(valid(a),"Invalid motor state vector");
    }
    void validate_motor_parameters(const MotorParameters& p){
        auto within=[](double value,MotorParameterRange range){return std::isfinite(value)&&value>=range.minimum&&value<=range.maximum;};
        check(within(p.walk_speed_mps,motor_walk_speed_range),"Movement walk speed must be 0.5 to 12 m/s");
        check(within(p.crouch_speed_mps,motor_crouch_speed_range),"Movement crouch speed must be 0.25 to 12 m/s");
        check(within(p.acceleration_mps2,motor_acceleration_range),"Movement acceleration must be 1 to 120 m/s2");
        check(within(p.jump_velocity_mps,motor_jump_velocity_range),"Movement jump velocity must be 0 to 20 m/s");
        check(within(p.gravity_mps2,motor_gravity_range),"Movement gravity must be 1 to 60 m/s2");
        check(p.coyote_ticks<=motor_timer_tick_limit&&p.jump_buffer_ticks<=motor_timer_tick_limit,"Movement coyote and jump buffer must be 0 to 30 ticks");
    }
}
