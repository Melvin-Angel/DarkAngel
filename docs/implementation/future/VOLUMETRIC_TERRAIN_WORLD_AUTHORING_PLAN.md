# Volumetric Terrain and World Authoring System

Status: **Planned / documentation only.** This is a future R9 workstream. It does not claim that volumetric terrain, splines, water, terrain painting, world streaming, or the performance budgets below exist in the current engine.

## Product intent

DarkAngel should make a beautiful, complex third-person action-RPG environment quick to build from the Scene / Level Design workflow. World Tools therefore belong inside that workspace: the designer selects a terrain, spline, water region, brush, or scene asset in the same viewport, edits it directly, and sees derived mesh, collision, navigation, and material results update without assembling a collection of unrelated components.

The system is intentionally not a heightmap-only terrain feature. It must represent caves, tunnels, arches, overhangs, cliffs, stacked surfaces, and floating islands while supporting stylized low-poly and toon-shaded presentation. Hand authoring and deterministic procedural generation share the same authored source model and can be combined. Runtime digging/destruction is out of scope for this milestone unless a later game feature establishes a separate authoritative contract.

This plan extends the initial mesh-first terrain, surface painting, spline, and scatter work assigned to historical M6 / DAE-015–017. M6 can ship useful non-volumetric products first; R9 adds volumetric source data and a unified authoring workflow after the editor, asset/material, job, renderer, physics, serialization, and navigation foundations are ready.

## Roadmap placement and dependencies

R9 follows R1 Scene Editor Navigation, R2 Editor UI/UX Overhaul, R3 Movement Authoring, R4 Prefab Authoring and Publishing, R5 Asset and Material Editing, R6 UI Authoring and Gameplay Integration, and R7 Environment and Rendering Tools. It does not renumber historical M0–M9 IDs or the deferred R8 World Puzzle UI item.

Required checkpoints:

- R2: shared selection, viewport gizmos, command history, property inspection, layout, focus/input routing, and non-stuttering editor jobs.
- R4/R5: stable asset IDs, source/cooked separation, material slots/keys, reusable modular assets, placement and prefab identity.
- R7: Vulkan validation, stylized material/shader conventions, lighting/fog/post profiles, and renderer resource lifetime.
- M6: scene regions/cells, initial collision/navigation products, spline/scatter contracts, NPC navigation queries, and world identity rules.
- Existing services: WorldSession/cell residency, EditorService prepare/commit/undo, JobService/bounded background work, SceneDocument serialization, AssetService/cook, Diligent Vulkan/D3D12 paths, Jolt collision, and Recast/Detour navigation.

The first implementation must not silently make terrain a second scene format. Editable terrain, spline, water, paint, scatter, and brush operations are scene-owned source records with stable IDs. Generated meshes, collision, navmesh tiles, GPU resources, thumbnails, and caches are derived products with generation/version metadata and explicit invalidation.

## Authoring architecture

### Source-to-runtime pipeline

```text
Terrain source (SDF chunks, layers, seed, edit history)
        + terrain paint/material rules
        + spline/water/placement source records
                    |
                    v
        dirty regions / dependency graph / generation IDs
                    |
       background mesh + paint + collision + nav jobs
                    |
                    v
        render mesh, collision mesh, nav data, streaming package
```

Each source record is editable and recoverable. A failed or cancelled job retains the last valid derived generation and reports stale state; it never replaces authored data with a partial result. Save commits source data and product manifests atomically, with rebuildable products treated like cooked assets rather than authoritative scene content.

### Volumetric terrain model

