# Tiled navigation and streaming readiness

16 October 2026. **Planned, not implemented. Priority: low / future. No implementation date.** Category: navigation / world scale. Backlog entry: [roadmap deferred work](../../architecture/DarkAngel_Implementation_Handoff.md#deferred-work-and-unresolved-planning-decisions). Scheduled after the higher-priority editor authoring and gameplay workflows (R2–R8); it does not displace them. Dependency: the existing Recast & Detour navigation.

## Current state

- Library: Recast & Detour (`recastnavigation`, vcpkg feature `m6-nav`). Wrapper: `engine/runtime/navigation.cpp` (`NavigationMesh`, `PathFollow`); editor: Tools > Navigation, `apps/editor/navigation_tool.cpp`; tests: `tests/navigation_tests.cpp`.
- One solo mesh per scene, in the style of Recast's `Sample_SoloMesh`: one heightfield over the scene bounds and one Detour tile. Input is the scene's effective static collision (the per-model static layer from the scene-scale work), each body's shape triangles at its pose.
- `navigation.cpp` refuses a grid above 4096 × 4096 cells ("Navigation scene is too large for one mesh at this cell size"). With the default 0.2 m cell that is about 819 m per axis. A coarser cell admits larger scenes but loses precision near walls, doors and narrow paths.
- The bake lives in memory for the editor session. Play reuses it while the scene collision is unchanged and reports it stale otherwise. There is no cooked navigation product yet.

## Phases

1. **Research and design.** Study `Sample_TileMesh`. Evaluate tile size, border size, cell size and cell height. Document Detour's `maxTiles` / `maxPolys` constraints and the polygon-reference bit split. Fix stable world-space tile coordinates. Define the cooked tile storage format and its versioning.
2. **Tiled baking MVP.** Split the static collision geometry into independently baked tiles and add them to one multi-tile `dtNavMesh`. Paths must work across tile boundaries. The tiled path removes the whole-scene grid limit (a per-tile bound replaces it).
3. **Runtime integration.** Load the cooked tiles in Play and run path queries across tiles. Existing NPC navigation behaviour (`PathFollow`, repath, arrive distance) stays the same. Initially all tiles of the active scene are loaded.
4. **Editor tooling.** Tools > Navigation gains tile size and resolution settings, a tile overlay, bake progress and per-tile errors, tile statistics (count, polygons, memory) and a full rebuild.
5. **Incremental baking.** Detect the tiles touched by collision or settings changes, rebuild only those, reuse unchanged cooked tiles, and add a Rebuild Dirty Tiles action.
6. **Streaming readiness.** Load and unload tiles with Detour's `addTile` / `removeTile`. Align tile coordinates with future world-cell streaming. Handle path queries safely when a tile is unavailable: fail or truncate predictably, never read freed data.
7. **Optional dynamic obstacles.** Research `dtTileCache` and runtime tile rebuilds for doors, moving obstacles and dynamic geometry. This stays separate from, and optional after, the tiled implementation.

## Technical considerations

- **Borders and erosion:** each tile is rasterized with a border of at least the agent radius in cells plus a margin (`borderSize`), so erosion and region building match across seams. Geometry is gathered from the tile bounds expanded by that border.
- **Polygon references:** Detour packs salt, tile index and polygon index into a `dtPolyRef`. `maxTiles` × `maxPolysPerTile` must fit the available bits, so tile size and world size are chosen together.
- **Per-tile limits:** Detour tiles use 16-bit vertex indices and a maximum vertices-per-polygon. Tile size must keep the densest expected tile inside them; an over-limit tile reports an error instead of being silently dropped.
- **Cross-tile connectivity:** tiles connect through matching border edges. Settings (cell size/height, agent dimensions) must be identical across tiles in one mesh, and any change invalidates every tile.
- **Source geometry:** tiles derive from the cooked static collision layer, never from render meshes, so navigation and the motor agree.
- **Deterministic tile identity:** tile coordinates come from world position and tile size, independent of bake order or object order. The same inputs give the same tile bytes.
- **Cooked data compatibility:** tile products carry a format version, the navigation settings and a hash of their input collision. A version or settings change invalidates the tiles, and a stale tile is never mixed with fresh ones. Engine changes to cooker inputs alter `DAE_COOKER_BUILD_HASH`, as for every cooked product.
- **Memory and scaling:** tile count grows with area. Record memory per tile and set a tile budget per scene before streaming exists.
- **Preservation:** the solo-mesh path and its behaviour stay available until the tiled path passes the existing navigation tests and gates.

Initial experimental defaults, **tuning candidates, not decisions:** 64 × 64 cells per tile with a 0.25 m cell (a 16 m tile).

## Acceptance criteria

- A scene larger than the current single-grid limit bakes without that restriction.
- NPCs navigate across tile boundaries.
- Cooked navigation data survives save, reopen and fresh Play.
- Incremental rebuilding changes only the affected tiles; unaffected tile bytes are unchanged.
- Existing navigation tests and gates continue to pass.
- The editor overlay distinguishes valid, invalid (failed or over-limit) and unbaked tiles.
- Runtime tile loading and unloading supports future world streaming without invalid path queries.
