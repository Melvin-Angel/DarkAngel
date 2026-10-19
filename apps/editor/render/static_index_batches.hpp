#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace darkangel::editor_app::render {
struct StaticIndexRange {
    std::uint32_t first{}, count{};
    std::size_t part{};
    bool cutout{}, double_sided{};
};
// Opaque shadow depth ignores material identity. Preserve alpha tests, culling
// mode and gaps (including excluded transparent surfaces) when joining ranges.
inline std::vector<StaticIndexRange> shadow_index_batches(std::span<const StaticIndexRange> input) {
    std::vector<StaticIndexRange> result;
    for (const auto& range : input) {
        if (!range.count) continue;
        if (!result.empty()) {
            auto& previous = result.back();
            if (!previous.cutout && !range.cutout && previous.double_sided == range.double_sided &&
                std::uint64_t(previous.first) + previous.count == range.first &&
                std::uint64_t(previous.count) + range.count <= UINT32_MAX) {
                previous.count += range.count;
                continue;
            }
        }
        result.push_back(range);
    }
    return result;
}
}
