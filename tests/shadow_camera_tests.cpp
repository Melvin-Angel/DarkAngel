#include "render/shadow_camera.hpp"
#include <iostream>
#include <stdexcept>
using namespace DirectX;
using darkangel::editor_app::render::shadow_camera;
int main() { try {
    for (float offset : {0.f, 200.f, -400.f}) {
        const auto view = XMMatrixLookAtRH(XMVectorSet(offset, 3, 5, 1), XMVectorSet(offset, 1, 0, 1), XMVectorSet(0,1,0,0));
        const auto projection = XMMatrixPerspectiveFovRH(1.f, 1.8f, .1f, 1000.f);
        for (auto direction : {XMVector3Normalize(XMVectorSet(.4f,-.8f,.3f,0)), XMVectorSet(0,-1,0,0)}) {
            const auto shadow = shadow_camera(view, projection, direction, .1f, 30.f, 96.f, 1024);
            // Entire receiver slice and casters displaced toward the light must survive clipping.
            for (float depth : {.1f, 5.f, 30.f})
                for (float y : {-1.f, 0.f, 1.f})
                    for (float x : {-1.f, 0.f, 1.f}) {
                        const auto ray = XMVector3TransformCoord(XMVectorSet(x,y,.5f,1), XMMatrixInverse(nullptr,projection));
                        const auto receiver = XMVector3TransformCoord(XMVectorScale(ray,depth/-XMVectorGetZ(ray)),XMMatrixInverse(nullptr,view));
                        for (float towards_light : {0.f, 50.f}) {
                            const auto point = XMVectorSubtract(receiver, XMVectorScale(direction,towards_light));
                            const auto clip = XMVector3TransformCoord(point, shadow.view * shadow.projection);
                            if (!std::isfinite(XMVectorGetX(clip)) || std::abs(XMVectorGetX(clip))>1.001f ||
                                std::abs(XMVectorGetY(clip))>1.001f || XMVectorGetZ(clip)<-.001f || XMVectorGetZ(clip)>1.001f)
                                throw std::runtime_error("Cascade clips receiver or character caster");
                        }
                    }
        }
    }
    std::cout << "Shadow camera: receiver slices, translated cameras, offscreen casters and vertical lights pass\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
