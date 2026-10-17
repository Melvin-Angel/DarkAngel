# R7 rendering implementation report

Date: 17 October 2026

Status: **Accepted and complete for the current development phase (17 October
2026).** This is a deliberate project-management closure, not a claim that
every optional historical R7 qualification receipt has been produced.

## R7 closure decision

R7 is now the accepted renderer foundation for Ashen Roots. It provides the
supported D3D12/Vulkan path, HDR scene/display targets, procedural and imported
sky plumbing, Standard/Character/Terrain toon materials, clustered directional/
point/spot lighting, cascaded directional shadows, outlines, fog, bloom, FXAA,
exposure/tone mapping/colour grading, editor-authored settings, scene
serialization and focused cross-backend evidence. The implementation is not to
be extended with a new R7 phase merely to close optional receipts.

The shared forward material shader receives the main directional cascade map
for opaque and alpha-cutout static/skinned materials. Opaque and cutout
static/skinned submissions also enter the cascade shadow pass, so supported
opaque/cutout materials can cast and receive the directional shadows of other
supported casters, including self-shadowing where the map resolution and bias
allow it. Transparent materials are intentionally excluded from the opaque
shadow map. Point- and spot-light shadows are not implemented. `ShadowTint` is
an authored toon-lighting option: it is serialized, sent through the shared
material constants, applied to shadowed directional lighting, and exposed as
`Tools > Lighting > Toon lighting > Shadow tint`.

The remaining cutout-shadow, isolated-outline, project-HDR, save/reload/cook,
hot-reload, broader visual and profiling receipts are **non-blocking follow-up
work**. They remain documented below so evidence is not overstated, but they
do not keep R7 open.

## Roadmap review: proposed priorities, not approved milestones

The source audit found that the highest value after R7 is gameplay/editor
integration rather than another rendering program. The following shortlist is
recommendation only; it is not an approved replacement roadmap:

1. Finish R2 editor UX qualification and workflow reliability: qualify the
   already-built editor surface with real task sessions, restart/DPI,
   cross-window routing, save/cook/Play recovery and unsaved-preview latency.
2. Complete M8 inventory/equipment integration: connect the verified
   persistent-profile inventory and mask-slot contracts to fresh Play,
   starting loadouts, atomic live equipment, pickups, ledger/grant flow and
   the existing UiService inventory surface.
3. Deliver a playable combat vertical slice: close the gaps between the
   existing native action/ability/effect foundations and an authored Ashen
   Roots encounter, including graph/socket authoring, hit presentation,
   status/motor integration, prediction/correction and usable runtime UI.
4. Make encounters and NPC workflows production-usable: finish authoring and
   runtime integration for behavior, sensing, spawn/encounter state, triggers,
   interaction and navigation with a small playable level receipt.
5. Close multiplayer development qualification: cross-machine transport,
   controlled latency/loss cases, lifecycle/streaming behavior and EOS
   qualification only when the required external identities and deployment
   are available.
6. Add measured scene-scale performance only where needed: GPU instancing,
   batching, frustum/occlusion culling, mesh LOD and texture streaming should
   follow captured Ashen Roots workloads rather than a new SR program.
7. Keep selected rendering polish as small game-driven increments: advanced
   outlines, hit/status/dissolve, practical transparency, SSAO and LUT colour
   grading are candidates when a concrete scene or profiling result justifies
   them. Volumetric fog, atmospheric scattering, light shafts, GPU particles,
   temporal reconstruction and indirect rendering remain conditional, not
   commitments.

The former SR0–SR7 initiative is therefore retired as a standalone schedule.
Its useful research remains historical reference; existing R7 functionality
must not be reimplemented under SR names.

## Current engine capability assessment

The repository audit separates capability from roadmap intent:

- **Implemented and locally verified foundations:** AssetService import/cook/CAS
  and registry paths; MaterialDefinition/MaterialInstance editing and
  previews; scene/place/inspector/Lighting/Assets authoring; WorldSession and
  CharacterSceneSession foundations; movement profiles; native ability/effect,
  tag and combat-kit contracts; cooked-document `UiService`; audio and VFX
  services; and the R7 renderer/material stack. These are the systems to reuse.
