#pragma once
#include <darkangel/effect_replication.hpp>
namespace darkangel::session_detail {
std::vector<std::byte> encode_effect_frame(const EffectFrame&);
EffectFrame decode_effect_frame(std::span<const std::byte>);
}
