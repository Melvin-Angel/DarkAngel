#include "play_hud.hpp"
#include <darkangel/check.hpp>

namespace darkangel::editor_app {
using namespace Diligent;
HudResources::HudResources(IRenderDevice* device)
    : hud_renderer(
          [this, device](const unsigned char* rgba, int width, int height) {
              TextureDesc desc;
              desc.Name = "HUD font atlas";
              desc.Type = RESOURCE_DIM_TEX_2D;
              desc.Width = static_cast<Uint32>(width);
              desc.Height = static_cast<Uint32>(height);
              desc.MipLevels = 1;
              desc.Format = TEX_FORMAT_RGBA8_UNORM;
              desc.Usage = USAGE_IMMUTABLE;
              desc.BindFlags = BIND_SHADER_RESOURCE;
              TextureSubResData level;
              level.pData = rgba;
              level.Stride = static_cast<Uint64>(width) * 4;
              TextureData data;
              data.pSubResources = &level;
              data.NumSubresources = 1;
              RefCntAutoPtr<ITexture> texture;
              device->CreateTexture(desc, &data, &texture);
              require(texture, "HUD texture creation failed");
              auto id = reinterpret_cast<ImTextureID>(texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
              hud_textures.emplace(id, std::move(texture));
              return id;
          },
          [this](ImTextureID id) { hud_textures.erase(id); }) {}
} // namespace darkangel::editor_app
