# Skyboxes, procedural skies, lighting, fog and post processing

**Status: R7 accepted and complete for the current development phase (17
October 2026).** This document is retained as the architectural reference for
the implemented environment/lighting/post contracts and historical decisions.
It is no longer a schedule for additional rendering milestones. Future work
must be proposed as small game-driven increments; existing R7 functionality
must not be reimplemented under a new rendering initiative.

## Rendering decisions (16 October 2026)

The user decided the following; they resolve most of the open questions further down. Details are also recorded under DAE-005 in the [Decision Log](../../architecture/Decision_Log.txt).

| Topic | Decision |
| --- | --- |
| Baked GI / lightmaps | **Out of scope and not planned.** Levels use real-time lighting, dynamic shadows and ambient contribution. No baker, lightmap authoring workflow, probe dependency or precomputed-data requirement is part of the Ashen Roots engine plan. The old bake research remains historical only. |
| Renderer architecture | Clustered Forward+ on the existing Diligent abstraction. Directional, point and spot lights; multiple coloured dynamic lights with intensity, range, colour and attenuation; compute light assignment where justified. General-purpose and shading-model agnostic; no full PBR/deferred renderer. D3D12 and Vulkan. Inspect current infrastructure first and document a migration path instead of rewriting. |
| Shadows | Real-time shadow maps; cascaded shadow maps for the primary directional light with configurable distance, resolution, bias and cascades. |
| Ambient / AO | Ambient/environment contribution is implemented. Lightweight SSAO is a future candidate only if a concrete scene need and profile justify it; it is not an R7 gate. No SSGI, ray tracing or advanced GI is planned. |
| Fog | Distance fog is baseline: colour, density, start/end, linear and exponential modes; height fog when straightforward. Integrates with depth, lighting and transparency. Volumetric fog and epipolar scattering are not planned unless a concrete game requirement later justifies a separate proposal. |
| Post stack | HDR, tone mapping, exposure, colour grading, bloom, FXAA, fog and outline composition are implemented. LUT grading is a future candidate only when a concrete art workflow needs it. Motion blur, DOF, advanced volumetrics, SSR, SSGI, ray tracing, temporal reconstruction and HDR10 are not planned. |
| Materials | MaterialDefinition, MaterialInstance, per-renderable runtime parameters, bounded variants, cached PSOs, opaque/alpha-cutout/transparent states, slots, prefab overrides, cooking, previews and shader hot reload are existing foundations. Standard/Character/Terrain toon families are already part of R7; they are not SR deliverables. |
| Outlines | Object/group ID plus depth and normal discontinuity outlines; per-object enable, colour, thickness and grouping; thickness/visibility scaled by distance or projected size. Inverted hull only as a later override. Compatible with characters, terrain, transparency, dissolve and AA. |
| R7 versus SR | R7 is accepted as the reusable environment, lighting, shadow, material and post foundation. The former SR0–SR7 initiative is retired as a standalone roadmap; future polish is selected individually from game value, visual need or profiling evidence. |
| Validation | A repeatable test scene with dynamic lights, shadows, skinned characters, terrain, transparency and post processing; D3D12 and Vulkan where available. |

### Historical R7 implementation refinement (16 October 2026)

The following records the implementation decisions that led to the now-accepted
R7 result. It is retained for traceability, not as an authorization for a new
R7 phase or for the removed rendering systems.

This refinement supersedes conflicting deferrals and open choices below. It is implementation authorization after R4, R5 A2/A3 and R6 U0/U2, not implementation evidence.

