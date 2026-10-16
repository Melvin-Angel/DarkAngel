#pragma once
#include <darkangel/ui_document.hpp>
#include <DirectXMath.h>
#include <darkangel/world_ui.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/party_control.hpp>
namespace darkangel::editor_app {
WorldUiCamera world_ui_camera(const DirectX::XMFLOAT4X4&,const DirectX::XMFLOAT4X4&,float width,float height);
}
