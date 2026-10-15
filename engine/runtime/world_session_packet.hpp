#pragma once
#include <darkangel/check.hpp>
#include <darkangel/world.hpp>
#include <darkangel/transport.hpp>
#include <darkangel/collision_asset.hpp>
#include "session_wire.hpp"

namespace darkangel::session_detail {

enum class Kind : std::uint64_t {
    Hello = 1,
    ContentReady,
    Baseline,
    Ack,
    Ready,
    Resync,
    MotorCommand,
    MotorSnapshot,
    CollisionSnapshot,
    CollisionAck,
    AbilityCorrection,
    AbilityCorrectionAck,
    AbilityCommand,
    AbilityReceipt,
    AbilityPublic,
    EffectOwner,
    EffectPublic,
    Projectiles,
    MotorBatch,
    PublicIndex,
    PublicBatch,
    MotorRemote,
    Interact,
    InteractReceipt,
    OwnerService
};
// Motor states packed into one state packet; three fit the packet limit.
constexpr std::size_t motor_batch_states = 3;
// What an observer uses of a character it does not own is 104 bytes (128 on a moving support); nine fit a packet.
constexpr std::size_t motor_remote_states = 9;
constexpr std::size_t packet_limit = 1000, header_size = 56, object_size = 104, per_chunk = (packet_limit - header_size) / object_size;
inline Writer header(Kind kind, const SessionHandshake& h, std::uint64_t revision = 0, std::uint64_t index = 0, std::uint64_t chunks = 0,
                     std::uint64_t total = 0) {
    Writer w;
    w.u64(0x3253454144);
    w.u64(static_cast<std::uint64_t>(kind));
    w.u64(h.session_epoch);
    w.u64(revision);
    w.u64(index);
    w.u64(chunks);
    w.u64(total);
    return w;
}
inline void write_object(Writer& w, const ObjectData& o) {
    w.u64(o.network.value);
    w.u64(o.id.high);
    w.u64(o.id.low);
    for (double n : {o.transform.yaw, o.transform.x, o.transform.y, o.transform.z, o.transform.pitch, o.transform.roll, o.transform.scale,
                     o.health.maximum, o.health.current})
        w.number(n);
    w.u64(0);
}
inline ObjectData read_object(Reader& r) {
    ObjectData o;
    o.network.value = r.u64();
    o.id = {r.u64(), r.u64()};
    o.transform = {r.number(), r.number(), r.number(), r.number(), r.number(), r.number(), r.number()};
    o.health = {r.number(), r.number()};
    require(r.u64() == 0, "Unsupported object wire schema");
    return o;
}

constexpr unsigned collision_chunk_items = 4;
inline std::size_t collision_items(const CollisionStreamFrame& frame) {
    return frame.boxes.size() + frame.actors.size() + frame.meshes.size();
}
inline void write_collision_item(Writer& w, const CollisionStreamFrame& frame, std::size_t item) {
    if (item < frame.boxes.size()) {
        const auto& b = frame.boxes[item];
        w.u64(1);
        w.u64(b.id);
        for (auto value : {b.center, b.half, b.velocity, b.angular})
            write_vector(w, value);
        w.number(b.yaw);
        w.number(b.roll);
        w.u64(unsigned(b.moving) | unsigned(b.dynamic) * 2 | unsigned(b.sensor) * 4);
        w.number(b.mass);
        for (auto n : b.rotation)
            w.number(n);
    } else if ((item -= frame.boxes.size()) < frame.actors.size()) {
        const auto& a = frame.actors[item];
        w.u64(2);
        w.u64(a.id);
        w.u64(a.epoch);
        write_vector(w, a.foot);
        write_vector(w, a.velocity);
        w.number(a.yaw);
        w.u64(a.crouched);
    } else {
        const auto& mesh = frame.meshes[item - frame.actors.size()];
        w.u64(3);
        w.u64(mesh.id);
        for (auto n : mesh.runtime.bytes)
            w.bytes.push_back(static_cast<std::byte>(n));
        for (char c : mesh.signature)
            w.bytes.push_back(static_cast<std::byte>(c));
    }
}
inline void read_collision_item(Reader& r, CollisionStreamFrame& frame) {
    auto kind = r.u64();
    if (kind == 1) {
        CollisionBox b;
        b.id = r.u64();
        b.center = read_vector(r);
        b.half = read_vector(r);
        b.velocity = read_vector(r);
        b.angular = read_vector(r);
        b.yaw = r.number();
        b.roll = r.number();
        auto flags = r.u64();
        require(flags <= 7, "Collision wire flags");
        b.moving = flags & 1;
        b.dynamic = flags & 2;
        b.sensor = flags & 4;
        b.mass = r.number();
        for (auto& n : b.rotation)
            n = r.number();
        frame.boxes.push_back(b);
    } else if (kind == 2) {
        CollisionActor a;
        a.id = r.u64();
        a.epoch = r.u64();
        a.foot = read_vector(r);
        a.velocity = read_vector(r);
        a.yaw = r.number();
        auto stance = r.u64();
        require(stance <= 1, "Collision wire stance");
        a.crouched = stance != 0;
        frame.actors.push_back(a);
    } else if (kind == 3) {
        CollisionGeometryReference mesh;
        mesh.id = r.u64();
        require(r.bytes.size() - r.at >= 80, "Collision wire geometry reference");
        for (auto& n : mesh.runtime.bytes)
            n = std::to_integer<unsigned char>(r.bytes[r.at++]);
        for (unsigned n = 0; n < 64; ++n)
            mesh.signature.push_back(static_cast<char>(std::to_integer<unsigned char>(r.bytes[r.at++])));
        frame.meshes.push_back(std::move(mesh));
    } else
        throw std::runtime_error("Collision wire item kind");
}

} // namespace darkangel::session_detail