- **Implemented but incomplete or qualification-limited:** R2 still needs
  end-to-end UX qualification; M8 inventory/equipment and mask-slot runtime
  integration is incomplete; M4/M5 combat and multiplayer gates remain
  partial; M6 NPC/encounter integration is not production-usable; navigation
  authoring exists but tiled streaming is not complete; and networking has
  same-machine evidence but not cross-machine/EOS qualification.
- **Present but not yet a dependable production workflow:** encounter/NPC
  authoring, inventory/equipment setup, packaged standalone game delivery,
  cross-machine network testing, and performance diagnostics. These are larger
  production bottlenecks than additional renderer features.
- **Planned but not approved as the next schedule:** World Puzzle UI and
  volumetric terrain/world authoring remain candidates in the broader roadmap;
  GPU instancing phase 6, navigation/world streaming and EOS work require
  concrete workload or deployment evidence before scheduling. The removed
  rendering systems and the retired SR0–SR7 program are not candidates.

This assessment is a planning boundary, not a claim that every listed
foundation has passed a complete production-release qualification matrix.

## 17 October stabilization and RoyalCombat visual receipt

This increment stabilizes the existing RoyalCombat scene without introducing a
parallel scene, material, lightmap, or play-session format.

Root causes fixed:

- The cascaded-shadow crash was an incompatible Diligent root-table commit:
  depth-only shadow PSOs were used with a forward-material SRB, while the
  opaque path also bound a zero-render-target PSO with a dummy colour target.
  D3D12 symbolization resolved the access violation to
  `PipelineResourceSignatureD3D12Impl::CommitRootTables`. Shadow PSOs now own
  compatible static/skinned/alpha SRBs and the pass binds matching D32 plus
  dummy-RGBA8 targets. The static diagnostic now runs on D3D12 and Vulkan.
- GPU cluster lookup treated `SV_POSITION` as NDC and compute assignment did
  not reject depth-slice misses. Lookup now uses the actual render-target
  dimensions, depth overlap is explicit, and CPU/GPU light bounds use nearest
  sphere depth. GPU local shading applies authored strength, normals, toon
  response and transformed spot direction, removing the vertical spotlight
  cluster seam.
- Vulkan shadow receivers used the opposite comparison convention for this
  Diligent depth path. The shader selects the backend convention through frame
  constants; Vulkan also uses an explicit comparison/clamp sampler. Cascade
  selection now uses linear view-space depth.

RoyalCombat is authored with four enabled 1024-resolution cascades, 96-unit
distance, explicit bias/split controls, and a shadow-casting main light. Warm
and cool point lights plus the overhead spot remain ordinary authored lights;
their world positions, colours, ranges, cones and falloff reach the GPU path.
Real-time lighting uses positions and normals, not baked lightmap UVs. Toon
ambient/shadow values were lifted for readable shaded surfaces, and the
existing Lighting-window outline controls were tuned to a visible plum edge.
The play-session binding behavior from `d8c39be` remains preserved.

Inspected actual-engine receipts are in `artifacts/r7/visual_polish/`:

- `royalcombat_wide_final_d3d12.png`
- `royalcombat_close_tuned_sampler_d3d12.png`
- `royalcombat_close_final_vulkan.png`
- `royalcombat_close_tuned_local_lights_off_d3d12.png`
- `royalcombat_close_outlines_off_d3d12.png`
- `shadow_regression_d3d12.png` and `shadow_regression_vulkan.png`

`python scripts/verify_r7_shadow_regression.py --model <terrain-id>` passed on
both backends with exit 0 and foreground receipts of 412389 and 419596 pixels.
The guarded editor rebuild passed, and CTest
`R5.materials|R7.rendering_core` passed 2/2. Vulkan validation layers were not
available on this machine, but the Vulkan backend executed successfully.

## 17 October Unity-look environment grade pass

