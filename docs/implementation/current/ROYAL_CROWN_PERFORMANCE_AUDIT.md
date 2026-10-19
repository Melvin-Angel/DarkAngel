# Royal Crown Temple performance audit

18 October 2026. Working tree based on `c378730`.

This records the opening audit and first pass. The final implementation,
fresh pre-push measurements and current test boundary are in
[ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md](ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md).

Implemented and measured a first optimization pass on the real imported Crown
scene: 1,052 objects and 1,049 model placements. Existing outline, cloud,
material and character changes in the shared checkout were retained. No push
or commit was made because implementation overlaps those existing changes.

## Result and acceptance boundary

The same docked camera improved from **43.0 to 75.6 average FPS** on the local
Intel Iris Xe / i7-1185G7 (driver 32.0.101.7085). Frame time fell from 23.25 to
13.23 ms, a 43% reduction. This includes both renderer optimizations and the
explicit Crown shadow-quality change described below.

**A strict minimum of 60 FPS is not qualified.** The final maximized 1080p
window averages 70.3 FPS, but its 95th-percentile frame is 17.13 ms, above the
16.67 ms budget. Maximizing at 2560x1600 averages 40.3 FPS. Native-resolution
GPU work and occasional slower frames remain active performance work.

| D3D12 run | Actual scene pixels | Samples | Mean ms | Average FPS | P95 ms |
|---|---:|---:|---:|---:|---:|
| Before, original settings, docked | 1168x636 | 79 | 23.25 | 43.0 | 32.40 |
| Final, tuned Crown, docked | 1168x636 | 589 | 13.23 | 75.6 | 18.05 |
| Final, maximized 1920x1080 window | 1896x878 | 589 | 14.23 | 70.3 | 17.13 |
| Final, maximized west courtyard | 1896x878 | 289 | 13.94 | 71.7 | 17.54 |
| Final, maximized 2560x1600 window | 2536x1398 | 389 | 24.82 | 40.3 | 27.79 |

The window size includes editor chrome. Hidden runs disable vsync and audio;
the first ten frames are warm-up. These are static authoring-scene frame wall
times, including GPU backpressure, not packaged-game, moving-camera, combat,
split-screen or sustained thermal qualification. Short runs vary: earlier
tuned runs averaged 83-84 FPS in the docked viewport. Use the longer final
receipts above for the reported result. No build or separate graphics run was
active during performance sampling.

Final executable SHA256:
`30403ccea1f06030a0b6cba91a585caf0797d220dc1a3a1b31b4c392dffeb23a`.
Final Crown source SHA256:
`412e3a15e2045537870f9649cacb26f33acd1590c9e11f3adf0044ab1008963e`.

## Implemented changes

- Combined outline normal and object-ID output into one geometry pass using
  two attachments. Both primary and second viewports use it. Per-instance IDs
  remain full 32-bit floats; they are not truncated into half-float normals.
- Instanced repeated static models in that pass. The primary test camera goes
  from 485 outline draws in the initial corrected implementation to 81. Its
  measured CPU wall cost falls from about 6.7 to 1.9 ms. The color and outline
  transforms now follow the same instanced vertex calculation, removing the
  visible striping caused by depth disagreement between their old paths.
- Corrected the old object-mask route entering ordinary color instancing.
  Suppressed coplanar object-ID floor seams when continuous depth and normals
  show that separate pieces form one surface.
- Replaced static bounding-sphere frustum rejection with transformed-box
  plane extents, including shadow views. Animated skins retain conservative
  behavior because rest bounds do not enclose every animation.
- Cached placement resolution and base transforms within each frame, keyed
  by presentation world and object. Cascades, color and outlines share these
  reads; no cache survives into a later frame.
- Removed CPU cluster-cell allocation/sorting from GPU shading. Selecting
  CPU reference shading still runs the independent reference algorithm and
  avoids unnecessary GPU assignment dispatch. GPU assignment uses 64 threads
  per workgroup rather than one, with rounded-up dispatch and a bounds guard.
- Limited outline gathering to the active radius, rejected fully faded pixels
  before gathering, and skipped circular-filter samples with zero weight.
  Neutral gamma bypasses redundant power operations in the final pass.
- Alpha-cutout shadow draws exit after alpha/normal work instead of evaluating
  the full surface lighting into their unused dummy color attachment.

Before this task the scene submitted three shadow passes, color, two outline
passes and final post: seven scene passes. The tuned scene submits two shadow
passes, color, one outline pass and final post: five. ImGui/effects are separate.
Sky, bloom, grading, exposure and AA already share the final post pass. The
audited path does not contain expensive GI, SSR or SSAO passes to remove.

## Crown shadow-quality choice

Only these authored shadow fields changed for this task:

| Setting | Before | Applied |
|---|---:|---:|
| Cascades | 3 | 2 |
| Resolution | 4096 | 2048 |
| Distance | 320 m | 180 m |

They remain editable in Lighting and are preserved by the Crown importer.
AA, bloom, clouds, fog, outline thickness, materials, geometry and light
placement were retained. The four-layer depth and dummy-color allocations
drop from approximately 512 MiB to 128 MiB of raw attachment storage; the
implementation still allocates four array layers even when fewer are active.

Code changes alone, with the original three 4096-pixel cascades and 320 m
range, reached 16.48 ms / 60.7 average FPS at the docked test camera, with a
22.16 ms P95 (`artifacts/performance-crown-batched/receipt.json`). Thus the
quality reduction is a meaningful part of the final gain, not a free visual
equivalence claim. Full-window south and west captures were visually checked
for intact authored geometry, lighting, sky, contours and shadows.

## Remaining bottlenecks and profiling

The final docked frame reports roughly 1.85 ms preparation, 4.38 ms shadow
submission, 1.64 ms color, and 1.89 ms outlines. These are **CPU wall timings
including driver waits**, not GPU timestamps. In the maximized native view,
scene-render CPU time remains around 9.35 ms while presentation grows to
14.78 ms. Resolution scaling and feature ablations indicate a GPU limit there.

Native-size whole-frame ablations on the final binary, using isolated scene
copies (`artifacts/performance-crown-native-ablation/receipt.json`):

| Feature | Enabled frame mean | Disabled frame mean |
|---|---:|---:|
| Outlines: geometry plus post gathering | 24.82 ms | 18.89 ms |
| Procedural clouds | 24.82 ms | 21.42 ms |
| Shadows: submission plus sampling | 24.82 ms | 19.71 ms |
| FXAA | 24.82 ms | 24.42 ms |

These separate-run deltas include variability and overlapping CPU/GPU work;
they are not additive or isolated GPU pass timings. FXAA is a small target
compared with outlines and shadows, so it stays enabled. Bloom ablation at
1080p was also within run-to-run noise.

Lighting now has a Frame performance panel with frame budget, viewport size,
CPU render sections, per-pass draws, UI/Play and present timings. The existing
scale receipt includes per-pass timings/counts, and the benchmark script
records binary/source hashes, cameras, actual viewport rectangles, frame
distributions and isolated feature variants. The prior counters omitted
outline and some shadow draws, so old and new total-draw counters must not be
compared as though they measured the same submissions.

Next work toward reliable native-resolution 60 FPS should prioritize:

1. Safe asynchronous GPU timestamps with delayed, fenced query readback, so
   pixel work can be separated from CPU submission and presentation waits.
2. Outline bandwidth/sample reduction and caching the unchanged procedural
   sky/cloud result, rather than regenerating its noise for every sky pixel
   on every frame. Preserve contour quality and HDR sky values.
3. Explicit render-scale/quality controls for integrated GPUs, with output
   resolution and internal scene resolution both visible in profiling.
4. Tighter shadow receiver/caster visibility and broader material/mesh
   batching. Occlusion/Hi-Z and animated bounds need correctness qualification;
   indiscriminate distance culling would discard visible geometry.

## Verification and reproduction

Pinned MSVC/CMake `DarkAngelEditor` build passed; existing manifest-install-off
configuration and dependency/build directories were preserved. Nine focused
CTest gates passed: smooth normals, materials, renderable human, lighting
authoring, shadow camera, preview routing, scene camera, rendering core and
game camera. The complete 124-test suite was not run for this renderer pass.

- D3D12 outline thickness/black ink/fade/fog pixel regressions:
  `artifacts/toon-outlines-eb6d56671b/receipt.json`.
- Vulkan equivalent regressions:
  `artifacts/toon-outlines-5458a6ef22/receipt.json`.
- D3D12 Animation playback, Character scrubbing and Game FreeLook after resize:
  `artifacts/viewport-output-25cd45512a/receipt.json`.
- Vulkan Character scrubbing after resize:
  `artifacts/viewport-output-605161e233/receipt.json`.
- Final performance receipts:
  `artifacts/performance-crown-qualified-{panel,full,west,native}/receipt.json`.
- Original baseline: `artifacts/performance-crown-before/receipt.json`.
- Final build: `artifacts/performance-crown-build.log`.

Example reproduction after preparing the normal Crown package:

```powershell
python scripts/benchmark_royal_crown.py --output artifacts/crown-check --frames 600 --maximize --cases authored no_fxaa no_outline no_clouds no_shadows
```

Add `--width 2560 --height 1600` for the native-sized window, or omit
`--maximize` for the docked layout. `--camera-from` and `--camera-target` each
accept three coordinates. The benchmark makes scene copies and isolated
preferences; it never saves authoring operations into project sources.