- Store a sparse, chunked signed-distance field (SDF), with configurable voxel resolution per terrain asset or region. Chunk coordinates, bounds, material channels, seed, schema version, and edit generation are stable and serializable.
- Keep a narrow-band or brick-friendly representation where possible; prototype dense, sparse, compressed, and streamed storage on the same representative island before selecting the encoding.
- Support additive and subtractive primitives, smooth/flatten/inflate/carve/noise/erosion-like modifiers, and deterministic procedural layers. Non-destructive modifier stacks are preferred for operations whose cost and dependency behavior remain understandable; baked checkpoints may be used for production scale.
- Track dirty voxels, surface bounds, and dependent spline/water/paint/scatter regions. Rebuild only affected chunks, with neighbor invalidation for seam safety.
- Do not commit to Marching Cubes, Surface Nets, Dual Contouring, or another extractor before prototypes measure stylized flat/smooth output, sharp-feature retention, topology failure modes, memory, rebuild time, and seam behavior. The target visual modes are flat-shaded faceted terrain and smooth-shaded terrain; both must be first-class outputs.
- Generate collision and navigation from the derived surface using bounded background jobs. Jolt receives an explicit collision generation; Recast/Detour receives explicit tile invalidation/cooking. Render, collision, and nav products may complete at different times and expose their generation/status independently.
- Use chunk LOD and transition rules that keep boundaries watertight. Streaming and far-field simplification must preserve stable world coordinates, references, and playable collision policy.

### Paint and materials

Terrain paint is a source layer over the generated surface, not a second mesh authority. The minimum model supports vertex-color channels, multiple keyed terrain materials, texture blending, biome masks, height/slope rules, procedural distributions, and brush parameters (radius, falloff, opacity, strength). Designers should be able to produce attractive stylized terrain with a small material palette and sensible defaults.

Material evaluation must target the future toon/cel-shading pipeline and current Vulkan renderer contracts. It must tolerate generated topology changes: stable terrain material keys and paint layers survive a local rebuild, while explicitly documented remapping handles topology or resolution changes. Texture-heavy authoring is optional; vertex color and rule-driven variation are the fast path.

### Shared spline framework

Splines are reusable scene source records with stable control-point IDs, editable tangents, width, banking, elevation, falloff, and local coordinate options. Generated mesh, collision, paint masks, terrain deformation, and placed instances are derived outputs. Editing a spline updates all dependents through the same dirty-region/job pipeline.

The Level Design / World Tools workspace provides generators for roads and paths, walls/fences/railings, bridges, building pads, and other repeated modular structures. Closed regions provide lakes, ponds, reservoirs, flattening, and material masks. Automatic asset placement follows the spline with authored spacing, alignment, orientation, banking, slope, and surface-conform rules; placed instances retain stable IDs and can be overridden or regenerated safely.

### Water authoring

Water uses the shared spline/region infrastructure. Open splines define rivers and streams; closed regions define lakes and ponds. Source data includes flow direction, speed, width, depth, bank/bed influence, material profile, and optional terrain-modification policy. The system generates a surface, banks/transition masks, collision/query surfaces, and stylized shader parameters; it does not require full fluid simulation.

When a flowing spline encounters an authored or generated unsupported vertical drop above a prototype threshold, the authoring preview may propose a waterfall segment. The designer accepts or adjusts it. Waterfall outputs include geometry, UVs, foam and transition effects, flow direction, and a stable relationship to the river and receiving lake/terrain. Terrain sculpting and spline edits remain reversible source operations.

### World productivity tools

Keep these in the Scene / Level Design workspace, sharing selection, commands, gizmos, history, and background jobs:

- surface snapping and alignment; drag/drop placement; multi-object placement, duplication, and compatible regeneration;
- brush scattering for rocks, trees, foliage, and debris with density, scale, rotation, slope, surface, biome, seed, and exclusion filters;
- vertex/material painting and terrain-rule previews;
- lightweight CSG/brush modeling or an equivalent greybox volume workflow; basic mesh edits for blockouts;
- live previews, progress/stale indicators, real-time gizmos, and Undo/Redo for every authored operation.

This is one coherent authoring surface, not a new top-level tab per tool. Heavy mesh, collision, navigation, and scatter work is cancellable and asynchronous. Viewport interaction remains responsive while a previous valid generation is displayed.

## Proposed stages and actionable backlog

### R9-A — Terrain foundation

Prototype and then implement source-owned SDF chunks, coordinate/version schema, configurable voxel resolution, serialization, one approved initial meshing path, flat/smooth output, generated render resources, and basic Jolt collision. Add diagnostic views for chunk bounds, voxel resolution, dirty state, and generation IDs.

