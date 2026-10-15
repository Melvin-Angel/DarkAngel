#include <darkangel/session_frames.hpp>
#include <bit>
namespace darkangel {
std::uint64_t public_ability_content(const AbilityPublicSnapshot& snapshot) {
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&](std::uint64_t value) {
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= 1099511628211ull;
        }
    };
    mix(snapshot.loadout);
    for (auto mask : snapshot.equipped_masks)
        for (auto byte : mask.bytes)
            mix(byte);
    mix(snapshot.attributes.size());
    for (const auto& attribute : snapshot.attributes) {
        mix(attribute.id);
        mix(std::bit_cast<std::uint64_t>(attribute.value));
    }
    mix(snapshot.tags.values.size());
    for (auto tag : snapshot.tags.values)
        mix(std::uint64_t(tag));
    mix(snapshot.active ? snapshot.active->activation : 0);
    if (snapshot.action) {
        mix(snapshot.action->activation);
        mix(snapshot.action->clock - std::uint64_t(snapshot.action->rate) * snapshot.action->tick);
        mix(snapshot.action->rate);
        mix(std::uint64_t(snapshot.action->phase));
    }
    return hash;
}
AbilityPublicSnapshot public_ability_at(const AbilityPublicSnapshot& snapshot, std::uint64_t tick) {
    auto result = snapshot;
    if (tick <= result.tick)
        return result;
    if (result.action) {
        result.action->clock += std::uint64_t(result.action->rate) * (tick - result.tick);
        result.action->tick = tick;
    }
    result.tick = tick;
    return result;
}
} // namespace darkangel