- Clustered Forward+ starts with tunable 16x16 pixel tiles, 24 logarithmic depth slices and 64 local lights per cluster; directional light stays separate. Prefer compute assignment within the existing renderer. Bounds checks, deterministic overflow selection, counters and occupancy/overflow views are required.
- Prefer RGBA16_FLOAT scene color, existing depth conventions and explicit linear/HDR accumulation followed by SDR display conversion. Qualify sRGB sampling, exposure, tone mapping and output gamma together. HDR10 is out of scope.
- Bring a minimal shared HLSL toon library into R7: base color/texture, main-directional three-band shading with configurable thresholds/softness/shadow tint, colored point/spot additions, ambient color/strength and real shadow attenuation. Emission follows qualified R5 support. The directional light establishes bands; local lights add stylized energy without repeatedly quantizing the combined lighting. AO affects ambient/indirect only. Expose parameters through `.damaterial`; ordinary numeric edits must not create shader variants. Reuse the existing functioning skinning path.
- Start directional shadows with four 2048x2048 cascades, practical logarithmic/linear splits, texel snapping, configurable distance/depth/normal bias and baseline 3x3 PCF. Cascade count, resolution and transitions are tunable. Prioritize directional shadows; a small shadowed-spot budget may follow if practical. Unrestricted point cubemaps remain deferred.
- Require enabled/color/start/end/density controls and linear/exponential distance fog. Share evaluation across opaque and transparent draws, account for sky, and fog transparency exactly once. Height fog is optional.
- Prefer exact-version DiligentFX Bloom, tone-mapping utilities and useful ShadowMapManager/cascade utilities behind engine-owned adapters. Inspect APIs, build dependencies and notices before integration; do not import the complete PBR renderer. SSAO/PostFXContext must have valid required depth, normal and history inputs; never fabricate temporal resources. Document a spatial-only configuration or defer SSAO if prerequisites are substantial. Epipolar scattering remains deferred.
- Use manual exposure, neutral controllable tone mapping, exposure compensation/contrast/saturation/tint and practical shadow/highlight grading; LUT is optional. Bloom runs in HDR before tone mapping, exposing enable/threshold/intensity/soft threshold/radius. Initial AA is FXAA after LDR composition and before UI; no TAA or multiple AA modes.
- Resource contracts for future hybrid outlines use depth, efficiently encoded normals and integer object/group IDs, with explicit resize/lifetime ownership. Groups join character submeshes. Future distance/projected-size attenuation suppresses tiny objects. Basic composition may land only if it fits the approved buffers/scope; no full deferred G-buffer or inverted hull system.
- Logical dependencies: shadows, required geometry buffers, clusters, eligible AO, opaque Forward+ toon, sky/fog, transparent/VFX, HDR bloom, tone mapping/grading, available outlines, FXAA, UI. Actual pass order follows correctness and the existing graph. Disabled effects avoid unnecessary allocations/execution.
- Validate shader compilation/variant resolution, material cook/save/reload/editor persistence, cluster bounds/overflow, cascade stability, optional effects, fog and resource resizing on D3D12/Vulkan. A repeatable scene includes static and supported animated skin meshes, colored point/spot lights, directional shadows and receivers, visible toon bands, fog, emission/bloom, a transparent object and post controls. Record captures, GPU timings/statistics where available; compilation alone is not visual qualification.

Full Character/Entity/Terrain families, terrain layering/triplanar, rim/specular, status/hit/dissolve effects, advanced outlines, GPU-driven/advanced instancing and atmosphere are historical research topics or individually proposed candidates, not an SR schedule. Baked GI/lightmaps and bake dependencies are out of scope and not planned. Add no Embree/OIDN/Cycles/RadeonRays/lightmap/probe dependency.

Follow-up decisions left to implementation: exact SSAO configuration and prerequisites, local-shadow allocation/budget, total light budgets, and exact-version DiligentFX component compatibility/licensing and adapter choices.

15 October 2026. **Historical research and future-work design only. No renderer features, assets, tools, dependencies or bake backend added.** Extends the [authoring handoff](AUTHORING_NEXT_STEPS_2026-10-15.md) at the user's request. Current roadmap decisions supersede the old bake/probe proposals: they are out of scope and not planned for Ashen Roots.

## Authoring locations and ownership

Add **Tools > Lighting**, opening a tool with these sections: **Environment**, **Lights & Shadows**, **Fog**, **Baking**, and **Post Processing**. It edits the current scene's rendering references and their owning assets. Add **Tools > Procedural Skybox**, which creates/edits a reusable sky recipe and can assign its saved result to the scene. Both tools can also be reached with **Edit** from Assets.

Every section shows the selected profile/source, whether the edit is shared, the current scene assignment, validation and dirty state. **Duplicate and Assign** makes a scene-specific profile. Existing scene/asset Undo, Save and candidate publication own changes; a tool window is not a hidden second settings store.