Exit evidence: a saved/reloaded cave-and-cliff sample produces the same deterministic surface; collision matches the displayed generation; failed rebuild preserves the prior valid generation; a second scene has independent terrain state.

### R9-B — Interactive sculpting

Add viewport brush raycasts, add/subtract/smooth/flatten/inflate/carve/noise/erosion-like operations, adjustable brush radius/falloff/opacity/strength, modifier/source history, transactional Undo/Redo, dirty-region tracking, neighbor invalidation, incremental rebuild, cancellation, and background mesh/collision jobs.

Exit evidence: a designer can sculpt a tunnel and overhang interactively, undo and redo a multi-chunk operation, continue editing while a rebuild runs, and recover after a cancelled or failed job without data loss.

### R9-C — Materials and painting

Add terrain material keys/layers, vertex-color channels, texture blending, biome masks, height/slope rules, procedural deterministic distribution, brush painting, preview/validation, and stylized Vulkan shader integration. Define topology/resolution remapping and stale paint diagnostics.

Exit evidence: a terrain retains paint and material assignments after save/reload and a local rebuild; flat and smooth modes show the intended stylized result in Vulkan; invalid material references are reported without corrupting source.

### R9-D — Spline framework

Add reusable open/closed splines, control-point editing, width/banking/elevation, snapping, terrain conform/deform masks, generated road/path/wall/fence/bridge/pad products, modular asset placement, collision, and derived-output invalidation. Reconcile with the M6 spline/scatter records instead of creating a parallel schema.

Exit evidence: editing one road control point updates its mesh, terrain edge, collision, nav dirty region, and placed markers; Undo/Redo and reload preserve stable IDs and independent instance overrides.

### R9-E — Water tools

Add river/stream/lake/pond source records, flow controls, width/depth, surface and bank generation, receiving-water relationships, waterfall proposal/override, foam/UV/transition outputs, terrain integration, collision/query behavior, and efficient stylized water shaders.

Exit evidence: a river crosses a terrain edit, a waterfall is regenerated after its source edge moves, a closed lake reloads correctly, and water products expose stale/cooking status without stalling the editor.

### R9-F — World-building productivity

Add scatter/placement brushes, deterministic seeds, multi-selection/duplication, surface/slope filters, exclusion volumes, greybox CSG/brush operations, basic blockout mesh editing, reusable gizmo presets, and an integrated World Tools layout. Connect placement to R4 prefabs and R5 material/asset keys.

Exit evidence: the representative environment can be blocked out and populated entirely in the Level Design workspace, with every operation undoable and no tool-specific duplicate scene format.

### R9-G — Production readiness

Measure and implement chunk LOD, watertight transitions, optional world streaming integration, bounded residency, background collision/navmesh cooking, profiling, save/load/rebuild stress tests, generation retirement, crash/cancel recovery, and clean-machine package behavior. Validate D3D12 where useful, but Vulkan is the required renderer qualification path for this milestone.

Exit evidence: acceptance budgets are measured on representative content, not inferred from a successful build; stale generations, unloaded tiles, and save migrations have explicit tests and diagnostics.

## Technical decisions requiring prototypes

Before locking implementation, build small isolated prototypes for:

1. SDF encodings and compression at at least two voxel resolutions, including memory, disk, load, edit, and dirty-region costs.
2. Marching Cubes vs Surface Nets vs Dual Contouring (and any Brutus/DualMC-derived reference implementation) for stylized flat/smooth output, sharp arches, cracks, topology, normals, seams, and incremental rebuilds.
3. LOD transition strategies with caves, floating-island undersides, and narrow bridges; measure popping, watertightness, and collision policy.
4. Material/vertex paint persistence across topology changes and Vulkan shader cost with a small material palette.
5. Jolt collision and Recast/Detour tile cooking from partial dirty regions, including cancellation and a last-valid-generation swap.
6. Spline deformation and waterfall detection rules on steep terrain, including designer override and deterministic regeneration.
7. Job scheduling and editor interaction under simultaneous mesh, collision, nav, and scatter work; record queue latency and cancellation behavior.

