#pragma once
#include <array>
#include <optional>
namespace darkangel {
// Row-major matrices for row-vector world/view/projection, matching the rendered camera.
struct WorldUiCamera {std::array<float,16> view{},projection{};float width{},height{};};
struct WorldUiProjection {float x{},y{},scale{},depth{};};
std::optional<WorldUiProjection> project_world_ui(const WorldUiCamera&,double x,double y,double z);
}
