#pragma once
#include <array>
#include <algorithm>

namespace darkangel::editor_app {
struct RenderSize { unsigned width, height; };
struct GameResolution { const char* label; RenderSize size; };
inline constexpr std::array game_resolutions{
    GameResolution{"Match viewport", {0, 0}},
    GameResolution{"1280 x 720 (720p)", {1280, 720}},
    GameResolution{"1366 x 768 (768p)", {1366, 768}},
    GameResolution{"1600 x 900 (900p)", {1600, 900}},
    GameResolution{"1920 x 1080 (1080p)", {1920, 1080}}};
// Display and input remain in panel coordinates. Only GPU targets use this size.
inline RenderSize viewport_render_size(bool game, unsigned preset, float width, float height) {
    if (game && preset > 0 && preset < game_resolutions.size()) return game_resolutions[preset].size;
    return {static_cast<unsigned>(std::max(1.f, width)), static_cast<unsigned>(std::max(1.f, height))};
}
}
