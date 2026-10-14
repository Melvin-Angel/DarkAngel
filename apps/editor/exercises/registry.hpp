#pragma once
#include <span>
#include <string_view>

namespace darkangel::editor_app {
struct EditorOptions;
namespace exercises {
// Compatibility registry: each legacy flag enables its existing scenario and prerequisite options.
std::span<const std::string_view> option_names();
bool consume_option(std::string_view name, int& argument, int argc, char** argv, EditorOptions&);
}
}