| Data | Proposed source of truth | Scope |
| --- | --- | --- |
| Imported cubemap/spheremap or procedural sky recipe | Native `SkyDefinition`, proposed `.dasky` under `content/environment/skies/` | Reusable asset |
| Sky reference, ambient/reflection intensity, shadow quality defaults, global fog | `LightingProfile`, proposed `.dalighting` | Reusable asset selected by scene |
| Direct light position/type/color/intensity/range and shadow enable | Typed scene Light component | Scene object, optionally a prefab default/instance override |
| Scene-wide lighting/post/bake profile references | Versioned `SceneRenderingSettings` in scene source | Scene-owned, frozen with prepared render resources |
| Bake parameters | `LightingBakeSettings`, proposed `.dalightbake` | Reusable authoring recipe |
| Lightmaps, probe captures and object/atlas bindings | Immutable scene-specific derived lighting products | Cooker/CAS results, never hand-edited source |
| Post-processing settings | `PostProcessProfile`, proposed `.dapostprocess` | Scene default, optional explicit camera override |
| Preview exposure/quality overrides | Tool preview/local preferences | Clearly labelled and not saved into the scene accidentally |

Names/extensions above are proposals to finalize with AssetService conventions. Keep direct-light component data distinct from global profile settings. A Player camera may reference a presentation override; Player/NPC gameplay loadouts do not own global lighting. A project game definition may supply defaults, but the selected scene's saved references determine its rendered environment. Headless hosts do not initialize these presentation services.

## Current source boundary

Inspection: `apps/editor/render/{gpu_resources,scene_viewport,scene_resources}`, `engine/assets/{gltf_cooker,asset_service,external_import}`, `engine/assets/include/darkangel/assets.hpp`, renderer shader sources and DAE-005.

- The renderer uploads a 2D sRGB RGBA8 base-color texture and simple color/roughness/metallic/double-sided material data. The current cooked texture structure holds 2D RGBA8 mips; this is not a float HDR/cubemap loader.
- Scene viewport rendering clears a background color. There is no authored SkyDefinition, general light/fog editor, lightmap/probe bake path or profile-driven post stack in this source path.
- Mesh `Vertex` has one UV set; lightmap UVs/atlas bindings need a versioned mesh/product extension.
- DiligentCore and DiligentTools are present. DiligentFX is not integrated by the inspected build/dependency configuration. Its upstream examples are research references, not proof of features in the pinned checkout.
- DAE-005 targets clustered forward, linear HDR, an explicit render graph, one output conversion, directional shadows plus bounded local lights, versioned materials and deferred lightmaps/probes. The current proof renderer must be extended incrementally toward that contract; copying a deferred-renderer tutorial does not change it.

## Cubemap and spheremap skyboxes

Support these author-facing sources through one SkyDefinition:

1. **Cubemap:** six explicitly labelled equal-size square faces (`+X/-X/+Y/-Y/+Z/-Z`) or a validated cube texture container. An orientation preview catches flipped faces before publication.
2. **Spheremap / panorama:** a 360-degree latitude/longitude **equirectangular** image, normally 2:1, with explicit mapping metadata. This is the default meaning of spheremap in the UI. Do not infer projection solely from filename or dimensions.
3. **Procedural:** a saved parameter recipe evaluated by the generator below.

Blender's environment documentation distinguishes latitude/longitude mapping from mirror-ball mapping. Adopt an explicit projection selector rather than treating them as interchangeable. Mirror-ball image conversion can follow as a separately labelled importer; it must not invent missing directional coverage. [Blender World Environment](https://docs.blender.org/manual/en/latest/render/lights/world.html)

LDR color inputs may use the existing image import codecs; add a distinct HDR import profile for floating-point radiance, initially a qualified Radiance HDR/float DDS path. EXR requires an explicitly qualified codec and can follow. Validate container type, face/mip count, dimensions, finite values, limits and color metadata. Never force HDR through the current sRGB RGBA8 `CookedTexture` path or clamp it to display range during import.

