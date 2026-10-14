#pragma once
#include <darkangel/collision_asset.hpp>
#include <darkangel/navigation.hpp>
#include <memory>
#include <string>
#include <vector>

namespace darkangel::editor_app {
// Tools > Navigation: an explicit bake of the scene's cooked collision that Play reuses while the collision is unchanged.
struct NavigationTool {
    darkangel::NavigationSettings settings, baked_settings;
    std::shared_ptr<const darkangel::NavigationMesh> mesh;
    std::string key, error;
    double milliseconds{};
    std::size_t triangles{};
    unsigned reused{};
    bool shown{};
    std::vector<darkangel::NavigationMesh::Edge> outline;
    // Identifies a collision asset's contents, so a bake knows whether it is still current.
    static std::string collision_key(const CollisionDefinition&);
    static bool same_settings(const NavigationSettings&, const NavigationSettings&);
    // Bakes the given collision with the current settings; a missing collision or a failed bake is kept in `error`.
    void bake(const CollisionDefinition* source);
    // What Play uses: the bake when it matches this collision, otherwise a mesh built on demand with the current settings.
    std::shared_ptr<const NavigationMesh> mesh_for(const CollisionDefinition&);
    // The tool panel, for the open scene's cooked collision (null when the session has none).
    void draw_panel(const CollisionDefinition* source);
};
} // namespace darkangel::editor_app
