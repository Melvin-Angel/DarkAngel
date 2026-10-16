# Skyboxes, procedural skies, lighting, fog and post processing

**Scheduling: Planned, R7 (H + I + J)** in the [authoritative roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026), after R6. R7.1 covers sky/Lighting/fog/lights, R7.2 scene/camera post profiles and legal effect stages, R7.3 UV/backend work and static indirect baking; probes remain later. Future tools adopt the [UX standards](EDITOR_UX_OVERHAUL_PLAN.md), not implementation during R2.

15 October 2026. **Research and future-work design only. No renderer features, assets, tools, dependencies or bake backend added.** Extends the [authoring handoff](AUTHORING_NEXT_STEPS_2026-10-15.md) at the user's request. [DAE-005](../../architecture/Decision_Log.txt) still owns rendering architecture; its deferred lightmaps/probes are now explicitly planned work, not implemented or a newly locked backend choice.

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
| Render4 (formerly R4) | Bake settings/status tool, UV/backend spike, then actual static indirect bake integration | UV1 preserved separately, static placement bindings, no double diffuse light, stale-result rejection and cold packaged sampling |
| Later | Probes, local volumes, richer atmosphere/weather and temporal effects | Dynamic actors/doors/travel, quality/performance and resource-lifetime qualification |

Renderer-local steps are now labelled Render0–Render4 (old R0–R4 retained above for traceability), distinct from project R1–R8. Map them to R7: Render0 is the foundation; Render1/Render2 plus Render3's direct-light/shadow controls belong to 7.1; Render3's post stack belongs to 7.2; Render4 belongs to 7.3. Direct-light controls must not wait for post processing merely because they shared an older row. Material editing and render texture profiles from [Assets and materials](ASSETS_MATERIAL_EDITING_PLAN.md) are shared prerequisites. Follow the main roadmap rather than the former parallel rendering-stream suggestion; preserve active build/dependency state. Use focused cook/transaction tests plus a small reference scene (neutral swatches, chrome/rough spheres, alpha foliage, a cave/interior, moving character and UI). Broaden to D3D12/Vulkan when render graph/format/shader changes land; no multiplayer protocol matrix is needed for a numeric fog form. Track actual bake quality/time/memory and renderer timings instead of assuming a supported dependency guarantees a finished feature.

## Ashen Roots stylized rendering initiative (deferred)

**Status: Deferred future work / not scheduled.** This initiative is a planning boundary for Ashen Roots, not an implementation authorization. R4–R6 establish source ownership, stable material assignments, MaterialDefinition/MaterialInstance contracts and preview/editor integration. R7 establishes the environment, render-graph, lighting-profile and post-process prerequisites. The remaining work below is deferred until the roadmap explicitly schedules it. Do not create shaders, runtime classes, renderer behavior or new dependencies from this section.

### Current capability boundary

The inspected renderer path currently provides DiligentCore/DiligentTools infrastructure, a basic color-oriented material path with 2D sRGB RGBA8 textures, simple color/roughness/metallic/double-sided fields, and a stylized light in the proof viewport. It does not currently provide authored HDR/cubemap loading, a general light/fog editor, a profile-driven render graph/post stack, toon material families, outline composition, or qualified local-light/shadow products. DiligentFX is not integrated; its PBR, post-processing and atmospheric examples are research references only. xatlas is UV tooling, not a GI solver. These are current source findings, not claims about future implementation.

### Target architecture

The deferred target is a clustered Forward+ pipeline over DarkAngel's existing Diligent abstraction, render graph, resource lifetime, HLSL/DXC compilation, PSO cache and AssetService/cooker boundaries. D3D12 remains the first path and Vulkan remains an early and required validation path. The design must remain game-agnostic while using Ashen Roots' stylized third-person environments as the initial acceptance target; it must not become a PBR-centered architecture.

The shared surface library should support directional, point and spot lights, multiple colored dynamic lights, toon bands/thresholds/softness/shadow colors, ambient contribution, optional normal/specular/rim terms and controlled transparency. Compute clustered light assignment is preferred where it improves bounded local-light scaling, but a measured fallback is required for unsupported or small scenes. Integrate light lists, material parameters, render states and pass resources through the existing graph and resource manager rather than allowing materials to create ad-hoc GPU objects.

