#pragma once
#include <darkangel/rendering.hpp>
#include <nlohmann/json.hpp>
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
    rendering::ToonLighting toon;
    rendering::FogSettings fog;
    rendering::PostProcessSettings post;
    rendering::Color3 sky_color{.08f,.12f,.20f};
    bool sky_enabled{true};
};
AuthoredLight decode_light(const nlohmann::json&);
nlohmann::json encode_light(const AuthoredLight&);
SceneRenderingSettings decode_rendering_settings(const nlohmann::json&);
nlohmann::json encode_rendering_settings(const SceneRenderingSettings&);
const char* light_type_name(rendering::LightType);
}
