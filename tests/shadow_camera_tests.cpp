#include "render/shadow_camera.hpp"
#include "render/static_index_batches.hpp"
#include <iostream>
#include <stdexcept>
using namespace DirectX;
using darkangel::editor_app::render::shadow_camera;
using darkangel::editor_app::render::shadow_caster_visible;
int main() { try {
    unsigned broad_box_rejections = 0;
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
                            const auto world = XMMatrixTranslationFromVector(point);
                            if (!shadow_caster_visible(shadow, world, {0,0,0}, {0,0,0}))
                                throw std::runtime_error("Caster hull discards a receiver or offscreen shadow caster");
                        }
                    }
            // The optimized volume must reject some empty corners of the
            // unchanged broad projection, rather than simply rendering it all.
            const auto inverse_shadow = XMMatrixInverse(nullptr, shadow.view * shadow.projection);
            for (float x : {-.95f, .95f}) for (float y : {-.95f, .95f}) for (float z : {.2f, .8f}) {
                const auto point = XMVector3TransformCoord(XMVectorSet(x,y,z,1), inverse_shadow);
                if (!shadow_caster_visible(shadow, XMMatrixTranslationFromVector(point), {0,0,0}, {.01f,.01f,.01f}))
                    ++broad_box_rejections;
            }
        }
    }
    if (!broad_box_rejections) throw std::runtime_error("Caster hull never tightens broad shadow visibility");
    using namespace darkangel::editor_app::render;
    const std::array<StaticIndexRange, 7> input{{{0,3,0,false,false},{3,6,1,false,false},{9,3,2,true,false},
        {12,3,3,false,true},{15,3,4,false,false},{21,3,5,false,false},{24,3,6,false,false}}};
    const auto ranges = shadow_index_batches(input);
    if (ranges.size() != 5 || ranges[0].count != 9 || ranges[1].part != 2 || !ranges[1].cutout ||
        !ranges[2].double_sided || ranges[3].first != 15 || ranges[4].first != 21 || ranges[4].count != 6)
        throw std::runtime_error("Shadow batching loses alpha tests, sidedness or excluded-index gaps");
    std::cout << "Shadow camera: receiver slices, translated cameras, offscreen casters and vertical lights pass\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