Material families are three schemas over shared lighting code:

- **Character:** base color/albedo, cel bands, colored lights, optional normal/emission/rim/specular, vertex color/tint, hit/status overlays, directional/noise dissolve with glowing edge, detail/tattoo masks, opaque/cutout/transparent modes, outline participation and GPU skinning. Runtime effects use per-character GPU parameters rather than material creation or shader recompilation.
- **Entity:** the shared standard surface features plus UV, world/object triplanar mapping, gradient/vertex blending, alpha modes, outlines and compatible static GPU instancing. Character-only hit/dissolve defaults are not required.
- **Terrain:** only a separate family if prototypes show a meaningful performance/architecture benefit; shared toon lighting plus surface layers, slope/height rules, world triplanar, vertex/splat blending, optional normals, distance detail, outlines and LOD-aware terrain inputs.

Feature-based variants, cached PSOs, separate opaque/cutout/transparent states, conditional texture sampling, shared definitions with per-instance parameters, material sorting, bindless resources where beneficial, static instancing and future instanced skinning are optimization directions. A variant budget, stable keys and fallback materials are required to prevent uncontrolled permutation growth.

### Deferred implementation order

| Phase | Objective and scope | Dependencies / acceptance boundary |
| --- | --- | --- |
| SR0 — renderer qualification | Record DiligentCore/DiligentTools revision/API boundaries, render-graph/resource/PSO integration points, D3D12/Vulkan format support, shader variant accounting and reuse candidates | R7 renderer foundation. Acceptance is a written capability matrix and small isolated prototypes; no feature is considered available because an upstream sample exists |
| SR1 — shared toon surface and lighting | Shared toon-lighting library, HDR intermediates, directional light, bounded point/spot lights, clustered Forward+ prototype, ambient contribution and shadow contract | R5 MaterialDefinition/Instance and R7 lighting profile. Qualify colored-light accumulation, D3D12/Vulkan parity, overflow diagnostics, shadow budgets and no-PBR assumptions |
| SR2 — material families and editor | Character/entity/terrain family schemas, feature variants, material serialization/cook/hot reload, relevant-property editor, previews and GPU effect parameters | R4 stable slot identity, R5 asset/material editor, R6 preview routing. Acceptance includes save/reopen/reimport, variant bounds, no-op controls hidden/diagnosed and cold cooked load |
| SR3 — stylized presentation | Renderer-level object/group, depth-discontinuity and normal-discontinuity outlines; distance scaling/fade; hit flash, status tint, dissolve, transparency and AA interaction | SR1/SR2 depth/normal/HDR contracts. Choose a practical initial outline method; retain inverted hull only as fallback. Qualify connected character grouping, transparent/dissolve ordering and distant visual noise |
| SR4 — environment lighting | Cascaded main directional shadows, budgeted local shadows, ambient/AO treatment, distance and optional height fog, terrain material integration | R7 sky/lighting/fog profiles and SR1. AO primarily affects ambient/indirect contribution; acceptance covers shadow bias/cascade controls, fog with transparency and toon compatibility |
| SR5 — post processing | HDR output/tone mapping, exposure, color grading/LUT option, bloom, one qualified AA baseline, AO/fog/outline composition as legal graph stages | R7 post profile/render graph and SR3/SR4. Effects are independently disabled with minimal overhead; choose one AA baseline for stable cel edges rather than multiple competing methods |
| SR6 — production optimization | Material sorting, batching, static GPU instancing, upload/frame buffers, bindless where useful, GPU culling/indirect draws, skinning direction, async streaming/residency, texture mips and mesh LOD | Measured SR1–SR5 workloads and existing AssetService/WorldSession budgets. Acceptance is profiling evidence, bounded memory/variants and cold packaged loading, not a successful compile |
| SR7 — advanced deferred features | Atmospheric scattering, epipolar light shafts, GPU particles, instanced skinned meshes and selected higher-end effects | Only after SR0–SR6 and a new priority decision. Investigate reusable DiligentFX atmospheric components; advanced GI, ray tracing, SSR/SSGI, motion blur, DOF and temporal reconstruction remain explicitly deferred |

