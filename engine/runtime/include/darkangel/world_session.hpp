#pragma once
#include <darkangel/transport.hpp>
#include <darkangel/character_motor.hpp>
#include <darkangel/world.hpp>
#include <map>
#include <memory>
namespace darkangel {
enum class SessionRole { Server, Client };
enum class SessionReadiness { Handshake, ContentReady, Bootstrap, CatchUp, Ready };
struct SessionLimits {std::size_t objects{128},peers{4},packets_per_tick{16};unsigned timeout_ticks{600};};
// All providers carry the same bounded, versioned protocol. Only the server
// creates identities and publishes state. Client worlds are read-only views.
class WorldSession {
public:
    WorldSession(SessionRole,SessionHandshake,SessionLimits={});
    ~WorldSession();
    WorldSession(const WorldSession&)=delete;
    WorldSession& operator=(const WorldSession&)=delete;
    void attach(Transport&,ConnectionHandle);
    void detach(ConnectionHandle);
    void tick();
    std::uint64_t create(ObjectData);
    void move(std::uint64_t,const Transform&);
    void destroy(std::uint64_t);
    // Explicit server ownership; movement commands never grant authority.
    void own_motor(std::uint64_t,ConnectionHandle);
    bool submit_motor(std::uint64_t,const MotorInput&);
    bool motor_input_ready(std::uint64_t,std::uint64_t) const;
    MotorInput consume_motor(std::uint64_t,std::uint64_t simulation_tick,std::uint64_t epoch);
    void publish_motor(std::uint64_t,const MotorState&);
    const std::map<std::uint64_t,MotorState>& motors() const;
    const World& world() const;
    const std::map<std::uint64_t,ObjectData>& objects() const;
    std::uint64_t revision() const;
    SessionReadiness readiness(ConnectionHandle) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