The RoyalCombat environment was tuned against the supplied Unity reference:
dark brown upper sky, amber horizon haze, low warm sunlight, olive-green ground,
and readable orange/cream toon architecture. The existing scene rendering
component remains the source of truth; the values are editable through Tools >
Lighting and the directional-light Inspector transform.

One directly relevant renderer defect was found while tuning fog. The authored
fog start/end values are world-space distances, but the pixel shader compared
them to normalized clip-space depth. Linear fog therefore stayed disabled for
ordinary scene distances. The shader now uses `length(ViewPosition)`, keeping
the existing authored contract and making distance fog respond to the Lighting
window values. The main light transform was also moved to a lower late-day
angle, and RoyalCombat sky, fog, ambient, exposure, contrast, sun colour and
terrain tint were adjusted without adding a scene-specific shader path.

New actual-engine receipts:

- `artifacts/r7/visual_polish/royalcombat_unity_grade_d3d12.png`
- `artifacts/r7/visual_polish/royalcombat_unity_grade_vulkan.png`
- `artifacts/r7/visual_polish/royalcombat_unity_grade_wide_d3d12.png`

Both backends rendered 30 frames successfully with 76,789 foreground pixels;
the inspected images match across backends. `EditorCli validate` reports the
authored RoyalCombat document valid with 10 objects. Focused CTest
`R5.materials|R7.rendering_core` remains 2/2 passed. The full save/reload/cook
receipt and an interactive Lighting-window edit receipt remain separate
qualification work.

## 17 October Lighting UI and colour grading increment

Tools > Lighting now exposes the previously hidden scene-authored controls:
Fog Height and Height falloff, Exposure compensation, and Colour tint. Bloom
soft threshold and radius are also exposed because they were already part of
the post contract but had no editor path. Post tint is now serialized instead
of being silently reset on scene reload.

The existing post pass now supports neutral-by-default lift, gamma, gain and
temperature controls. They are stored in the same schema-1 `post` component,
uploaded in the existing `PostConstants` buffer, and applied after exposure,
tone mapping, contrast and saturation but before tint/FXAA. This adds artist
control without adding a second grading system or changing the default image.
The CPU reference helper and `R7.rendering_core` receipt cover neutral grading,
temperature/lift response and height fog attenuation.

The actual RoyalCombat post pass rendered successfully for 30 hidden frames on
D3D12 and Vulkan after the cbuffer/shader contract change. Captures are
`artifacts/r7/visual_polish/royalcombat_post_grade_d3d12.png` and
`royalcombat_post_grade_vulkan.png`; both report 76,789 foreground pixels and
were inspected. The captures intentionally use neutral new grading defaults;
the new controls are ready for the next authored Unity-look tuning pass.

## Architecture used

The renderer keeps the existing Diligent device/context, scene viewport ownership, cooked model/material resources, GPU skinning path, Place/Inspector transaction path, and `ObjectData::optional_json` preservation. R7 scene-authored lighting and rendering settings are ordinary optional components, so they follow existing selection, undo/redo, save/load, prefab and cooking routes. No parallel scene or material format was introduced.

The rendering core is game-agnostic (`engine/runtime/rendering.*`). It provides stable-ID scene lights, toon parameters, fog/post settings, a deterministic CPU light-cluster reference, bounded 64-candidate capacity, and a four-light shading budget. Directional lights are kept separate from local-light selection. The primary viewport prepares the CPU reference and GPU compute assignment per frame; the static/skinned graphics shaders consume the GPU selected-index and packed-light buffers when GPU clustered mode is enabled. The CPU path remains available for independent deterministic tests. GPU occupancy/overflow counters are retained through the existing asynchronous readback path.

## Implemented in this checkpoint

