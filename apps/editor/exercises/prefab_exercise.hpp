#pragma once
#include "exercise.hpp"
#include <memory>

namespace darkangel::editor_app {
class Controller;
class Shell;
namespace exercises {
std::unique_ptr<Exercise> make_prefabs(bool enabled, Controller&, Shell&);
}
}
