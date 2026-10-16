# R7 rendering implementation report

Date: 16 October 2026

Status: In progress. This report records the first implementation checkpoint; it is not a claim that the expanded R7 acceptance criteria are complete.

## Architecture used

The renderer keeps the existing Diligent device/context, scene viewport ownership, cooked model/material resources, GPU skinning path, Place/Inspector transaction path, and `ObjectData::optional_json` preservation. R7 scene-authored lighting and rendering settings are ordinary optional components, so they follow existing selection, undo/redo, save/load, prefab and cooking routes. No parallel scene or material format was introduced.

The first runtime rendering core is game-agnostic (`engine/runtime/rendering.*`). It provides stable-ID scene lights, toon parameters, fog/post settings, a deterministic CPU light-cluster preparation path, bounded 64-candidate cluster capacity, and a four-light shading budget. Directional lights are kept separate from local-light selection. The primary viewport now prepares one assignment per frame and uses the selected stable IDs for each renderable's screen/depth cluster; the CPU assignment remains the correctness reference while GPU compute buffers remain future work in R7.3.

## Implemented in this checkpoint

- R7 scene light components: Directional, Point and Spot, with stable scene identity, authored defaults, Inspector editing and Place-panel creation under Lighting.
- Tools > Lighting: sky/ambient settings, main directional-light selection, toon bands, fog and post settings. Scene settings and light components use schema 1 JSON and stable references.
- Shared toon constants and lighting evaluation for the existing static/skinned material pipelines, including deterministic local-light ordering, material-specific toon overrides, object-space triplanar base sampling, character hit/status/dissolve controls, terrain layer sampling/blending controls, emission and alpha modes.
- Registered and cooked material families: `toon-standard-v1`, `toon-character-v1`, and `toon-terrain-v1`. Existing `lit-color-v1` and `lit-surface-v1` contracts remain compatible.
- HDR viewport accumulation in `RGBA16_FLOAT` targets and a separate display target. The post stage performs exposure, neutral tone mapping, contrast/saturation/tint, bounded single-pass bloom sampling, and FXAA before ImGui composition.
- Real-time main directional shadows: bounded one-to-four D32 cascades, configurable 512/1024/2048/4096 resolution, authored distance, depth/normal bias and split distribution, static/skinned caster PSOs, and 3x3 comparison-PCF receiver sampling. The current projection still needs texel snapping/stability qualification and uses a four-slice resource array with unused slices safely disabled by the authored count.
- Primary-view local-light selection now uses the per-view cluster assignment instead of scene insertion order. Candidate capacity and four-light shading budget are distinct, stable-ID tie-breaking is deterministic, and occupancy/overflow statistics are retained on `SceneViewport` for later GPU-stat/debug presentation.
- Spotlights now carry their transformed direction plus inner/outer cone and falloff parameters into the shared four-light toon payload; they are no longer shaded as unrestricted point lights.
- Initial outlines are now a registered pre-FXAA composition stage: the existing opaque/cutout material passes can emit encoded visible normals and stable object-group IDs into an isolated per-view target, and the post shader combines depth, normal and group discontinuities with configurable thickness, sensitivity and distance fade. Transparent surfaces are intentionally excluded until their depth/ID policy is defined.
- Scene-authored sky color and linear/exponential distance fog. Fog is applied in the material pass so transparent objects are not fogged again in the post stage.
- Saved procedural sky recipe fields (zenith, horizon, ground, sun direction/size/colour/intensity, cloud amount and deterministic seed) now persist with the scene profile and render as a bounded HDR background gradient/sun disk in the post stage. The procedural sun is environment-only; the selected Main Light remains authoritative for toon lighting and shadows.
- Expanded resource bindings and shader feature bits are bounded; float/color edits do not create new PSO keys.

## Verification receipts

- `R7RenderingTests.exe`: passed; deterministic cluster case reports 5,760 clusters, 2,856 non-empty clusters and no overflow.
- `MaterialTests.exe`: passed; includes Standard toon, Character visual-state and Terrain layer/blend cooking checks.
- CTest `R7.rendering_core`: passed.
- `DarkAngelEditor` guarded target build: passed with the pinned MSVC/CMake environment and `VCPKG_MANIFEST_INSTALL=OFF`.
- `scripts/verify_scene_navigation.py --gate place`: passed on D3D12 after the HDR/post/fog shader changes. The gate exercised DXC and the post-process pipeline and wrote a foreground capture.
- `scripts/verify_royal_renderer.py`: partial only. Royal document load, D3D12/Vulkan process execution, and GPU/reference image parity all passed for the generated rest/attack captures; the final visible-attack-delta assertion failed because the captured camera contained no qualifying pose change. The retained `royal-*.log/png` files are evidence, not a cross-backend acceptance pass.

## Not yet implemented or not visually qualified

- Shadow visual qualification, stable texel-snapped cascades, alpha-cutout caster discard and transparent-shadow policy still require the dedicated rendering validation scene. The cascade count/resolution/distance/bias/split controls are now persisted and drive the bounded shadow resources.
- GPU compute clustered assignment and per-view GPU occupancy/overflow buffers; the current CPU assignment is a correctness reference and the primary-view integration step, not the final GPU path.
- Independent visual qualification of the object/group-ID plus depth/normal screen-space outlines and distance fading; the functional pass and persisted controls are present, but the current placement fixture does not contain the authored R7 validation scene.
- Cubemap/equirectangular HDR sky import, procedural recipe asset publication, simple-cloud generation and editor visual qualification. The scene-profile procedural recipe is functional; its sun synchronization policy is explicit and intentionally does not override the selected Main Light.
- Full validation scene, material hot-reload qualification, save/reload/cook/run parity, a qualifying visible Royal pose delta, GPU captures/timings, and Royal Temple visual qualification.
- Advanced AO is not enabled; baked GI, lightmaps and the explicitly deferred systems remain out of scope.

The D3D12 capture is evidence that the modified pipeline executes; it is not evidence that the final R7 visuals or all material families are artistically qualified.
