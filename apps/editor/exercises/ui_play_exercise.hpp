#pragma once
#include "exercise.hpp"
#include <memory>
namespace darkangel::editor_app {class Controller;class Shell;class PlayController;
namespace exercises {std::unique_ptr<Exercise> make_ui_play(bool,Controller&,Shell&,PlayController&);}}
