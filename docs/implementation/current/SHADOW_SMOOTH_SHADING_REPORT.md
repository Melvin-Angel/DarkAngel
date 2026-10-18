# Character shadows and smooth imported shading

17 October 2026. User-requested implementation; local working tree, no commit or push.

## Diagnosis and implementation

- The old shadow camera looked along the surface-to-sun vector, rather than from the sun toward the scene. Its fixed 200-unit eye and cascade-dependent far plane could also clip nearby characters out of the nearest cascade (Royal's first far plane was about 127 units).
- Shadow lookup used the wrong texture V convention. Vulkan additionally remapped an already 0..1 depth and reversed the comparison; D3D12 did not request comparison filtering. Both backends now use the device's NDC-to-texture Y convention, unchanged 0..1 depth, and a clamped LESS_EQUAL comparison sampler.
- Cascades now fit the active camera's receiver slice, include space toward the light for offscreen casters, snap their projection to shadow texels, and support vertical sun directions. This does not change any Scene, Game or preview camera state.
- Bias values now represent world-space metres (labelled in Lighting). PCF uses receiver-plane depth correction instead of adding depth and normal bias as fractions of the entire cascade depth range, which visibly detached shadows from feet.
- Static and renderable-character importers smooth angle-weighted normals across coincident split/UV vertices without welding geometry or changing UVs, indices, materials, joints or weights. Props retain 60-degree creases; characters use full organic smoothing within each primitive. Existing material/object overrides remain intact.
- The smoothing implementation is included in the cook-recipe fingerprint, so existing imported assets recook through normal cache invalidation. No authored model or scene is rewritten.

## Verification

Final world-space contact-bias/full-organic-smoothing GPU qualification passed on 18 October: `artifacts/shadow-output-d618731abf/receipt.json`, binary SHA256 `6cb1414d9f69012395527188bf6b94e61f3c754b861a66ba5931c2cfe4930c23`. D3D12/Vulkan produced 33,686/33,713 darkened floor pixels and 475 foot-contact pixels each. All authored content hashes were unchanged. This is the baseline before the subsequent Unity-inspired toon-band upgrade; recheck the new shader separately.

The first iteration passed ShadowCameraTests, SmoothNormalsTests, RenderableHumanTests, ExternalImportTests and R7RenderingTests. Actual Royal shadows-on/off captures on both D3D12 and Vulkan showed 11,195 darkened floor pixels per backend, with all authored content hashes unchanged (`artifacts/shadow-output-2d3439b0fe/receipt.json`). Visual inspection identified excessive contact detachment, prompting the world-space bias follow-up; this first receipt is not final acceptance of that follow-up.

The initial 60-degree character smoothing changed 6,909 of 7,286 source normals. Full organic smoothing changes 7,274 normals. Canonical inverse-bind rest error remains 2.99e-7 metres; RenderableHumanTests, ExternalImportTests, ShadowCameraTests, SmoothNormalsTests, SceneCameraTests and CameraTests passed.

`scripts/verify_shadow_output.py` uses disposable scene/record copies and private editor preferences. It compares final GPU viewport pixels with shadows enabled/disabled, including a floor patch adjacent to the player's feet, and checks every authored content file for mutation.

Qualification is local to this machine and these Royal views. No claim of a full project regression suite or cross-machine qualification is made. Point-light shadows remain unsupported.

The subsequent toon-band task revealed floor striping in the above baseline
that its initial contact-only pixel test missed. The final receiver-plane PCF
texel-centre fix and a clear-floor rejection test are verified in
`artifacts/shadow-output-ead9c949b9/receipt.json`; see
[TOON_BANDS_REPORT.md](TOON_BANDS_REPORT.md). Use that newer result for the final
shadow-filter qualification.
