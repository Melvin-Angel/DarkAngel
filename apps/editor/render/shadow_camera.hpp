#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace darkangel::editor_app::render {
struct ShadowCamera { DirectX::XMMATRIX view, projection; };
// Fit the receiver slice, including space toward the light for off-screen casters.
// A fixed light eye plus a cascade-sized far plane clips nearby casters out.
inline ShadowCamera shadow_camera(DirectX::FXMMATRIX camera_view, DirectX::CXMMATRIX camera_projection,
                                  DirectX::FXMVECTOR direction, float near_depth, float far_depth,
                                  float caster_distance, unsigned resolution) {
    using namespace DirectX;
    const auto inverse_projection = XMMatrixInverse(nullptr, camera_projection);
    const auto inverse_view = XMMatrixInverse(nullptr, camera_view);
    std::array<XMVECTOR, 8> corners;
    auto center = XMVectorZero();
    unsigned index = 0;
    for (float depth : {near_depth, far_depth})
        for (float y : {-1.f, 1.f})
            for (float x : {-1.f, 1.f}) {
                const auto ray = XMVector3TransformCoord(XMVectorSet(x, y, .5f, 1), inverse_projection);
                const auto corner = XMVector3TransformCoord(XMVectorScale(ray, depth / -XMVectorGetZ(ray)), inverse_view);
                corners[index++] = corner;
                center = XMVectorAdd(center, corner);
            }
    center = XMVectorScale(center, 1.f / 8);
    float radius = 1;
    for (const auto corner : corners)
        radius = std::max(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corner, center))));
    radius = std::ceil(radius * 16) / 16;
    const auto up = std::abs(XMVectorGetY(direction)) > .99f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
    const auto light_view = XMMatrixLookToRH(XMVectorZero(), direction, up);
    const auto light_center = XMVector3TransformCoord(center, light_view);
    const float texel = 2 * radius / std::max(1u, resolution);
    const float x = std::floor(XMVectorGetX(light_center) / texel + .5f) * texel;
    const float y = std::floor(XMVectorGetY(light_center) / texel + .5f) * texel;
    const float depth = -XMVectorGetZ(light_center);
    return {light_view, XMMatrixOrthographicOffCenterRH(x - radius - texel, x + radius + texel,
        y - radius - texel, y + radius + texel, depth - radius - caster_distance, depth + radius)};
}
}