## Open-source research candidates

These are references for learning and prototype comparison, not approved dependencies: Godot Voxel Tools (Zylann) for SDF/chunk/LOD/collision/editing ideas; Brutus for a lightweight C++ Marching Cubes implementation; DualMC for alternative extraction; Godot Waterways for spline-driven rivers; and Unreal Engine Water System for integrated spline-water workflow reference.

For each candidate, record license, attribution obligations, current maintenance/activity, supported toolchain, algorithmic assumptions, data-format compatibility, integration cost, and whether code reuse would create a runtime or editor dependency. Prefer extracting lessons or writing a narrow compatible adapter over adding a large dependency. No candidate is approved until legal and technical review are complete.

## Acceptance environment and proposed budgets

The editor-only acceptance scene is a large floating island with cliffs and varied elevation, a sculpted cave/tunnel, a second island joined by a bridge, a winding road to an ancient temple, a river, a waterfall from the island edge into a lake, painted terrain materials and vertex-color variation, scattered vegetation/rocks/props, and greyboxed ruins/buildings. Where applicable it has working Jolt collision and NPC navigation. Save, close/reopen, rebuild derived products, and continue editing without losing source data.

Initial proposed budgets are planning targets, not validated limits:

- common brush feedback target: viewport input and gizmos remain interactive at 30 Hz or better while derived work runs in the background;
- local edit target: a small brush stroke invalidates/rebuilds its visible chunk set in under 250 ms median and under 500 ms p95 on the reference development machine;
- save/reload target: source save and reopen of the representative scene completes within 5 seconds excluding a separately reported first-time cook;
- background collision/nav target: no editor-wide stall longer than 100 ms from a normal local edit, with stale status visible while cooking;
- streaming/LOD target: representative far-field terrain uses bounded memory and has no visible seam/crack at tested transitions;
- authoring target: the complete acceptance environment is reproducible with deterministic seeds and can be edited after reload without manual repair.

Each target must be replaced or retained with measured evidence after prototypes. Hardware, resolution, scene size, active chunk count, renderer, and whether the result is warm/cold cache must be recorded. A passing target is not a claim of universal performance.

## Risks and boundaries

- SDF memory, thin features, topology changes, and LOD seams may make a single representation unsuitable; retain a mesh-first fallback and prototype hybrid products.
- Background generation can expose stale collision or nav data; authority must use explicit generation checks and safe last-valid products.
- Non-destructive stacks can become expensive or difficult to serialize; define flatten/checkpoint rules and bounded history before promising unlimited editing.
- Terrain paint tied to topology can be lost during remeshing; stable keys, remapping, and diagnostics are mandatory.
- Large scatter sets and stable interactive vegetation IDs interact with M6 persistence and streaming; do not hide them in renderer-only instance buffers.
- Waterfall detection is heuristic; make it a controllable proposal with authored overrides, not an opaque simulation.
- Vulkan shader/material limitations, Jolt mesh cooking cost, and Recast tile limits require measured integrations; planned documents are not implementation evidence.
- Keep terrain source, generated products, runtime scene objects, and future streaming residency separate. Do not add runtime voxel editing, full fluid simulation, or a separate world-authoring tab without a new approved scope.

## Cross-references and ownership

The main schedule is [DarkAngel Implementation Handoff](../../architecture/DarkAngel_Implementation_Handoff.md). The current editor direction is in the historical [Editor Workflow Plan](../legacy/plans/EDITOR_WORKFLOW_PLAN.md); its advanced-volumetric deferral is superseded by this R9 plan once R9 is scheduled. The [Editor UX Backlog](EDITOR_UX_BACKLOG.md) remains the place for shared editor controls and should link new World Tools commands rather than duplicate them. [Tiled Navigation and Streaming Readiness](TILED_NAVIGATION_PLAN.md) remains a separate navigation/scale dependency and is not closed by this plan.
