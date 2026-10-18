#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace darkangel::assets_detail {
// Angle-weighted smoothing across split/UV vertices. Keep the original topology,
// UVs and skin influences. Props retain 60-degree creases; organic profiles
// may request full smoothing by passing -1 for the crease cosine.
template<class Vertex>
void smooth_normals(std::vector<Vertex>& vertices, std::span<const std::uint32_t> indices, double crease_cosine = .5) {
    std::vector<double> weights(vertices.size());
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto i = indices[t + corner];
            const auto& p = vertices[i].position;
            const auto& a = vertices[indices[t + (corner + 1) % 3]].position;
            const auto& b = vertices[indices[t + (corner + 2) % 3]].position;
            double dot = 0, aa = 0, bb = 0;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const double x = double(a[axis]) - p[axis], y = double(b[axis]) - p[axis];
                dot += x * y; aa += x * x; bb += y * y;
            }
            if (aa * bb > 1e-30)
                weights[i] += std::acos(std::clamp(dot / std::sqrt(aa * bb), -1., 1.));
        }
    std::map<std::array<float, 3>, std::vector<std::size_t>> positions;
    for (std::size_t i = 0; i < vertices.size(); ++i)
        positions[vertices[i].position].push_back(i);
    std::vector<std::array<float, 3>> normals(vertices.size());
    for (const auto& [position, group] : positions)
        for (const auto i : group) {
            std::array<double, 3> sum{};
            for (const auto j : group) {
                double dot = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                    dot += double(vertices[i].normal[axis]) * vertices[j].normal[axis];
                if (dot > crease_cosine)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        sum[axis] += vertices[j].normal[axis] * weights[j];
            }
            const double length = std::sqrt(sum[0]*sum[0] + sum[1]*sum[1] + sum[2]*sum[2]);
            normals[i] = vertices[i].normal;
            if (length > 1e-12)
                for (unsigned axis = 0; axis < 3; ++axis)
                    normals[i][axis] = float(sum[axis] / length);
        }
    for (std::size_t i = 0; i < vertices.size(); ++i)
        vertices[i].normal = normals[i];
}
}