### Outlines, fog, shadows and post-process decisions

Outlines remain a renderer-level composition feature, not geometry embedded in each material. Prototype object/group IDs, depth discontinuities and normal discontinuities; compare cost, grouping quality, transparency behavior, AA stability and distance scaling. The initial choice must prevent thick distant outlines and excessive foliage/terrain noise. Inverted hull remains a documented fallback, not the default.

Use cascaded shadow maps for the main directional light, explicit resolution/distance/bias/cascade controls and bounded local-light shadow budgets. Ambient occlusion should primarily affect indirect/ambient lighting rather than indiscriminately darkening toon bands. Distance fog supports linear/exponential density, color, start/end and optional height terms, with depth/atmospheric integration and editor controls. Post effects are legal registered graph stages with explicit resource dependencies; disabled effects do not allocate or execute their expensive path.

### Reuse and dependency research

Evaluate DiligentCore/DiligentTools for existing device, shader, PSO, resource, render-target and tooling contracts. Evaluate DiligentFX PBR pieces only for reusable environment-map, post-process or atmospheric algorithms behind DarkAngel's forward/material boundary; do not adopt a deferred PBR renderer wholesale. Evaluate DiligentFX post-processing and epipolar scattering against pinned revisions, formats, depth conventions, history requirements and license/maintenance status. Any code reuse requires a recorded license, compatibility, maintenance, integration-cost and fallback decision. No DiligentFX dependency is approved by this backlog.

### Visual verification scene and proposed budgets

The future verification scene contains multiple colored directional/point/spot lights, a toon-shaded character with normal/emission/rim, hit flash/status tint and dissolve, outlined grouped meshes, opaque/cutout/transparent entities, terrain layers/triplanar/vertex variation, cascaded and local shadows, fog, bloom, grading and the selected AA baseline. It must render on D3D12 and Vulkan, save/reload material and scene profiles, and expose variant/overflow/stale-resource diagnostics.

Initial budgets are proposals requiring measurement: clustered-light assignment and frame submission should remain within a documented per-frame budget; disabled features should add no meaningful GPU pass cost; outline thickness should scale or fade with distance; material variants should have an explicit bounded count; and the verification scene should record GPU timings, memory, draw/dispatch counts, light-cluster overflow, shader/PSO cache misses and cold-load time. Hardware, resolution, light count, visible mesh count and cache state must accompany every result.

### Open architectural decisions

- Forward+ cluster dimensions, light-list overflow/fallback behavior, and whether compute assignment is always enabled or workload-gated.
- HDR format/output transform, exposure ownership, toon ramp representation, shadow color/ambient energy model and colored-light accumulation rules.
- Exact Character/Entity/Terrain feature schemas, variant keys, permutation budget, fallback behavior and texture semantic/cook contracts.
- Outline pass inputs, grouping IDs, transparency/dissolve ordering, AA choice and distance metric.
- Terrain-family separation versus shared entity surface, triplanar cost, LOD inputs and material-key persistence across generated topology.
- DiligentFX reuse boundaries, pinned revision/licensing, atmospheric algorithm, local shadow budgets and any bindless/indirect-draw requirements.

### Cross-reference to the roadmap

R4 carries stable prefab/material-slot identity. R5 carries the shared MaterialDefinition/MaterialInstance and editor/cook foundation. R6 consumes the definitions for preview and authored UI surfaces without a second material editor. R7 carries environment, lighting profile, render-graph and post-process prerequisites. SR0–SR7 remain a deferred future initiative and must be scheduled explicitly before implementation.

## Research boundaries

Primary upstream sources linked above were consulted on 15 October 2026. Recommendations for asset ownership, settings locations and staged delivery are DarkAngel design judgments based on source/DAE-005, not upstream guarantees. DiligentFX/xatlas compatibility, final bake backend and exact numerical budgets remain to be qualified. No upstream pin or locked architecture record was changed.
