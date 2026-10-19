# Royal Crown sky, shadows and Game resolution

18 October 2026. Follow-up to [the performance audit](ROYAL_CROWN_PERFORMANCE_AUDIT.md).
Target: 60 FPS on Iris Xe at 1280 x 720 or 1366 x 768 internal resolution.

## Renderer changes

- Procedural sky/clouds render into an HDR cache per viewport. Settings or
  target-size changes rebuild it; ordinary frames sample it directly. Exposure,
  tone mapping and grading remain downstream. Imported HDR skies retain their
  original path. The cache replaces repeated cloud noise evaluation; it adds
  one full-screen draw only on invalidation.
- Shadow caster culling tests transformed bounds against the receiver frustum
  extruded toward the light. This preserves offscreen objects casting visible
  shadows while rejecting unused corners of the broad cascade box. Existing
  cascade matrices, shadow resolution and shadow distance remain unchanged.
- Static objects sharing a model and the same native material overrides can
  instance together. Opaque shadow submissions share constants and bindings;
  contiguous opaque mesh ranges with compatible sidedness coalesce. Cutout
  ranges retain their material and texture bindings, including alpha tint.
  Crown's single-part meshes do not benefit from range coalescing itself.
- Profiling reports sky builds/hits, additional caster rejections and merged
  shadow ranges. Existing section values measure CPU wall time and driver
  backpressure; they are not GPU timestamps.

## Game view

The Game viewport's Resolution menu offers Match viewport, 1280 x 720,
1366 x 768, 1600 x 900 and 1920 x 1080. Fixed presets resize the GPU targets
and stretch the result across the existing panel. They do not change docking,
panel size or input coordinates. Both local player views use the selected
preset, and Scene/authoring views continue matching their panels.

The choice persists in the existing per-user editor preferences. Camera
projection uses the render target's aspect; the existing HUD/world-marker
presenter maps that projection into the display panel. Screen-space UI retains
panel resolution. A panel with a different aspect ratio stretches the image,
as requested. Changing resolution recreates targets and can briefly stall;
fixed resolution avoids repeated recreation during panel resizing.

## Iris Xe results

Actual target sizes were checked from captured viewport rectangles. Same
authored Crown scene and main camera; hidden editor, no audio, vsync disabled,
10-frame warm-up excluded. No additional authored quality reduction.

| Backend / view | Target | Samples | Mean ms | Mean FPS | P95 ms | Max ms |
|---|---|---:|---:|---:|---:|---:|
| D3D12 main | 1280 x 720 | 889 | 10.27 | 97.3 | 13.77 | 22.02 |
| D3D12 main | 1366 x 768 | 889 | 10.01 | 99.9 | 13.98 | 16.87 |
| D3D12 reference main | 1366 x 768 | 889 | 11.24 | 89.0 | 15.20 | 19.03 |
| Vulkan main | 1366 x 768 | 889 | 6.63 | 150.9 | 9.38 | 13.10 |
| D3D12 west | 1366 x 768 | 589 | 8.28 | 120.8 | 12.92 | 16.60 |

The reference flag disables this follow-up's cache, caster hull and new batching
paths. D3D12 main-view mean frame time improves about 11% at 768p. Crown shadow
draws fall from 199 to 150 at the main camera and 202 to 118 at the west camera.
The shared-native-override regression reduces color/outline draws from 104 to
82 each. That variant demonstrates the broader batching path; it is not the
authored Crown performance measurement.

These samples exceed the average and P95 60 FPS budget. Occasional D3D12
frames exceed 16.67 ms, so they do not establish an absolute 60 FPS minimum,
whole-game gameplay performance, or native-resolution qualification.

Receipts: `artifacts/sky-shadow-target-{720-d3d12,768-d3d12,768-vulkan,768-west}/receipt.json`
and `artifacts/sky-shadow-reference-768-d3d12/receipt.json`. These timings were
taken before the subsequent Game-resolution UI addition, using exact target
sizes through the Scene viewport.

## Validation

- D3D12 and Vulkan optimized/reference pixel and cache-invalidation checks:
  `artifacts/sky-shadow-check-f3f754fc4d/receipt.json` and
  `artifacts/sky-shadow-check-3bbdedb160/receipt.json`.
- Final 768p main/west images: `artifacts/sky-shadow-target-image-checks.json`;
  zero pixels exceed channel error four, maximum error three.
- Cache probes cover seed/cloud edits, actual target resize, exposure changes,
  disable and reenable. Caster tests cover receiver points, offscreen casters,
  translated cameras, vertical lights and empty broad-box corners.
- Final rebuilt editor: `artifacts/game-resolution-build.log`, binary SHA256
  `c7b347b5b69a8587ec96d045bd1c4a2092fb1aaf5495e626ab0d9240373732ab`.
- Live preset switching on D3D12/Vulkan:
  `artifacts/game-resolutions-945410454a/receipt.json` and
  `artifacts/game-resolutions-9d17d975e3/receipt.json`. All fixed presets preserve
  the same panel bounds; later panel resizing preserves the fixed target;
  Auto/Scene restoration matches panel dimensions. Saving records the preset.
