#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
namespace darkangel {
struct AssetId {
    std::array<std::uint8_t,16> bytes{};
    auto operator<=>(const AssetId&) const = default;
    static AssetId random();
    static AssetId parse(std::string_view);
    std::string text() const;
};
}
