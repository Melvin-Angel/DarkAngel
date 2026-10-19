# Toon outline compositor and distance atmosphere

18 October 2026

The preceding thickness increases did not produce visible dark ink because the
post shader added `outline_colour * edge_coverage` to the lit image. Adding
black changes nothing, and adding the near-black authored colour only makes
edges slightly brighter. The previous visual acceptance was incorrect.

`gpu_resources.cpp` now keeps edge coverage independent of colour and blends
the configured ink over the final image after FXAA. Geometry/ID lookups use
point loads. Projected depth slope is removed from neighbour comparisons, and
ID boundaries require a surface break, avoiding a grid around coplanar floor
pieces. Smooth normal variation no longer becomes opaque ink. The exaggerated
8 px test confirmed the fix; the final project default and Crown override are
1.5 px, with normal sensitivity returned to 1.

Outline distance fading previously compared 0..1 depth-buffer values with
metre-based settings. The compositor now reconstructs view-space distance
from each viewport's exact inverse projection (including the second Play
viewport), with the backend's NDC-to-texture Y convention. Outlines fade from
25 m to zero at 150 m. Camera ownership and interaction are unchanged.

The project and Crown scene use exponential fog with density .008 and the
existing warm colour (.38, .22, .075). Shared toon shading fogs the complete
surface radiance after specular and emission, so all families approach the
same fog colour as camera distance increases. These controls remain editable
in Lighting through the existing project-default/scene-override system.

## Verification

- Rebuilt `DarkAngelEditor` using the pinned CMake/MSVC environment;
  `LightingAuthoringTests` passed.
- Added `scripts/verify_toon_outlines.py`: isolated Crown scene variants with
  disabled outlines, 1 px pure-black ink and 8 px pure-black ink. It compares
  actual GPU viewport pixels and checks darkening, solid ink coverage and
  increased coverage when thickness increases. Authored scene bytes and user
  preferences are preserved. Additional variants check that a 2-3 m fade fully
  removes the more distant geometry's ink, moderate exponential fog moves
  surfaces toward the fog colour, and saturated fog converges to its expected
  tone-mapped colour. The latter uses neutral grading to verify actual colour.
- D3D12 passed: 16,095 solid black ink pixels at 1 px, 69,640 at 8 px; zero
  significantly brightened pixels. Receipt:
  `artifacts/toon-outlines-b82a8c477a/receipt.json`.
- Vulkan passed with the same solid ink counts and zero significantly
  brightened pixels. Receipt:
  `artifacts/toon-outlines-e664d871b2/receipt.json`.
- The fully faded outline image equals disabled outlines (zero changed pixels).
  Saturated fog converges on the expected colour across 207,500 geometry
  pixels. Moderate fog reduces mean colour error from 216.7 to 144.5 on D3D12.
- Vulkan's Khronos validation layer is unavailable on this machine. The run
  exercised Diligent's backend checks and logged dynamic heap waits; this is
  visual correctness verification, not a frame-time qualification.

The exaggerated diagnostic captures are under
`artifacts/user-captures/royal-crown-outlines-*.png`. Final 2560x1600 workspace
captures use the authored near-black colour, 1.5 px setting, real distance
fading and exponential fog under
`artifacts/user-captures/royal-crown-atmosphere-*.png` (overview, west, aerial,
detail and Play). The isolated regression captures use pure black specifically
to catch a recurrence of additive composition.

All five final D3D12 workspace captures were visually checked: foreground
contours remain visible, distant structures fade into warm haze, and Play
shows the spawned character with the FreeLook gameplay camera. Split-screen
and isolated authoring previews use the same projection-aware implementation
but were not separately captured in this focused verification.

## 18 October follow-up

Outline thickness no longer rounds to whole-pixel radii. The compositor uses
a continuous thickness radius with a feathered outer pixel and smooth depth,
normal, and object-group thresholds. Royal Crown's scene thickness is 1.25 px
to retain a crisp core with a small antialiased fringe. The scene shadow-map
resolution is raised from 2048 to 4096; the project default is unchanged.

The extended `scripts/verify_toon_outlines.py` now checks fractional thickness
between 1 px and 8 px. D3D12 and Vulkan captures passed the solid-ink,
fractional-coverage, distance-fade and fog checks. These are static camera
captures; moving-camera temporal flicker was not directly measured. Runtime
performance with 4096 shadow maps was not benchmarked.

The motion-flicker follow-up replaces strongest-neighbor outline selection
with a soft spatial union of nearby edge evidence, and broadens depth/normal
transition bands to reduce sensitivity to subpixel changes. The D3D12 GPU
outline regression passed after this update (thin/medium/thick solid black ink
coverage: 1,372 / 6,344 / 94,812 pixels). This is still a static-capture
regression; moving-camera and animated-character temporal stability need a
dedicated motion test and visual confirmation.

## Per-object outline mask

The compositor now reads a dedicated `R32_FLOAT` object-ID mask. IDs are
assigned from sorted stable object IDs for each view, avoiding the previous
small modulo-hash namespace and keeping identity stable when draw ordering
changes. Object boundaries drive the primary outline coverage independently
of depth/normal thresholds. Depth and normal contours remain available as a
much lower-sensitivity detail layer. The extra mask pass is also used by the
second local-player view.

`scripts/verify_toon_outlines.py` passed on D3D12 and Vulkan after the mask
change. D3D12 solid black ink coverage at thin/medium/thick settings was
6,247 / 13,826 / 45,653 pixels; Vulkan was 6,680 / 13,537 / 44,104 pixels.
The GPU captures verify resource binding, compositing, thickness response,
distance fade and fog; they do not constitute moving-camera temporal
qualification. Editor compilation succeeded. The additional full geometry
mask pass has not been performance-profiled.

The subsequent performance audit merged normal and object-ID outputs into one
instanced MRT geometry pass, retaining the full R32 object mask. The final Crown
shadow settings are two 2048-pixel cascades over 180 m. These supersede the
separate mask pass and earlier 4096-pixel setting described above. Final
performance and verification are in
[ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md](ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md).
