#pragma once
#include <darkangel/action.hpp>
#include <cmath>
namespace darkangel::editor_app {
// Timeline pixels are converted to fixed action units, never truncated to ticks.
inline long long composer_drag_units(double pixels,double span,double ticks){
    return span>0&&std::isfinite(pixels)&&std::isfinite(span)&&std::isfinite(ticks)?std::llround(pixels/span*ticks*action_tick_units):0;
}
}
