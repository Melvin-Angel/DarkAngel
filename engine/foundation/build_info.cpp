#include <darkangel/foundation.hpp>
#include <darkangel/build_info.hpp>
#include <iostream>
namespace darkangel {
std::string_view build_identity() noexcept { return "DarkAngel " DAE_VERSION " revision=" DAE_REVISION; }
void diagnostic(std::string_view message) { std::clog << "[DarkAngel] " << message << '\n'; }
}
