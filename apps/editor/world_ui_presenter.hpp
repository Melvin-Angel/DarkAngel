#pragma once
#include <darkangel/hud.hpp>
#include <DirectXMath.h>
#include <darkangel/world_ui.hpp>
#include <darkangel/character_scene.hpp>
#include <darkangel/party_control.hpp>
namespace darkangel::editor_app {
WorldUiCamera world_ui_camera(const DirectX::XMFLOAT4X4&,const DirectX::XMFLOAT4X4&,float width,float height);
std::vector<HudWorldMarker> character_world_ui(const CharacterSceneSession&,const SceneClient&,const WorldUiCamera&,std::span<const PartyMember> roster={});
}
