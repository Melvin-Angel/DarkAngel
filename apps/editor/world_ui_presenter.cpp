#include "world_ui_presenter.hpp"
#include <darkangel/scene_client.hpp>
#include <algorithm>
#include <cstring>
#include <cmath>
namespace darkangel::editor_app {
WorldUiCamera world_ui_camera(const DirectX::XMFLOAT4X4& view,const DirectX::XMFLOAT4X4& projection,float width,float height){WorldUiCamera camera;std::memcpy(camera.view.data(),&view,sizeof(view));std::memcpy(camera.projection.data(),&projection,sizeof(projection));camera.width=width;camera.height=height;return camera;}
}
