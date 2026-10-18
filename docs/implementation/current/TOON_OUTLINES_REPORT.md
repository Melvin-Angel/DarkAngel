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
