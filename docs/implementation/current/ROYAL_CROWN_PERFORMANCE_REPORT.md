# Royal Crown Temple performance check

Date: 18 October 2026

## Setup

- Scene: `content/royal_crown/RoyalCrownTemple.dascene` (1,052 authored
  objects: 1,049 model placements and 3 lights).
- Backend/device: D3D12, Intel Iris Xe Graphics.
- Hidden editor run at fixed 1920x1080 and 1366x768 render dimensions, with
  audio disabled. Each run sampled 301 authoring frames and 601 Play frames
  after warmup. The scale exercise added two static test models at (500, 500),
  outside the scene camera view.
- Play was enabled with the existing Royal Crown player model, player kit, and
  player object `00000000000000000000000000000500`.

## Results

| Resolution | Mode | Mean frame | P50 | P95 | Scene render | Present |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | Authoring | 16.61 ms | 16.13 ms | 20.38 ms | 14.58 ms | 0.96 ms |
| 1920x1080 | Play | 19.67 ms | 19.30 ms | 22.47 ms | 17.73 ms | 1.02 ms |
| 1366x768 | Authoring | 15.13 ms | 14.77 ms | 16.41 ms | 13.25 ms | 0.93 ms |
| 1366x768 | Play | 18.52 ms | 18.08 ms | 20.85 ms | 16.65 ms | 1.01 ms |

Play averaged about 50.8 FPS at 1080p and 54.0 FPS at 768p. Neither result
sustains 60 FPS (16.67 ms/frame). Reducing resolution improved Play mean time
by about 1.15 ms, so resolution alone does not remove the bottleneck. These
are hidden fixed-resolution timings, not a maximized interactive-window or
vsync/compositor measurement. Vulkan was not profiled.

The scene package/launcher now includes the existing player model and kit;
Play preparation and activation succeeded in both runs. Reimport preserves
the scene's authored rendering/light overrides. Peak process memory during
Play preparation was about 2.57 GB; settled working set was about 1.7 GB.

Reports: `.cache/royal-crown-benchmark/report-1080.json` and
`.cache/royal-crown-benchmark/report-768.json` (ignored local artifacts).
