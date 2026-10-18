#pragma once
#include <darkangel/rendering.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <array>
#include <string_view>

namespace darkangel::editor_app::render {
struct AuthoredLight {
    rendering::LightType type{rendering::LightType::Point};
    bool enabled{true};
    bool cast_shadows{};
    rendering::Color3 color{1,1,1};
    float intensity{1}, range{8}, inner_cone_degrees{20}, outer_cone_degrees{35}, falloff{2};
};
struct SceneRenderingSettings {
    StableId main_light;
    bool gpu_clustered{true};
    rendering::ClusterConfig clusters;
    rendering::ToonLighting toon;
    rendering::ShadowSettings shadows;
    rendering::SkySettings sky;
    rendering::FogSettings fog;
    rendering::PostProcessSettings post;
    rendering::Color3 sky_color{.08f,.12f,.20f};
    bool sky_enabled{true};
};
struct ProjectRenderingSettings {
    SceneRenderingSettings scene;
    rendering::ToonLighting material_toon;
    std::array<float,4> material_color{1,1,1,1};
    float material_roughness{1};
    float material_metallic{};
    bool material_properties_override{};
};
AuthoredLight decode_light(const nlohmann::json&);
nlohmann::json encode_light(const AuthoredLight&);
SceneRenderingSettings decode_rendering_settings(const nlohmann::json&);
SceneRenderingSettings merge_rendering_settings(const SceneRenderingSettings& defaults, const nlohmann::json& overrides);
nlohmann::json encode_rendering_settings(const SceneRenderingSettings&);
ProjectRenderingSettings decode_project_rendering_settings(const nlohmann::json&);
nlohmann::json encode_project_rendering_settings(const ProjectRenderingSettings&);
ProjectRenderingSettings load_project_rendering_settings(const std::filesystem::path&);
void save_project_rendering_settings(const std::filesystem::path&, const ProjectRenderingSettings&);
const char* light_type_name(rendering::LightType);
}