Cook all supported source projections to a canonical cube orientation and a typed linear-HDR cube product. Generate the separate diffuse irradiance and roughness-prefiltered specular products needed by the selected material families; ordinary downsampled mips are not a substitute for specular prefiltering. Record source, projection, rotation, filter settings and converter version in dependencies/fingerprints. DiligentFX's PBR renderer documents image-based lighting and environment-map processing worth evaluating behind the existing renderer boundary. [DiligentFX PBR](https://github.com/DiligentGraphics/DiligentFX/tree/master/PBR)

**Background** and **environment lighting** are separate toggles with explicit intensity controls. A sky can be hidden while continuing to illuminate/reflect, and a decorative background can be shown without contributing light. Use the same authored orientation for related background/irradiance/specular data. Camera exposure belongs to post processing; do not add a second automatic exposure control in SkyDefinition.

Render the background at infinity with camera rotation and no camera translation, correct scene depth handling and no gameplay collision. Test cardinal-face labels, poles, seam continuity, camera translation, transparent objects, resize and both backends. Do not put a huge ordinary gameplay sphere into every scene as the sky implementation.

## Tools > Procedural Skybox

The first generator is a practical stylized recipe: zenith/horizon/ground colors and falloff, sun direction/angular size/color/intensity, optional simple cloud layer parameters and deterministic seed. Add seeded stars/night gradient after the basic gradient/sun workflow. Physical atmosphere/turbidity/aerial perspective, volumetric clouds, weather and live time-of-day are later integrations, not hidden claims of the first tool.

Controls include a low-resolution interactive preview, cube-face/panorama preview, final face resolution/quality, **Generate**, **Cancel**, **Save Sky**, **Assign to Scene** and optional export as panorama or six faces. All generated skies become ordinary SkyDefinition assets usable in Assets, Lighting and scene references. Keep editable recipes; a generated image alone is not the surviving source of truth.

Generation uses the same radiance evaluator for preview and final cube capture. Store recipe/seed/generator version and chosen numeric backend provenance; fixed inputs give a reproducible recipe, but do not promise byte-identical GPU results across vendors. Bound resolution/memory/work, stage jobs against a source revision, discard stale/cancelled results and publish the new immutable result only after validation. Failed jobs leave the previous sky available. Reuse cooker/job-service interfaces rather than starting a second asset generation pipeline.

A procedural sun disk does not automatically create another directional light. A scene Sun light may explicitly **Follow Sky Sun** for direction/color, with a visible binding to the recipe, or use manual settings. The resolved scene has one authoritative presentation value for each linked parameter. Imported panoramas have no inferred analytic Sun unless the author supplies one. This avoids doubled illumination and mismatched sky/shadow directions.

