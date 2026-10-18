# Unity-inspired toon band upgrade

18 October 2026. User-requested local implementation; no commit/push.

## Inspected references

Read-only sources in `C:/Unity Projects/AshenRootsMP/Assets`:

- `Game/Shaders/AshenRootsPainterlyToon.shader`
- `Game/Shaders/AshenRootsPainterlyToon_Directional.shader`
- `Game/Shaders/AshenRootsPainterlyLightingCommon.hlsl`
- `Game/Prefabs/Post Processing/PostProcessing.prefab`
- Its referenced profile: `thirdparty/3D/EmaceArt/PolyVania/PostProcess/SampleSceneRenderingSettings_Night.asset` (GUID `3481ba10c276b7a4aba32da06eab06ec`).

The Unity shaders interpolate painted shadow/lit colours, restrain indirect light, and quantize attenuated lighting. The profile uses contrast +10, saturation +30, high-threshold bloom (10), outlines and vignette. These are art-direction references, not portable HDRP/URP values: no Unity file is edited, no prefab is imported, and no DarkAngel scene/profile is overwritten to imitate it numerically. Brush projections/textures and vignette are not implemented by this increment.

## Changes

- Shared Standard/Character/Terrain toon shader uses four paint values (0, .4, .75, 1). Existing ordered thresholds and edge softness still control their boundaries; zero softness is explicitly safe.
- Shadow tint participates in normal-facing shading, not only cast-shadow multiplication. Cast-shadow visibility enters the same band response. Emission remains independent.
- Main-light tint is restrained to preserve atlas colours, following the Unity painted-colour approach. Ambient is restrained (.35 gain) rather than washing out the band separation.
- Local distance/spot falloff enters the band function instead of multiplying smooth gradients over its output. Accumulated local fill is bounded at .65 per channel. Smooth specular is stepped for toon families; explicit lit-surface materials retain smooth lighting.
- Clustered lights now compare view-space light directions against view-space normals. The fallback path compares object-local directions and normals consistently. Camera ownership/routing and post-process texture binding are unchanged.
- Stronger contrast exposed floor shadow-map stripes in the pre-upgrade baseline. PCF now samples texel centres and compares the corresponding receiver-plane depth, rather than mixing bilinear depths across a sloping receiver. The GPU test now rejects shadowing in a known clear-floor patch as well as checking actual foot shadows.
- CPU scalar `evaluate_toon` mirrors the four values, smoothstep edge rule, painted key palette and bounded fill. Its local-light reference assumes a facing surface; it is not a full per-pixel GPU reference.

No new scene/material format or override hierarchy is introduced. Lighting-tab thresholds, band softness, shadow tint, ambient and local-light controls remain authoritative, including explicit scene/material overrides.

## Verification

Completed local verification (final editor SHA256 `87a30aa29a17358c202f9757c650454785c44471f47a17bbd74e1ed09fb47a13`):

- `DarkAngelEditor` and `R7RenderingTests` built; final Ninja check reports no work to do.
- R7RenderingTests passes four flat plateaus, ordered paint values, shadow/lit palette endpoints, soft-edge interiors/transitions, shadow visibility and bounded local fill. PreviewRoutingTests, SceneCameraTests, CameraTests, ShadowCameraTests and SmoothNormalsTests also pass.
- `artifacts/shadow-output-ead9c949b9/receipt.json`: D3D12 and Vulkan each produce 14,118 darkened floor pixels and 364 foot-contact pixels; the clear-floor shadow fraction is zero on both. All authored content hashes remain unchanged. Both final views were checked for clear ground and character shadow contact.
- Plain stopped Scene captures pass on both backends: `artifacts/shadow-output-ead9c949b9/scene-plain-d3d12.png` and `scene-plain-vulkan.png`. The D3D12 image was inspected for stepped hut lighting, intact scenery and cast shadows. An earlier `--select-spawn` exercise attempt was not a passed gate: the current authored scene has no NPC spawn points and its selected-spawn senses assertion failed. Plain rendering was then checked without that unrelated exercise.
- D3D12 Animation playback changes 21,193 preview pixels (`artifacts/viewport-output-9a3b99e129/receipt.json`); Vulkan changes 21,194 (`artifacts/viewport-output-6b6a260899/receipt.json`).
- D3D12 Character scrubbing changes 18,528 pixels (`artifacts/viewport-output-30cf3ddd04/receipt.json`); the isolated character preview was inspected. All three preview receipts report protected content unchanged.

These are local GPU captures and scripted-input checks, not physical-device, full combat, full-project, or remote qualification. This is a first Unity-inspired art-direction pass, not an assertion of an exact visual match to Unity.

Before this upgrade, the smooth-normal/contact-shadow comparison passed both backends with protected content unchanged: `artifacts/shadow-output-d618731abf/receipt.json`. Stronger contrast and subsequent visual inspection revealed baseline floor striping that its old test did not reject. Only the final receipt above qualifies the corrected filter and clear-floor check.
