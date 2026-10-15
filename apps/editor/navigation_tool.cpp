#include "navigation_tool.hpp"
#include <darkangel/static_collision.hpp>
#include <darkangel/check.hpp>
#include <imgui.h>
#include <algorithm>
#include <chrono>

namespace darkangel::editor_app {
std::string NavigationTool::collision_key(const CollisionDefinition& collision) {
    std::string key = std::to_string(collision.boxes.size());
    for (const auto& mesh : collision.meshes)
        key += ':' + std::to_string(mesh.id) + ':' + (mesh.data ? mesh.data->signature : std::string{});
    if (collision.statics)
        key += ":static:" + collision.statics->revision();
    return key;
}
bool NavigationTool::same_settings(const NavigationSettings& a, const NavigationSettings& b) {
    return a.cell_size == b.cell_size && a.cell_height == b.cell_height && a.agent_height == b.agent_height &&
           a.agent_radius == b.agent_radius && a.agent_climb == b.agent_climb && a.slope_degrees == b.slope_degrees;
}
void NavigationTool::bake(const CollisionDefinition* source) {
    auto& navigation = *this;
    navigation.error.clear();

    try {
        require(source != nullptr, "Navigation baking needs Play resources for the scene's static collision");
        const auto& collision = *source;
        const auto start = std::chrono::steady_clock::now();
        auto mesh = std::make_shared<const darkangel::NavigationMesh>(collision, navigation.settings);

        navigation.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        navigation.triangles = 0;
        for (const auto& box : collision.boxes)
            if (!box.moving && !box.dynamic && !box.sensor)
                navigation.triangles += 12;
        for (const auto& source : collision.meshes)
            if (source.data)
                navigation.triangles += source.data->triangles.size();
        if (collision.statics)
            for (const auto& body : collision.statics->bodies())
                navigation.triangles += body.geometry->definition().triangles.size();

        navigation.mesh = std::move(mesh);
        navigation.outline = navigation.mesh->outline();
        navigation.key = collision_key(collision);
        navigation.baked_settings = navigation.settings;
    }

    catch (const std::exception& error) {
        navigation.error = error.what();
    }
}
std::shared_ptr<const NavigationMesh> NavigationTool::mesh_for(const CollisionDefinition& collision) {
    auto& navigation = *this;
    if (navigation.mesh && navigation.key == collision_key(collision)) {
        ++navigation.reused;
        return navigation.mesh;
    }
    return std::make_shared<const darkangel::NavigationMesh>(collision, navigation.settings);
}
void NavigationTool::draw_panel(const CollisionDefinition* source) {
    auto& navigation = *this;
    ImGui::TextWrapped("Bakes one navigation mesh from the scene's cooked static collision. NPC controllers use it to find paths; movement "
                       "and collision stay with the motor. While the collision is unchanged, Play uses this bake; otherwise it builds a "
                       "mesh on demand with these settings.");

    ImGui::SeparatorText("Agent and voxel settings");
    auto field = [](const char* label, double& value, double low, double high) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputDouble(label, &value, 0, 0, "%.2f"))
            value = std::clamp(value, low, high);
    };

    field("Cell size (m)", navigation.settings.cell_size, .05, 2);
    field("Cell height (m)", navigation.settings.cell_height, .05, 2);
    field("Agent height (m)", navigation.settings.agent_height, .5, 4);
    field("Agent radius (m)", navigation.settings.agent_radius, .1, 2);
    field("Step height (m)", navigation.settings.agent_climb, 0, 1);
    field("Walkable slope (deg)", navigation.settings.slope_degrees, 1, 89);

    if (ImGui::Button("Use defaults"))
        navigation.settings = {};

    ImGui::SeparatorText("Bake");
    const bool available = source != nullptr;
    ImGui::BeginDisabled(!available);
    if (ImGui::Button("Bake navigation mesh"))
        bake(source);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!navigation.mesh);
    if (ImGui::Button("Clear bake")) {
        navigation.mesh.reset();
        navigation.outline.clear();
        navigation.key.clear();
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("Show in the viewports", &navigation.shown);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draws the baked mesh's polygon edges over the Scene and Game viewports; the mesh's border is brighter.");
    if (navigation.shown && !navigation.mesh) {
        ImGui::SameLine();
        ImGui::TextDisabled("Nothing to show until a mesh is baked.");
    }

    if (!available)
        ImGui::TextDisabled("This session has no cooked scene collision to bake.");

    if (!navigation.error.empty())
        ImGui::TextColored({1.f, .55f, .3f, 1.f}, "Bake failed: %s", navigation.error.c_str());

    if (navigation.mesh) {
        const bool current = available && navigation.key == collision_key(*source);
        ImGui::Text("Polygons: %u", navigation.mesh->polygons());
        ImGui::Text("Source triangles: %zu", navigation.triangles);
        ImGui::Text("Bake time: %.1f ms", navigation.milliseconds);
        ImGui::Text("Used by Play sessions: %u", navigation.reused);

        if (!current)
            ImGui::TextColored({1.f, .55f, .3f, 1.f}, "Stale: the scene collision changed since this bake. Bake again.");
        else if (!same_settings(navigation.settings, navigation.baked_settings))
            ImGui::TextColored({1.f, .55f, .3f, 1.f}, "Settings changed since this bake. Bake again to apply them.");
        else
            ImGui::TextDisabled("Current: the next Play session uses this bake.");
    }

    else
        ImGui::TextDisabled("Not baked. Play builds a mesh on demand when an NPC needs one.");
}
} // namespace darkangel::editor_app