Upstream DiligentFX includes atmospheric light-scattering work; evaluate compatibility/cost as a later atmosphere adapter against the pinned core rather than adopting an unpinned renderer subsystem. [DiligentFX components](https://github.com/DiligentGraphics/DiligentFX)

## Tools > Lighting: light, shadow and fog controls

**Environment:** sky selection and Edit/Generate shortcuts; background/environment participation, ambient/hemisphere tint/intensity, reflection intensity and environment preview. Prefer a neutral material-preview environment in Assets, with an explicit **Use Current Scene** option.

**Lights & Shadows:** a list linking to scene light objects; add/select directional, point and spot lights as their renderer support lands; intensity/color/temperature where supported, range and cone, cast/receive policy, directional shadow distance/resolution/cascades/bias and visible budgets. Start within DAE-005: one main directional shadow path, bounded unshadowed local lights, then budgeted spot shadows. Do not expose shadow controls that silently do nothing.

**Fog:** enable, color, distance density, height reference/falloff and start/max distance where supported. First implement inexpensive global analytic distance/height fog from the view's depth/world position, with defined sky/horizon and transparency/VFX policy. Fog remains presentation, not a gameplay visibility/sensing rule. Local fog volumes, volumetric scattering, shafts and weather are later features. Each fog term has one owning field; the procedural sky horizon gradient is not a second unnamed fog system.

**Diagnostics:** effective source values, light/shadow/texture counts, dropped/overflow budget diagnostics, render-pass timing and source/bake freshness. Scene, Game and previews use the same render-resource definitions. Runtime lighting scripts later select or blend validated profiles through presentation APIs; they do not mutate GPU objects or editor sources.

## Post-processing stack

Expose a dedicated **Post Processing** section in Lighting and **Edit** on its profile in Assets. Show effect enable, parameters, inherited/overridden source, preview bypass and a comparison view. An optional camera-profile override uses explicit field masks; it does not silently replace unrelated scene values. Local quality/accessibility preferences apply permitted presentation overrides without rewriting authored assets. Local blend volumes and their priority/weight policy follow after scene/camera selection works.

The stack is a list of **registered effect types with legal render stages**, not arbitrary shader execution or unrestricted drag ordering. The render graph owns required resources and stage constraints; reject cyclic/invalid order. Present a readable final pass order with effect dependencies and cost. Schema/commands may be future-ready, but unsupported effects are diagnosed and unavailable for production use until implemented.

Start with explicit manual exposure and one qualified tone-mapping/output transform; add basic color adjustment and bloom once HDR intermediates exist. Later add LUT grading, vignette, optional spatial AA, depth of field, SSAO and SSR where required inputs/quality are implemented. Keep outlines/fog at their correct depth/normal/HDR stages. TAA/motion blur need motion vectors, history and invalidation and remain deferred under DAE-005.

```text
view/depth/normal preparation -> any supported pre-lighting effects
 -> HDR scene lighting + sky + transparency/fog in their declared passes
 -> supported HDR effects (for example bloom)
 -> exposure / grading / tone mapping / output conversion in one color pipeline
 -> approved display-stage effects / spatial AA
 -> screen-space game UI and editor overlays -> present
```

Choose exact grading/AA placement with the selected implementations; do not apply output gamma twice. World UI is scene presentation, while HUD/menu UI stays after scene tone mapping under DAE-010. Per-view targets/history must reset on resize, camera cut, scene change and incompatible profile/resource changes. Preview overrides cannot leak to another viewport or packaged content.

DiligentFX offers reusable post components, but its Tutorial27 uses a deferred example and previous-frame resources. Reuse appropriate components after verifying formats, depth conventions, motion/history inputs and compatible pins; this is not a proposal to replace DarkAngel's planned forward renderer. [Diligent Tutorial27](https://diligentgraphics.github.io/docs/d7/de9/DiligentSamples_Tutorials_Tutorial27_PostProcessing_readme.html)

## Light baking: tool and actual runtime support

Environment cube generation/filtering, reflection capture and **scene diffuse-light baking are distinct jobs** with separate output/freshness status. Do not label filtered environment maps or AO-only output as complete scene GI.

The Baking section should configure reusable settings and show **Not supported / Needs bake / Baking / Current / Stale / Failed** accurately. A Bake button stays unavailable with a concrete backend/dependency reason until a real adapter exists. Full future scope includes:

- Lightmap texel density, atlas dimensions/count/memory limit, UV padding/dilation, input UV channel or generated unwrap, and per-object density scale/static participation.
- Direct/environment/indirect sample budgets, bounce limit, ray offset, optional bounded AO, filtering/denoising, output precision/compression and Preview/Production quality presets, restricted to the chosen backend's supported features.
- Bake selected scene/scope, progress, Cancel, logs, texel/UV charts, overlap/invalid-UV diagnostics and before/after comparison; view authored settings separately from generated results.
- Environment/reflection probe capture quality, bounds, inclusion masks and generation/freshness, with dynamic-actor diffuse probes as a later qualified path.

Use Unity's lightmapping settings only as an authoring reference for samples, texel density, padding and atlas controls; they do not supply a DarkAngel baker. xatlas can generate/pack secondary UVs and supports progress/cancellation, but is **not a GI solver**. A bake-backend spike must qualify tracing/transport, denoising, Windows/offline execution, pinned dependencies, bounded work, licensing and fidelity to the supported material model before selecting it. No new dependency is installed by this plan. [Unity lightmap settings](https://docs.unity3d.com/6000.0/Documentation/Manual/Lightmaps-reference.html), [xatlas](https://github.com/jpcy/xatlas)

Recommended first real bake is **static diffuse indirect lighting**, with realtime direct light/shadows and environment specular. For static receivers, baked indirect replaces the matching ambient/diffuse-IBL contribution rather than adding it twice; dynamic characters initially retain the qualified realtime ambient/environment path. Store incident diffuse transport or an explicitly documented compatible result, not camera exposure, tone mapping, final toon bands or screen fog in an albedo texture. Baked direct lighting, mixed-light shadow masks and dynamic probes need separate energy/occlusion contracts before exposure in the UI.

This needs secondary lightmap UV/product support, static object bindings/atlas scale-offset, shader sampling and the baker. Import or generate non-overlapping UV1 without changing material UV0. Unwrapping may split render vertices; retain submesh/material provenance and do not regenerate gameplay object IDs or rebuild collision from changed render topology by accident. Lightmap output is per **scene placement**, not shared persistent state on a prefab. Two instances may share geometry but have different baked illumination.

Bake the same freshly resolved scene geometry/material/environment closure used by the renderer, not the coarser gameplay collision mesh. Include effective instance material bindings, alpha/caster rules, static transforms, sky/light/profile settings, UV generation and baker versions in the fingerprint. Scene geometry, prefab swap/move, material/texture, sky or light changes invalidate affected results. Post exposure/grading alone does not rebake incident lighting. Movable doors/platforms and skinned actors are excluded from static bake by default; their policy must not leave a permanent baked obstacle shadow.

Workers read an immutable candidate snapshot. Jobs never write source heads or replace active data while still running. Publish all atlas/probe products and scene bindings coherently through AssetService/CAS after checking the scene/dependency revisions; reject stale completion and retain prior output. A stale result is clearly marked in editor preview; production packaging either requires a current bake or an explicitly selected unbaked realtime policy, never a silent stale bake. Scene travel/reload acquires the appropriate frozen products; servers have no rendering/bake execution dependency.

## Delivery order and acceptance

| Step | Result | Required acceptance |
| --- | --- | --- |
| Render0 (formerly R0) | Versioned render profiles, texture semantics, linear HDR/view resources and Assets routing contracts | Legacy scenes/defaults retained; packaged load; clear unsupported/missing-asset diagnostics |
| Render1 (formerly R1) | Cubemap + equirectangular sky import, background/environment controls and Lighting tool | Cardinal/pole/seam tests, HDR retained, visible-sky and IBL independence, both backends |
| Render2 (formerly R2) | Procedural Skybox tool and simple global fog; sun linkage | Save/reopen/regenerate/assign; consistent cube/panorama output; cancellation/stale result retention; scene/Game agreement |
| Render3 (formerly R3) | Useful post stack and direct-light/shadow controls | Per-view isolation; HDR/color correctness; UI unaffected; invalid ordering and resize/history reset; supported feature budgets |
| Render4 (new, 16 October 2026) | Real-time lighting completion: clustered colored local lights, cascaded directional shadows, ambient light, SSAO | Coloured light accumulation, shadow controls, overflow diagnostics, AO limited to ambient, both backends |
| Historical / not planned | Bake settings/status tool, UV/backend spike and static indirect bake integration are retained only as design history, not as milestones. | No bake dependency or lightmap authoring contract is required by the current engine. |
| Later | Probes, local volumes, richer atmosphere/weather and temporal effects | Dynamic actors/doors/travel, quality/performance and resource-lifetime qualification |

Renderer-local steps are now labelled Render0–Render4 (old R0–R4 retained above for traceability), distinct from project R1–R8. Historically, Render0 was the foundation; Render1/Render2 plus Render3's direct-light/shadow controls mapped to 7.1; Render3's post stack mapped to 7.2; and Render4 mapped to 7.3. R7 is now accepted; this mapping is historical only, and baking is out of scope and not planned. Direct-light controls must not wait for post processing merely because they shared an older row. Material editing and render texture profiles from [Assets and materials](ASSETS_MATERIAL_EDITING_PLAN.md) are shared prerequisites. Follow the main roadmap rather than the former parallel rendering-stream suggestion; preserve active build/dependency state. Use focused cook/transaction tests plus a small reference scene (neutral swatches, chrome/rough spheres, alpha foliage, a cave/interior, moving character and UI). Broaden to D3D12/Vulkan when render graph/format/shader changes land; no multiplayer protocol matrix is needed for a numeric fog form. Track actual renderer timings only when a concrete performance question justifies measurement.

## Ashen Roots stylized rendering research (retired as a standalone roadmap)

**Status: Historical design reference; no SR0–SR7 schedule is approved.** R4–R6 establish source ownership, stable material assignments, MaterialDefinition/MaterialInstance contracts and preview/editor integration. R7 now provides the environment, lighting, material and post-process foundation. Retain the useful architecture notes, but propose future improvements individually from Ashen Roots gameplay, visual requirements or profiling. Do not create a large replacement rendering program from this section.

### Current capability boundary

The current renderer provides DiligentCore/DiligentTools-backed D3D12 and
Vulkan viewport rendering, HDR scene/display targets, procedural and imported
sky plumbing, authored Lighting controls, Standard/Character/Terrain toon
families, clustered coloured local lights, cascaded directional shadows,
outlines, fog, bloom, FXAA and exposure/colour grading. AssetService and the
existing material/scene cook paths remain authoritative. The remaining limits
are documented in the current R7 report; this historical plan is not evidence
for capabilities beyond those receipts. DiligentFX is not an approved
dependency, and xatlas is UV tooling rather than a GI solver.

### Historical target architecture

This section records the design direction that informed R7. It is not a
commitment to implement every listed feature.

The deferred target is a clustered Forward+ pipeline over DarkAngel's existing Diligent abstraction, render graph, resource lifetime, HLSL/DXC compilation, PSO cache and AssetService/cooker boundaries. D3D12 remains the first path and Vulkan remains an early and required validation path. The design must remain game-agnostic while using Ashen Roots' stylized third-person environments as the initial acceptance target; it must not become a PBR-centered architecture.

The shared surface library should support directional, point and spot lights, multiple colored dynamic lights, toon bands/thresholds/softness/shadow colors, ambient contribution, optional normal/specular/rim terms and controlled transparency. Compute clustered light assignment is preferred where it improves bounded local-light scaling, but a measured fallback is required for unsupported or small scenes. Integrate light lists, material parameters, render states and pass resources through the existing graph and resource manager rather than allowing materials to create ad-hoc GPU objects.

Material families are three schemas over shared lighting code:

- **Character:** base color/albedo, cel bands, colored lights, optional normal/emission/rim/specular, vertex color/tint, hit/status overlays, directional/noise dissolve with glowing edge, detail/tattoo masks, opaque/cutout/transparent modes, outline participation and GPU skinning. Runtime effects use per-character GPU parameters rather than material creation or shader recompilation.
- **Entity:** the shared standard surface features plus UV, world/object triplanar mapping, gradient/vertex blending, alpha modes, outlines and compatible static GPU instancing. Character-only hit/dissolve defaults are not required.
- **Terrain:** only a separate family if prototypes show a meaningful performance/architecture benefit; shared toon lighting plus surface layers, slope/height rules, world triplanar, vertex/splat blending, optional normals, distance detail, outlines and LOD-aware terrain inputs.

Feature-based variants, cached PSOs, separate opaque/cutout/transparent states, conditional texture sampling, shared definitions with per-instance parameters, material sorting, bindless resources where beneficial, static instancing and future instanced skinning are optimization directions. A variant budget, stable keys and fallback materials are required to prevent uncontrolled permutation growth.

### Retired SR phase map (historical, not a development order)

| Candidate class | Examples | Scheduling rule |
| --- | --- | --- |
| High-value game-driven polish | Material authoring improvements, advanced outlines, hit/status/dissolve effects, practical transparency, SSAO, LUT colour grading | Propose as a small vertical slice only when a real Ashen Roots scene or workflow demonstrates the need. Reuse R7 contracts; do not recreate SR1–SR5. |
| Measured production improvements | GPU instancing/batching, practical frustum/occlusion culling, mesh LOD and texture streaming | Require captured workload evidence, a bounded memory/variant budget and cold packaged-load checks before scheduling. |
| Conditional research | Indirect rendering, advanced GPU culling, volumetric fog, atmospheric scattering, light shafts, GPU particles and temporal reconstruction | Not commitments. Schedule only after a concrete visual/gameplay requirement and profiling justify the cost. |

### Outlines, fog, shadows and post-process decisions

Outlines remain a renderer-level composition feature, not geometry embedded in each material. Prototype object/group IDs, depth discontinuities and normal discontinuities; compare cost, grouping quality, transparency behavior, AA stability and distance scaling. The initial choice must prevent thick distant outlines and excessive foliage/terrain noise. Inverted hull remains a documented fallback, not the default.

Use cascaded shadow maps for the main directional light, explicit resolution/distance/bias/cascade controls and bounded local-light shadow budgets. Ambient occlusion should primarily affect indirect/ambient lighting rather than indiscriminately darkening toon bands. Distance fog supports linear/exponential density, color, start/end and optional height terms, with depth/atmospheric integration and editor controls. Post effects are legal registered graph stages with explicit resource dependencies; disabled effects do not allocate or execute their expensive path.

### Reuse and dependency research

Evaluate DiligentCore/DiligentTools for existing device, shader, PSO, resource, render-target and tooling contracts. Evaluate DiligentFX PBR pieces only for reusable environment-map, post-process or atmospheric algorithms behind DarkAngel's forward/material boundary; do not adopt a deferred PBR renderer wholesale. Evaluate DiligentFX post-processing and epipolar scattering against pinned revisions, formats, depth conventions, history requirements and license/maintenance status. Any code reuse requires a recorded license, compatibility, maintenance, integration-cost and fallback decision. No DiligentFX dependency is approved by this backlog.

### Visual verification scene and proposed budgets

The future verification scene contains multiple colored directional/point/spot lights, a toon-shaded character with normal/emission/rim, hit flash/status tint and dissolve, outlined grouped meshes, opaque/cutout/transparent entities, terrain layers/triplanar/vertex variation, cascaded and local shadows, fog, bloom, grading and the selected AA baseline. It must render on D3D12 and Vulkan, save/reload material and scene profiles, and expose variant/overflow/stale-resource diagnostics.

Initial budgets are proposals requiring measurement: clustered-light assignment and frame submission should remain within a documented per-frame budget; disabled features should add no meaningful GPU pass cost; outline thickness should scale or fade with distance; material variants should have an explicit bounded count; and the verification scene should record GPU timings, memory, draw/dispatch counts, light-cluster overflow, shader/PSO cache misses and cold-load time. Hardware, resolution, light count, visible mesh count and cache state must accompany every result.

### Open architectural decisions

Resolved on 16 October 2026 (see [Rendering decisions](#rendering-decisions-16-october-2026)): clustered Forward+ with compute assignment where justified; cascaded main-directional shadows; AO limited to ambient/contact; distance fog modes; the post-stack contents and a single initial AA; and the combined ID/depth/normal outline strategy with distance scaling. R7 was accepted on 17 October 2026. Baked GI/lightmaps/probes and the removed rendering systems are out of scope and not planned; SSAO, LUT grading and other listed polish are candidates only. The remaining bullets are historical design questions, not open R7 gates:

- Forward+ cluster dimensions, light-list overflow/fallback behavior, and whether compute assignment is always enabled or workload-gated.
- HDR format/output transform, exposure ownership, toon ramp representation, shadow color/ambient energy model and colored-light accumulation rules.
- Exact Character/Entity/Terrain feature schemas, variant keys, permutation budget, fallback behavior and texture semantic/cook contracts.
- Outline pass inputs, grouping IDs, transparency/dissolve ordering, the specific AA method and distance metric.
- Terrain-family separation versus shared entity surface, triplanar cost, LOD inputs and material-key persistence across generated topology.
- DiligentFX reuse boundaries, pinned revision/licensing, atmospheric algorithm, local shadow budgets and any bindless/indirect-draw requirements.

### Cross-reference to the roadmap

R4 carries stable prefab/material-slot identity. R5 carries the shared MaterialDefinition/MaterialInstance and editor/cook foundation. R6 consumes the definitions for preview and authored UI surfaces without a second material editor. R7 carries the environment, lighting profile, render-graph and post-process foundation. The old SR0–SR7 labels are historical only; the standalone initiative is retired and no reimplementation schedule is authorized.

## Research boundaries

Primary upstream sources linked above were consulted on 15 October 2026. Recommendations for asset ownership, settings locations and staged delivery are DarkAngel design judgments based on source/DAE-005, not upstream guarantees. DiligentFX/xatlas compatibility, final bake backend and exact numerical budgets remain to be qualified. No upstream pin or locked architecture record was changed.
