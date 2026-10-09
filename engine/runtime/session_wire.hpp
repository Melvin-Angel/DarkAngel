#pragma once
#include <darkangel/character_motor.hpp>
#include <bit>
#include <span>
#include <stdexcept>
namespace darkangel::session_detail {
inline void wire_require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Writer {
    std::vector<std::byte> bytes;
    void u64(std::uint64_t n){for(unsigned i=0;i<8;++i)bytes.push_back(static_cast<std::byte>((n>>(i*8))&255));}
    void number(double n){u64(std::bit_cast<std::uint64_t>(n));}
};
struct Reader {
    std::span<const std::byte> bytes;std::size_t at{};
    std::uint64_t u64(){wire_require(bytes.size()-at>=8,"Truncated session packet");std::uint64_t n{};for(unsigned i=0;i<8;++i)n|=std::uint64_t(std::to_integer<unsigned>(bytes[at++]))<<(i*8);return n;}
    double number(){return std::bit_cast<double>(u64());}
    void end(){wire_require(at==bytes.size(),"Trailing session fields");}
};
inline void write_vector(Writer& w,MotorVec a){w.number(a.x);w.number(a.y);w.number(a.z);}
inline MotorVec read_vector(Reader& r){return {r.number(),r.number(),r.number()};}
inline void write_motor(Writer& w,const MotorState& s){for(auto n:{s.tick,s.sequence,s.epoch,s.topology,s.support,s.action})w.u64(n);for(auto a:{s.position,s.velocity,s.momentum,s.desired,s.achieved,s.support_local,s.support_velocity})write_vector(w,a);w.number(s.yaw);w.u64(s.coyote);w.u64(s.jump_buffer);w.u64(s.grounded?1:0);w.u64(s.crouched?1:0);}
inline MotorState read_motor(Reader& r){MotorState s;s.tick=r.u64();s.sequence=r.u64();s.epoch=r.u64();s.topology=r.u64();s.support=r.u64();s.action=r.u64();s.position=read_vector(r);s.velocity=read_vector(r);s.momentum=read_vector(r);s.desired=read_vector(r);s.achieved=read_vector(r);s.support_local=read_vector(r);s.support_velocity=read_vector(r);s.yaw=r.number();auto c=r.u64(),j=r.u64(),g=r.u64(),low=r.u64();wire_require(c<=6&&j<=6&&g<=1&&low<=1,"Invalid motor flags/timers");s.coyote=static_cast<unsigned>(c);s.jump_buffer=static_cast<unsigned>(j);s.grounded=g!=0;s.crouched=low!=0;validate_motor_state(s);return s;}

}
