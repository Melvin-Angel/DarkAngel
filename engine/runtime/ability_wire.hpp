#pragma once
#include <darkangel/world_session.hpp>
namespace darkangel::session_detail {
std::vector<std::byte> encode_ability_correction(const AbilityCorrection&);
AbilityCorrection decode_ability_correction(std::span<const std::byte>);
std::vector<std::byte> encode_ability_public(const AbilityPublicFrame&);
AbilityPublicFrame decode_ability_public(std::span<const std::byte>);
}
