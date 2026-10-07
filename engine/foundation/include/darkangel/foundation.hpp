#pragma once
#include <string_view>
namespace darkangel {
std::string_view build_identity() noexcept;
void diagnostic(std::string_view message);
}
