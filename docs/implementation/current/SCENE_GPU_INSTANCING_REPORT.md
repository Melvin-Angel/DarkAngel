# Scene GPU instancing verification

Date: 18 October 2026

Static scene color draws now share the existing per-instance transform stream
and batch by model/material part. The opaque and alpha-cutout paths use new
instanced surface pipelines. Models with per-object native material overrides,
transparent parts, skinned geometry, or selected object-local lights retain the
individual draw path. The outline-information pass remains per-object to keep
stable outline group IDs. Shader reload recreates both instanced color PSOs.
Static opaque/cutout shadow casters use the existing instanced shadow path.

## Verification

- `DarkAngelEditor` built with the pinned MSVC toolchain.
- `ctest -R R7`: 5/5 passed (`smooth_normals`, `lighting_authoring`,
  `shadow_camera`, `preview_routing`, `rendering_core`).
- Actual `RoyalCombat.dascene` rendered 8 hidden frames on D3D12 and Vulkan;
  both produced nonblank captures. D3D12 capture was visually checked for
  scene geometry and materials. Captures and the isolated cooked registry are
  in the ignored `.cache/royal-crown-play/` folder.
- D3D12 Play timing used the editor's built-in scale exercise, with 10 warm-up
  frames and 301 sampled Play frames on Intel Iris Xe Graphics. The scene
  contains 8 authored objects / 4 model placements; Play averaged 27 scene
  draws for 36 visible objects (the draw count includes the renderer's passes).

| Resolution | Mean frame | P50 | P95 | Scene render | Present | Mean-rate equivalent |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1366x768 | 3.290 ms | 3.104 ms | 4.173 ms | 2.312 ms | 0.522 ms | 304 FPS |
| 1920x1080 | 6.887 ms | 6.804 ms | 7.597 ms | 5.805 ms | 0.508 ms | 145 FPS |

These are hidden-run editor frame timings, not a windowed/vsync or GPU-only
benchmark. They establish that this scene stays below 16.67 ms in the measured
configuration; interactive compositor cost, other machines, and a same-harness
before/after delta were not measured. Vulkan was render-smoke-tested but not
performance-profiled. No claim is made that all R7 renderer qualification is
complete.