- Final Animation playback, Character scrubbing and Game FreeLook checks:
  `artifacts/viewport-output-1f5943f8af/receipt.json` (D3D12, saved 720p) and
  `artifacts/viewport-output-c16cb62fbe/receipt.json` (Vulkan, saved 768p).
  Game target dimensions and camera aspect are asserted, and final image motion
  is measured. Captures were inspected for the selector, stretched image and HUD.
  Scene/Character/Animation targets retain their panel dimensions despite the
  saved Game preset. These are single-player viewport receipts.
- Final-binary sky/shadow pixel regressions also pass on both backends:
  `artifacts/sky-shadow-check-36413091a3/receipt.json` and
  `artifacts/sky-shadow-check-29a0b27c8c/receipt.json`.

### Whole-suite limit

The serialized 124-test integration attempt is **not fully passing**. Initial
stale executables reported duplicate UUIDs and a cooked script API mismatch;
rebuilding the affected targets cleared those errors. Across the initial run
and refreshed rerun, 113 tests pass, five fail with `Actor closure product count`
in `engine/assets/actor_cooker.cpp:186`, and six fixture-dependent tests remain
unrun. Failing tests are native authoring, native authoring commands, native
graph commands, Royal mesh collision and behavior tree. This actor-cooking
boundary is outside the renderer/UI edits and remains unresolved; the affected
preexisting character material overrides were preserved.

Logs: `artifacts/sky-shadow-game-ctest.log`,
`artifacts/sky-shadow-refresh-build.log`, and
`artifacts/sky-shadow-game-ctest-refreshed.log`. The shadow-camera and Scene
camera/preset tests pass. Do not infer full authoring qualification from the
successful graphics checks.

Reproduce after building the editor:

```powershell
python scripts/verify_sky_shadows.py --backend d3d12
python scripts/verify_game_resolutions.py --backend d3d12
python scripts/verify_viewport_output.py --resolution 1
```

All exercises use isolated preferences and leave authored scene sources intact.

## Final pre-push comparison

Fresh measurements on the final rebuilt editor, after adding the Game resolution
selector. Each final run records 889 steady-state samples; same main Crown
camera, no parallel build/editor, hidden window, vsync/audio disabled. Explicit
`--ui-scale 1` reproduces the opening benchmark's actual panel geometry.

| D3D12 stage | Actual target | Mean ms | Mean FPS | P95 ms | Max ms |
|---|---|---:|---:|---:|---:|
| Opening baseline | 1168 x 636 | 23.25 | 43.0 | 32.40 | 42.24 |
| First optimization pass | 1168 x 636 | 13.23 | 75.6 | 18.05 | See original receipt |
| Final | 1168 x 636 | 12.24 | 81.7 | 16.46 | 27.27 |
| Final | 1280 x 720 | 10.64 | 94.0 | 14.02 | 21.91 |
| Final | 1366 x 768 | 11.03 | 90.6 | 14.91 | 18.79 |
| Final | 1896 x 878 | 12.03 | 83.2 | 15.91 | 28.39 |
| Final | 2536 x 1398 | 20.70 | 48.3 | 23.50 | 31.61 |

Vulkan final at 1366 x 768: 7.44 ms / 134.4 FPS average, 9.89 ms P95,
16.69 ms maximum. Previous runs above show normal run-to-run variation; use
this table for the fresh final-binary summary, not a mixture of best samples.

The direct opening-to-final comparison is **43.0 to 81.7 FPS at the same
1168 x 636 target**: about 90% more FPS and 47% less mean frame time. P95 falls
from 32.40 to 16.46 ms. This includes the authored shadow tradeoff from three
4096-pixel cascades over 320 m to two 2048-pixel cascades over 180 m; it is not
entirely an equivalent-quality code speedup. The earlier code-only pass with
original shadows averaged 60.7 FPS at that size.

There was no opening 720p/768p or Vulkan baseline, so those rows establish
current target performance without claiming a same-resolution initial gain.
The final 1896 x 878 result improves on the first pass's 70.3 FPS; the final
2536 x 1398 result improves on its 40.3 FPS. Both are intermediate-to-final
comparisons, not opening measurements. A strict minimum of 60 FPS remains
unqualified: even the final 720p/768p runs contain occasional frames over budget.

Fresh receipts are `artifacts/performance-push-final-{panel,720,768,full,native,768-vulkan}/receipt.json`.
The opening receipt is `artifacts/performance-crown-before/receipt.json`.
The authored Crown source and final binary hashes match those recorded above.

Example final docked reproduction:

```powershell
python scripts/benchmark_royal_crown.py --output artifacts/crown-final-panel --frames 900 --width 1920 --height 1080 --ui-scale 1 --cases authored
```

For exact 720p use `--width 1304 --height 922 --ui-scale 1 --maximize`;
for exact 768p use `--width 1390 --height 970 --ui-scale 1 --maximize`.
The extra window pixels account for editor chrome. Gameplay, moving-camera,
combat and sustained thermal qualification remain separate from these static
authoring-scene measurements.