- R7 scene light components: Directional, Point and Spot, with stable scene identity, authored defaults, Inspector editing and Place-panel creation under Lighting.
- Tools > Lighting: sky/ambient settings, main directional-light selection, toon bands, fog and post settings. Scene settings and light components use schema 1 JSON and stable references. Fog height/falloff, exposure compensation, post tint, bloom threshold/softness/radius, and lift/gamma/gain/temperature are editable and persisted.
- Shared toon constants and lighting evaluation for the existing static/skinned material pipelines, including deterministic local-light ordering, material-specific toon overrides, object-space triplanar base sampling, character hit/status/dissolve controls, terrain layer sampling/blending controls, emission and alpha modes.
- Registered and cooked material families: `toon-standard-v1`, `toon-character-v1`, and `toon-terrain-v1`. Existing `lit-color-v1` and `lit-surface-v1` contracts remain compatible.
- HDR viewport accumulation in `RGBA16_FLOAT` targets and a separate display target. The post stage performs exposure, neutral tone mapping, contrast/saturation, lift/gamma/gain/temperature/tint grading, bounded single-pass bloom sampling, and FXAA before ImGui composition.
- Real-time main directional shadows: bounded one-to-four D32 cascades, configurable 512/1024/2048/4096 resolution, authored distance, depth/normal bias and split distribution, static/skinned caster PSOs, texel-snapped light-space projections, and 3x3 comparison-PCF receiver sampling. The four-slice resource array safely disables unused slices by the authored count. Transparent surfaces remain conservatively excluded from the opaque shadow map.
- Texture-aware alpha-tested shadow casters: cutout materials use a shared static/skinned material-shader variant against a depth-plus-dummy-colour cascade target, sample the authoritative Albedo binding and compare the authored alpha cutoff; transparent surfaces remain excluded.
- GPU clustered-light assignment: Diligent compute resources own packed view-space lights, fixed-capacity candidate/selected index buffers, per-cluster occupancy and GPU overflow counters, and one-frame-late non-blocking staging readback. GPU light order is deterministic influence-descending/StableId tie-broken; the CPU result remains an independent reference.
- GPU clustered-light shading: the selected-index and packed-light SRVs are bound to the static and skinned pixel shader resource contract. Shader cluster lookup uses view-space depth and screen tiles, clamps to the authored candidate/shading budgets, and falls back to the CPU payload only when GPU mode is disabled. D3D12/Vulkan validation scenes execute this path.
- Primary-view local-light selection now uses the per-view cluster assignment instead of scene insertion order. Candidate capacity and four-light shading budget are distinct, stable-ID tie-breaking is deterministic, and occupancy/overflow statistics are retained on `SceneViewport` for later GPU-stat/debug presentation.
- Spotlights now carry their transformed direction plus inner/outer cone and falloff parameters into the shared four-light toon payload; they are no longer shaded as unrestricted point lights.
- Initial outlines are now a registered pre-FXAA composition stage: the existing opaque/cutout material passes can emit encoded visible normals and stable object-group IDs into an isolated per-view target, and the post shader combines depth, normal and group discontinuities with configurable thickness, sensitivity and distance fade. Transparent surfaces are intentionally excluded until their depth/ID policy is defined.
- Scene-authored sky color and linear/exponential distance fog. Fog is applied in the material pass so transparent objects are not fogged again in the post stage.
- Saved procedural sky recipe fields (zenith, horizon, ground, sun direction/size/colour/intensity, cloud amount and deterministic seed) now persist with the scene profile and render as a bounded HDR background gradient/sun disk in the post stage. The procedural sun is environment-only; the selected Main Light remains authoritative for toon lighting and shadows.
- HDR sky asset path: `.hdr` and RGBA16_FLOAT `.dds` sources are accepted by the existing AssetService/importer/cook/package flow. Equirectangular data is converted to linear RGBA16_FLOAT mip data; six-layer DDS payloads are uploaded as cube resources. The Lighting window exposes asset selection, intensity, rotation, and procedural-vs-imported selection; the post shader samples the selected asset.
- Repeatable validation content: `content/royal_district/R7Validation.dascene` contains authored terrain/huts, a skinned character, directional/point/spot lights, clustered settings, cascades, sky, fog, outlines, bloom, tone/exposure, and toon material usage. `EditorCli` validates eight objects with fingerprint `d15c86f577fe58440aeaa6d4d986093803567198ae49ffa8c005a1f24a24b183`.
- Expanded resource bindings and shader feature bits are bounded; float/color edits do not create new PSO keys.

## Verification receipts

