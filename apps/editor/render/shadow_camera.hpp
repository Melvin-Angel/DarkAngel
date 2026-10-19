#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace darkangel::editor_app::render {
struct ShadowCamera {
    DirectX::XMMATRIX view, projection;
    std::vector<DirectX::XMFLOAT4> caster_planes;
    float caster_padding{};
};
// Conservative plane test for a transformed local bounding box. Animated
// meshes need animation bounds and deliberately do not use this rest-box test.
inline bool shadow_caster_visible(const ShadowCamera& camera, DirectX::FXMMATRIX world,
                                  const std::array<float, 3>& center, const std::array<float, 3>& half) {
    using namespace DirectX;
    const auto position = XMVector3TransformCoord(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(center.data())), world);
    for (const auto& value : camera.caster_planes) {
        const auto plane = XMLoadFloat4(&value);
        const float extent = std::abs(XMVectorGetX(XMVector3Dot(plane, world.r[0]))) * half[0] +
            std::abs(XMVectorGetX(XMVector3Dot(plane, world.r[1]))) * half[1] +
            std::abs(XMVectorGetX(XMVector3Dot(plane, world.r[2]))) * half[2];
        if (XMVectorGetX(XMPlaneDotCoord(plane, position)) < -extent - camera.caster_padding) return false;
    }
    return true;
}
// Fit the receiver slice, including space toward the light for off-screen casters.
// A fixed light eye plus a cascade-sized far plane clips nearby casters out.
inline ShadowCamera shadow_camera(DirectX::FXMMATRIX camera_view, DirectX::CXMMATRIX camera_projection,
                                  DirectX::FXMVECTOR direction, float near_depth, float far_depth,
                                  float caster_distance, unsigned resolution, bool build_caster_volume = true) {
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
    ShadowCamera result{light_view, XMMatrixOrthographicOffCenterRH(x - radius - texel, x + radius + texel,
        y - radius - texel, y + radius + texel, depth - radius - caster_distance, depth + radius)};
    if (!build_caster_volume) return result;
    // Hull of the receiver slice extruded all the way to the existing shadow
    // near plane. This retains every caster admitted by the projection that
    // can project onto a visible receiver, including off-screen casters.
    std::array<XMVECTOR, 16> hull;
    const auto ray_direction = XMVector3Normalize(direction);
    const float near_plane = depth - radius - caster_distance;
    for (unsigned i = 0; i < corners.size(); ++i) {
        hull[i] = corners[i];
        const float corner_depth = -XMVectorGetZ(XMVector3TransformCoord(corners[i], light_view));
        hull[i + 8] = XMVectorSubtract(corners[i], XMVectorScale(ray_direction, std::max(0.f, corner_depth - near_plane)));
    }
    result.caster_padding = std::max(.01f, 3 * texel); // PCF footprint, texel snapping and floating-point error
    for (unsigned a = 0; a < hull.size(); ++a)
        for (unsigned b = a + 1; b < hull.size(); ++b)
            for (unsigned c = b + 1; c < hull.size(); ++c) {
                auto normal = XMVector3Cross(XMVectorSubtract(hull[b], hull[a]), XMVectorSubtract(hull[c], hull[a]));
                if (XMVectorGetX(XMVector3LengthSq(normal)) < 1e-8f) continue;
                normal = XMVector3Normalize(normal);
                auto plane = XMVectorSetW(normal, -XMVectorGetX(XMVector3Dot(normal, hull[a])));
                float minimum = 0, maximum = 0;
                for (const auto point : hull) {
                    const float distance = XMVectorGetX(XMPlaneDotCoord(plane, point));
                    minimum = std::min(minimum, distance); maximum = std::max(maximum, distance);
                }
                if (minimum < -.005f && maximum > .005f) continue;
                if (minimum < -.005f) plane = XMVectorNegate(plane);
                XMFLOAT4 value; XMStoreFloat4(&value, plane);
                const bool duplicate = std::any_of(result.caster_planes.begin(), result.caster_planes.end(), [&](const XMFLOAT4& old) {
                    return XMVectorGetX(XMVector3Dot(plane, XMLoadFloat4(&old))) > .99999f && std::abs(old.w - value.w) < .01f;
                });
                if (!duplicate) result.caster_planes.push_back(value);
            }
    return result;
}
}
