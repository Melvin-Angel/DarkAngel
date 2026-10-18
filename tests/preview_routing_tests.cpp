#include "../apps/editor/render/preview_routing.hpp"
#include <iostream>

using darkangel::editor_app::render::PreviewRouting;

int main() {
    if (!PreviewRouting::isolated_content(true, false, false, false) ||
        !PreviewRouting::isolated_content(false, true, false, false) ||
        !PreviewRouting::isolated_content(false, false, true, false))
        return 1;
    if (PreviewRouting::isolated_content(true, true, true, true) ||
        PreviewRouting::isolated_content(false, false, false, false))
        return 2;
    if (!PreviewRouting::scene_camera_is_primary(true, false) ||
        PreviewRouting::scene_camera_is_primary(false, false) ||
        PreviewRouting::scene_camera_is_primary(true, true))
        return 3;
    if (!PreviewRouting::gameplay_camera_is_primary(true, true) ||
        PreviewRouting::gameplay_camera_is_primary(false, true) ||
        PreviewRouting::gameplay_camera_is_primary(true, false))
        return 4;
    std::cout << "Preview routing: isolated content and gameplay camera ownership pass\n";
    return 0;
}