- `R7RenderingTests.exe`: passed with the shared CPU/GPU cluster-layout contract assertion; deterministic cluster case reports 5,760 clusters, 2,856 non-empty clusters and 2,448 bounded overflows in the constrained case.
- `MaterialTests.exe`: passed; includes Standard toon, Character visual-state and Terrain layer/blend cooking checks.
- CTest `R7.rendering_core`: passed.
- `DarkAngelEditor` guarded target build: passed with the pinned MSVC/CMake environment and `VCPKG_MANIFEST_INSTALL=OFF`.
- `scripts/verify_scene_navigation.py --gate place`: passed on D3D12 after the HDR/post/fog shader changes. The gate exercised DXC and the post-process pipeline and wrote a foreground capture.
- `scripts/verify_royal_renderer.py`: passed after correcting the hidden-renderer camera boundary. Royal document load, D3D12/Vulkan process execution, visible rest/attack captures, GPU/reference image parity, and the editor regression all passed. The deterministic `--camera-close` view is now preserved for hidden renderer runs while the R1 scene-navigation exercise still uses its authored matrices.
- `scripts/verify_materials.py --gate materials`: passed on D3D12 after the shared material shader/binding changes, including cutout and transparent authoring paths.
- `scripts/verify_royal_renderer.py`: passed after the GPU cluster and alpha-shadow PSO changes on D3D12 and Vulkan; rest/attack GPU/reference comparisons remained exact and visible attack deltas remained qualified.
- `ctest --test-dir build/m5-editor-relwithdebinfo -R 'R5.materials|R7.rendering_core'`: passed 2/2 after the HDR importer fixture and direct GPU shader changes.
- `EditorCli.exe validate content/royal_district/R7Validation.dascene`: passed; eight authored objects and the deterministic fingerprint above.
- R7 validation scene capture: eight hidden frames passed on D3D12 and Vulkan, each reporting `Viewport foreground pixels=221475`. The viewport crop comparison had maximum channel error 4, 212 pixels beyond error 2, and zero pixels beyond error 8. Captures are `evidence/r7-validation-d3d12.png` and `evidence/r7-validation-vulkan.png`.
- Direct `DarkAngelEditor` post-grade smoke capture: 30 hidden frames passed on D3D12 and Vulkan after the new post cbuffer contract; both reported `Viewport foreground pixels=76789` and produced inspected receipts under `artifacts/r7/visual_polish/`.

## Non-blocking follow-up and qualification limits

- Dedicated shadow visual qualification with an authored cutout caster is still required. The alpha-tested caster path is implemented and backend-exercised, but no isolated cutout-shadow image receipt exists yet.
- Isolated visual qualification of the object/group-ID plus depth/normal screen-space outlines and distance fading remains open; the R7 validation capture exercises the stage, but does not separate its contribution from the complete scene.
- HDR import/cook/upload is implemented and covered by a synthetic equirectangular asset test, but no project HDR source or cubemap is available for end-to-end visual qualification. The scene-profile procedural recipe is functional; its sun synchronization policy is explicit and intentionally does not override the selected Main Light.
- Full save/reload/cook/run parity for the new validation scene and material hot-reload qualification remain open. The scene is source-valid and runs from the existing cooked Royal registry, but a dedicated persisted/cooked receipt has not been produced.
- GPU timings remain open. A Diligent duration-query attempt was removed after both asynchronous and synchronized D3D12 readback crashed the editor; no GPU milliseconds are claimed. Current captures provide foreground-pixel counts and device allocation/query-manager diagnostics only.
- Royal Temple visual qualification remains scoped to the deterministic R7 scene plus the existing Royal rest/attack parity gate. Broader artist-facing quality review and a dedicated foliage/cutout asset are still required.
- Advanced AO is not enabled; SSAO and LUT grading remain optional future candidates. Baked GI, lightmaps, probes, advanced GI, hardware ray tracing, SSR/SSGI, motion blur, depth of field and HDR10 are out of scope and not planned.

The D3D12 capture is evidence that the modified pipeline executes; it is not evidence that the final R7 visuals or all material families are artistically qualified.
