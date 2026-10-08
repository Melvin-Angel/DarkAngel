#include <darkangel/character_motor.hpp>
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
        check((i.sequence>0||(i.x==0&&i.z==0&&!i.jump))&&i.tick>0&&i.epoch>0&&finite(i.x)&&finite(i.z)&&i.x*i.x+i.z*i.z<=1.00001&&finite(i.yaw),"Invalid motor input");
    }
    void validate_motor_state(const MotorState& s){
        check(s.epoch&&s.topology&&finite(s.yaw)&&s.coyote<=6&&s.jump_buffer<=6,"Invalid motor state header");
        for(auto a:{
            s.position,s.velocity,s.momentum,s.desired,s.achieved,s.support_local,s.support_velocity
        })check(valid(a),"Invalid motor state vector");
    }
}
