# R7 rendering implementation report

Date: 16 October 2026

Status: In progress. This report records the first implementation checkpoint; it is not a claim that the expanded R7 acceptance criteria are complete.

## Architecture used

The renderer keeps the existing Diligent device/context, scene viewport ownership, cooked model/material resources, GPU skinning path, Place/Inspector transaction path, and `ObjectData::optional_json` preservation. R7 scene-authored lighting and rendering settings are ordinary optional components, so they follow existing selection, undo/redo, save/load, prefab and cooking routes. No parallel scene or material format was introduced.

The first runtime rendering core is game-agnostic (`engine/runtime/rendering.*`). It provides stable-ID scene lights, toon parameters, fog/post settings, a deterministic CPU light-cluster preparation path, bounded 64-candidate cluster capacity, and a four-light shading budget. Directional lights are kept separate from local-light selection. The current assignment is intentionally a CPU reference implementation while the GPU compute assignment and per-view clustered buffers remain future work in R7.3.

## Implemented in this checkpoint

- R7 scene light components: Directional, Point and Spot, with stable scene identity, authored defaults, Inspector editing and Place-panel creation under Lighting.
- Tools > Lighting: sky/ambient settings, main directional-light selection, toon bands, fog and post settings. Scene settings and light components use schema 1 JSON and stable references.
- Shared toon constants and lighting evaluation for the existing static/skinned material pipelines, including deterministic local-light ordering, material-specific toon overrides, object-space triplanar base sampling, character hit/status/dissolve controls, terrain layer sampling/blending controls, emission and alpha modes.
- Registered and cooked material families: `toon-standard-v1`, `toon-character-v1`, and `toon-terrain-v1`. Existing `lit-color-v1` and `lit-surface-v1` contracts remain compatible.
- HDR viewport accumulation in `RGBA16_FLOAT` targets and a separate display target. The post stage performs exposure, neutral tone mapping, contrast/saturation/tint, bounded single-pass bloom sampling, and FXAA before ImGui composition.
- Scene-authored sky color and linear/exponential distance fog. Fog is applied in the material pass so transparent objects are not fogged again in the post stage.
- Expanded resource bindings and shader feature bits are bounded; float/color edits do not create new PSO keys.

## Verification receipts

- `R7RenderingTests.exe`: passed; deterministic cluster case reports 5,760 clusters, 2,856 non-empty clusters and no overflow.
- `MaterialTests.exe`: passed; includes Standard toon, Character visual-state and Terrain layer/blend cooking checks.
- CTest `R7.rendering_core`: passed.
- `DarkAngelEditor` guarded target build: passed with the pinned MSVC/CMake environment and `VCPKG_MANIFEST_INSTALL=OFF`.
- `scripts/verify_scene_navigation.py --gate place`: passed on D3D12 after the HDR/post/fog shader changes. The gate exercised DXC and the post-process pipeline and wrote a foreground capture.

## Not yet implemented or not visually qualified

- Real cascaded directional shadow maps, PCF sampling, shadow-caster passes, and skinned/alpha-cutout shadow qualification.
- GPU compute clustered assignment and per-view GPU occupancy/overflow buffers; the current CPU assignment is a correctness reference, not the final GPU path.
- Object/group-ID plus depth/normal screen-space outlines and distance fading.
- Cubemap/equirectangular HDR sky import, procedural sky recipe publication and sun synchronization.
- Full validation scene, material hot-reload qualification, save/reload/cook/run parity, Vulkan execution, GPU captures/timings, and Royal Temple visual qualification.
- Advanced AO is not enabled; baked GI, lightmaps and the explicitly deferred systems remain out of scope.

The D3D12 capture is evidence that the modified pipeline executes; it is not evidence that the final R7 visuals or all material families are artistically qualified.
