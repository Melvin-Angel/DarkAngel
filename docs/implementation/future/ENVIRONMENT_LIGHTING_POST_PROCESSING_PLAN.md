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

## Research boundaries

Primary upstream sources linked above were consulted on 15 October 2026. Recommendations for asset ownership, settings locations and staged delivery are DarkAngel design judgments based on source/DAE-005, not upstream guarantees. DiligentFX/xatlas compatibility, final bake backend and exact numerical budgets remain to be qualified. No upstream pin or locked architecture record was changed.
