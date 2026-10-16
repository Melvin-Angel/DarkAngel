# R7 renderer implementation outline

Status: implementation increment complete and cross-backend exercised, 16
October 2026. R7 is not marked fully accepted: dedicated cutout-shadow/HDR
visual receipts, cooked save/reload/run parity, and GPU timing qualification
remain open.

## Existing systems reused

- `RenderSystem` owns the native window, Diligent device/context/swap chain,
  Dear ImGui backend, one dynamic frame constant buffer and the existing static
  and skinned material PSOs.
- `SceneViewport` owns independent scene/game color and depth targets, camera
  matrices, CPU scene traversal, static/skinned draw submission, transparent
  ordering and resize handling.
- `SceneResources` owns cooked model/material GPU uploads, generation-aware
  refresh, static/skinned material bindings and shader reload replacement.
- R5 `MaterialDefinition`/`MaterialInstance`, stable imported material slot
  identities, texture semantics, prefab overrides and cooker closure remain
  authoritative for material data. R7 extends those definitions; it does not
  introduce a second material format.
- `ObjectData::optional_json` is the existing versioned-scene escape hatch for
  authored components not yet reflected as native gameplay components. Light
  components and scene rendering settings use this path initially, preserving
  stable object identities, undo/redo, save/load, prefab capture and cooking.
- `Shell::Place`, `scene_add_object`, `scene_set_component` and the existing
  Inspector are the placement/edit transaction path. Lighting is added to that
  path rather than to the Outliner.
- DXC/HLSL, Diligent resource layouts, current depth conventions and the
  existing GPU skinning path are retained. DiligentFX is not assumed available;
  each adapter must be proven against the pinned checkout before use.

## Minimum migration

1. Add bounded, game-agnostic rendering data and deterministic CPU clustered
   light assignment to the runtime-facing library. This owns light types,
   profiles, toon parameters, cluster defaults, overflow accounting and the
   four-light shading budget; it is testable without a GPU.
2. Add versioned `light` and `rendering` scene JSON contracts, three Place
   entries, ordinary Inspector editing, and Tools > Lighting. Main-light
   selection is a stable `StableId`; deleted selections are invalidated rather
   than silently rebound.
3. Move scene color to an HDR-capable per-view target and introduce an explicit
   owned post stack: scene/HDR, fog and transparent composition, bloom,
   exposure/tone mapping/grading, outlines when buffers are available, FXAA,
   then editor/game UI. Disabled stages allocate nothing.
4. Replace the hard-coded stylized shader with shared HLSL toon functions and
   material-family feature keys. Numeric values remain uniforms; structural
   features select bounded PSO variants. Static, skinned and future terrain
   consumers share the library.
5. Add main directional cascaded shadows, GPU cluster assignment resources and
   the validation scene/gates only after resource ownership and resize/barrier
   contracts are stable.

## Current implementation increment

The editor viewport now owns a bounded structured light input buffer, fixed
64-candidate and four-selected-index buffers per cluster, occupancy counters,
overflow counters, and a non-blocking staging readback. The compute-selected
indices are bound to the actual static/skinned pixel shaders and are the
default visual authority when GPU clustered mode is enabled. The CPU assignment
remains an independent deterministic reference path. Resource recreation is
generation/size guarded when cluster settings change. Alpha-tested shadow
casters use the authoritative material Albedo binding and cutoff against a
depth-plus-dummy-colour cascade target; transparent casters remain excluded.

HDR `.hdr` equirectangular sources are cooked through DirectXTex to linear
RGBA16_FLOAT DDS payloads. RGBA16_FLOAT DDS cubemaps/equirectangular payloads
are accepted by the same asset service, preserved as mip/layer data, uploaded
as 2D or cube Diligent resources, and sampled by the post sky stage with
rotation and intensity controls. A synthetic equirectangular import/cook test
and both backend shader/resource creation are covered; the project does not
currently contain an artist-authored HDR sky asset for a final visual receipt.

## Explicit boundaries

The first implementation does not add baked GI, lightmaps, a bake cooker,
deferred PBR/G-buffer infrastructure, temporal AA, HDR10, volumetric fog,
unrestricted point-light shadows, virtual texturing or GPU-driven rendering.
The result must be called complete only after runtime/editor save-reload-cook
and D3D12/Vulkan visual evidence, not after a build or shader compile alone.

## Initial dependency order

R7.1: scene light/environment/fog data and editor workflow.

R7.2: HDR targets and legal post-processing stages.

R7.3: clustered Forward+, toon core and directional cascades.

Expanded material families, outlines and the repeatable visual qualification
scene follow those foundations and remain connected to the R5 contracts.
