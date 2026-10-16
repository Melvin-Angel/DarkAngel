#pragma once
#include <darkangel/asset_id.hpp>
#include <darkangel/world.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace darkangel::rendering {
enum class LightType : unsigned char { Directional, Point, Spot };
struct Color3 { float r{1}, g{1}, b{1}; };
struct SceneLight { StableId id; LightType type{LightType::Point}; bool enabled{true}; bool cast_shadows{}; Color3 color{1,1,1}; float intensity{1}, range{8}, inner_cone_degrees{20}, outer_cone_degrees{35}, falloff{2}; std::array<float,3> position_view{}; std::array<float,3> direction_view{0,-1,0}; };
struct ToonLighting { Color3 ambient_color{.16f,.18f,.24f}; float ambient_strength{.35f}; Color3 shadow_tint{.22f,.25f,.38f}; std::array<float,3> band_thresholds{.18f,.52f,.82f}; float transition_softness{.035f}; float local_light_strength{1}; };
struct OutlineSettings { bool enabled{true}; Color3 color{.025f,.015f,.04f}; float thickness{1}; float depth_sensitivity{80}; float normal_sensitivity{1.5f}; float group_sensitivity{1}; float fade_start{20}; float fade_end{180}; };
struct ShadowSettings { bool cascades_enabled{true}; unsigned cascade_count{4}; unsigned resolution{2048}; float distance{180}; float depth_bias{.001f}; float normal_bias{.002f}; float split_distribution{.5f}; };
struct SkySettings { bool procedural{}; AssetId hdr_texture{}; float hdr_intensity{1}, hdr_rotation_degrees{}; Color3 zenith{.06f,.10f,.22f}; Color3 horizon{.42f,.30f,.24f}; Color3 ground{.025f,.02f,.03f}; std::array<float,3> sun_direction{-.35f,.7f,-.55f}; Color3 sun_color{1,.75f,.45f}; float sun_size{.025f}; float sun_intensity{2}; float cloud_amount{}; unsigned seed{}; };
struct FogSettings { bool enabled{}; Color3 color{.35f,.42f,.52f}; bool exponential{}; float start{20}, end{180}, density{.015f}, height{}, height_falloff{}; };
struct PostProcessSettings { bool bloom{}; float bloom_threshold{1}, bloom_soft_threshold{.5f}, bloom_intensity{.35f}, bloom_radius{4}, exposure{}, exposure_compensation{}, contrast{1}, saturation{1}; Color3 tint{1,1,1}; bool fxaa{true}; OutlineSettings outline; };
struct ClusterConfig { unsigned tile_size{16}, depth_slices{24}, max_candidates{64}, shading_budget{4}; };
struct ClusterView { unsigned width{1}, height{1}; float near_plane{.05f}, far_plane{1000}, tan_half_fov_y{.41421356f}, aspect{1}; };
struct ClusterCell { std::vector<StableId> candidates, selected; bool overflow{}; };
struct ClusterStats { std::size_t clusters{}, non_empty{}, overflowed{}, candidate_links{}, selected_links{}, directional_lights{}; };
// GPU assignment telemetry is intentionally separate from the deterministic CPU reference. The renderer may
// publish this one frame late while the readback is still in flight, so readiness is explicit.
struct ClusterGpuStats { bool ready{}; std::size_t non_empty{}, overflowed{}, candidate_links{}, selected_links{}, max_occupancy{}; };
struct ClusterLayout { unsigned tiles_x{}, tiles_y{}, slices{}; std::size_t clusters{}; };
struct ClusterAssignment { unsigned tiles_x{}, tiles_y{}, slices{}; std::vector<ClusterCell> cells; ClusterStats stats; ClusterCell& at(unsigned,unsigned,unsigned); const ClusterCell& at(unsigned,unsigned,unsigned) const; };
ClusterLayout cluster_layout(const ClusterConfig&, const ClusterView&);
ClusterAssignment assign_lights(const ClusterConfig&, const ClusterView&, std::span<const SceneLight>);
std::array<float,3> evaluate_toon(const ToonLighting&,Color3 base,Color3 main_color,float main_intensity,float main_visibility,float normal_main_dot,std::span<const SceneLight>,float ambient_occlusion=1);
float evaluate_fog(const FogSettings&,float distance,float world_height=0);
Color3 apply_color_grading(const PostProcessSettings&,Color3 linear);
}
