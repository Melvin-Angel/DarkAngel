#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/character_motor.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace darkangel {
// A spline is authored on a scene object; its control points are separate scene objects that name it, so the
// ordinary selection, gizmo and Undo shape it. The curve is a uniform Catmull-Rom through the points in order.
// scatter places one instance of a model every `spacing` metres of arc length, pushed sideways by up to `jitter`
// metres and facing along the curve (align) or turned at random. Every random number is a hash of the seed and
// the instance's index, so one source gives the same instances everywhere. Instances are derived: they are not
// scene objects and are never replicated.
struct ScatterDefinition {AssetId model;double spacing{2},jitter{};std::uint32_t seed{1};bool align{true};};
struct SplineDefinition {bool closed{};std::optional<ScatterDefinition> scatter;};
constexpr std::size_t spline_limit=16,spline_point_limit=32,scatter_instance_limit=256;
void validate_spline(const SplineDefinition&);
// Decodes and validates the "spline" component of a scene object.
SplineDefinition decode_spline(std::string_view json);
struct SplineSample {MotorVec position{},tangent{};};
class Spline {
public:
    // Two to spline_point_limit points. A closed spline returns to its first point.
    Spline(std::vector<MotorVec> points,bool closed);
    double length()const{return lengths_.back();}
    // Position and unit tangent at an arc length from the start, clamped to the curve.
    SplineSample sample(double distance)const;
    // The curve as straight pieces, for drawing.
    const std::vector<MotorVec>& polyline()const{return line_;}
private:std::vector<MotorVec> line_;std::vector<double> lengths_;
};
struct ScatterInstance {MotorVec position{};double yaw{};};
// The instances of a scatter along a curve, in order from its start. truncated is set when the limit cut them short.
std::vector<ScatterInstance> scatter_along(const Spline&,const ScatterDefinition&,bool closed,bool* truncated=nullptr);
}
