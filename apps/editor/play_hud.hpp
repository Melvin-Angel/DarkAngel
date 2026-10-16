#pragma once
#include "hud_overlay.hpp"
#include <darkangel/player_ui.hpp>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>
#include <Texture.h>
#include <map>
#include <memory>

namespace darkangel::editor_app {
// The runtime HUD's resources: the game's RmlUi document, the render interface that puts its geometry into an ImGui draw
// list, and the font atlases it uploads as device textures. PlayController draws it; it is made before PlayController.
struct HudResources {
    explicit HudResources(Diligent::IRenderDevice* device);
    std::map<ImTextureID, Diligent::RefCntAutoPtr<Diligent::ITexture>> hud_textures;
    ImGuiHudRenderer hud_renderer;
    PlayerUi* hud{};
    bool hud_failed = false, hud_second_drawn = false;
};
} // namespace darkangel::editor_app
