# Fresh-phase continuation

The follow-up sky/cloud cache, tighter shadow caster visibility, native-material
static batching and independent Game render-resolution selector are recorded in
[current/ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md](current/ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md).
At exact 720p/768p targets, Iris Xe D3D12 measurements average 97-100 FPS with
P95 below 14 ms; occasional frames exceed the 16.67 ms budget. Game presets
stretch to the existing panel and persist in editor preferences.
The fresh final-binary comparison is 43.0 to 81.7 FPS at the opening 1168x636
target. Final D3D12 720p/768p runs average 94.0/90.6 FPS; the follow-up report's
final pre-push section supersedes earlier timing samples for this summary.
Renderer/viewport checks pass on both backends. The whole-suite attempt still
has five actor-cooking closure failures and six dependent tests unrun; details
and logs are in that follow-up report.

The 18 October Royal Crown performance audit and first optimization pass are
recorded in [current/ROYAL_CROWN_PERFORMANCE_AUDIT.md](current/ROYAL_CROWN_PERFORMANCE_AUDIT.md).
The final docked view improved from 43 to 76 average FPS on Iris Xe; a maximized
1080p window averages 70 FPS. A strict 60 FPS minimum remains unqualified, and
the maximized 2560x1600 window averages 40 FPS. The report distinguishes code
changes from the applied two-cascade/2048-pixel/180 m Crown shadow settings,
and records the remaining GPU/pixel-work bottlenecks and profiling limits.

Date: 17 October 2026

Start with [current/CURRENT_PHASE.md](current/CURRENT_PHASE.md). The current
implemented systems are accepted as verified. Do not reopen archived milestone
reports or create work from their old “Next” paragraphs.

The only known qualification exceptions are multi-system/cross-machine
networking and EOS. All other work is either a new user-approved development
task or a candidate in [future/DEFERRED_WORK.md](future/DEFERRED_WORK.md).

No implementation milestone is active from this reset. Preserve the current
baseline until the next concrete Ashen Roots objective is selected.

The 17 October 2026 animation-editor/camera objective is now the selected
development task. Its implementation boundary, evidence and incomplete items
are recorded in [current/ANIMATION_EDITOR_OVERHAUL_REPORT.md](current/ANIMATION_EDITOR_OVERHAUL_REPORT.md).
The native state-machine/typed-parameter contracts and schema-2 graph tests are
implemented. Remaining qualification is concentrated on any-state interruption
matrices, graph breadcrumbs/multi-selection, and actual effect asset playback
inside the isolated character preview; do not infer those from the taxonomy UI
or a successful build alone.

The 17 October imported-toon/rendering-defaults task is implemented in the
working tree. Static glTF slots default to `toon-standard-v1`, renderable human
fallback slots default to `toon-character-v1`, and imported normal/surface/
emission maps remain routed into the toon shader contract. Project rendering
defaults live in `rendering/project.darendering`; the Lighting tab edits those
defaults or the optional scene component, and explicit object/slot material
assignments remain authoritative. Focused build and tests passed:
`DarkAngelEditor`, `MaterialTests`, `RenderableHumanTests`, and
`ExternalImportTests`. Visual editor/runtime qualification was not run because
other agents had active editor work in this shared checkout.

The 17 October camera/render-route repair keeps `Shell::scene_view` and
`Shell::scene_projection` owned by Scene navigation. Gameplay publishes
`game_view`/`game_projection` for the primary HUD/world-UI camera, and the
gameplay director is queried whenever Play is active, independent of the
selected authoring tab; stopping Play restores the Scene camera route.
Authoring previews remain an isolated content/camera route and no longer
overwrite the scene camera matrices. `DarkAngelEditor`, `PreviewRoutingTests`,
`SceneCameraTests`, `CameraTests`, and `NativeInputTests` passed; the hidden
D3D12 `game-camera` gate passed, Composer preview/Animation workflow gates
passed, and a direct Character preview capture passed. The initial hidden
navigation/place attempt failed at its synthetic Place drag; the follow-up
below supersedes that qualification failure.

The follow-up using `scripts/open_royal_scene.ps1` content found the actual
display freeze in `render::post_pipeline`: mutable SceneColor/SceneDepth/
OutlineInfo bindings retained the first targets after resize, while camera,
picking and pose state advanced normally. All five replaceable post inputs
now use dynamic bindings. Sky replacement uses exact point-loaded clear depth
instead of treating valid distant geometry at depth >= .999 as background.
Scene lighting/settings also fall back to the authored world during Play,
because static scene entities are deliberately absent from the gameplay world.

`scripts/verify_viewport_output.py` checks final GPU image pixels using the
Royal launcher's real content/registry/cache paths and isolated preferences.
D3D12 Animation playback changed 20,154 preview pixels and Character scrubbing
changed 18,583; receipt: `artifacts/viewport-output-b8840118fb/receipt.json`.
After the lighting fallback, Game FreeLook changed 603,566 pixels and retained
the authored warm lighting/sky; receipt:
`artifacts/viewport-output-02caede1f7/receipt.json`.
`verify_scene_navigation.py --gate all --width 2560 --height 1600`, with an
isolated .75 UI scale for this machine's DPI, passed all six native/editor
gates, including moved-camera picking/placement and Play/Stop restoration:
`evidence/r1-scene-navigation-e3ae88407f.json`. Protected project hashes stayed
unchanged. The editor build, PreviewRoutingTests, SceneCameraTests, CameraTests,
NativeInputTests and R7RenderingTests passed. These are local GPU captures and
synthetic input checks; physical devices and remote sessions were not tested.
Vulkan also passed the same Animation playback image comparison (20,155 changed
pixels): `artifacts/viewport-output-89ba6b3c43/receipt.json`. The final Royal
Scene capture, `artifacts/viewport-output-02caede1f7/scene.png`, was visually
checked for intact distant huts and alignment of the selected NPC overlay.
The original `scripts/open_royal_scene.ps1` command uses the rebuilt executable;
no launch-script or user-preference changes are required.

The 18 October Unity-inspired toon-band pass is implemented and locally
verified. It uses four stronger paint values, coloured shadow/lit regions,
restrained ambient/local fill, attenuation-before-quantization and stepped
specular, while preserving material overrides, emission and camera ownership.
References were read from `C:/Unity Projects/AshenRootsMP`; no Unity files or
authored DarkAngel scenes/profiles were changed. Sharper bands exposed baseline
shadow-map striping, fixed with receiver-plane comparisons at PCF texel centres.
Final D3D12/Vulkan captures show foot shadows and zero clear-floor striping;
Animation playback and Character scrubbing remain live. Exact receipts, build
hash, scope and the unrelated selected-spawn exercise limitation are in
[current/TOON_BANDS_REPORT.md](current/TOON_BANDS_REPORT.md).

The subsequent outline-thickness request raises project defaults and the Royal
Combat scene's existing override to 3 px (previously 1 and 1.35 respectively).
Only thickness changes; other overrides/settings and the previous scene record
are preserved. D3D12 Character preview scrubbing passes:
`artifacts/viewport-output-02b28ec557/receipt.json`. The same folder contains
the visually checked `scene-thick-outlines.png`. No rebuild is required.

The subsequent Lighting authoring repair fixes edits being written to ignored
top-level `rendering`/`light` fields instead of native optional components.
Existing scene transactions now update `optional.rendering`/`optional.light`,
including newly created settings/lights. Lighting scene mode also provides
"Apply scene overrides to project defaults", preserving material defaults and
scene overrides while excluding scene-specific main-light IDs. Editor build,
LightingAuthoringTests (next-frame persistence, undo/redo, save/reopen and
promotion), rendering/routing tests and post-build D3D12 Animation smoke passed.
See [current/LIGHTING_AUTHORING_REPAIR.md](current/LIGHTING_AUTHORING_REPAIR.md)
for exact boundaries; direct native-UI mouse interaction was not qualified.

The Royal Crown Unity import now repairs GLTFast's black atlas payloads from the
Unity source assets before cooking. All 88 scene model roots use
`toon-standard-v1` and have their base-color texture bound; six Royal light
modules also receive the Unity-authored emissive map and tint. Tree prefab and
material GUIDs in this checkout resolve to EmaceArt/PolyVania's
`T_colorPalette2048_Transylvania.psd` (the Blink folder contains human textures,
not this tree palette); a PNG composite of that source is embedded for all
three tree variants. Registry audit confirms 88 toon materials, 88 base-color
bindings and six emission bindings. After stopping the stale editor, four
max-resolution D3D12 scene captures were rendered and visually checked:
`artifacts/royal-crown-source-export-20261018/royal-crown-01-south-approach.png`,
`royal-crown-02-west-courtyard.png`, `royal-crown-03-east-courtyard.png`, and
`royal-crown-04-aerial.png`. A fresh interactive editor is relaunched from the
same cooked registry after the captures.

The 18 October outline follow-up fixes the actual visibility bug: dark ink was
added to the scene, so black could not darken any pixel. The shared compositor
now blends ink using independent edge coverage after FXAA; point geometry/ID
reads and plane-slope rejection also suppress coplanar floor-piece seams.
After the exaggerated 8 px test, project/Crown outlines return to 1.5 px and
fade over 25-150 m using distance reconstructed from each viewport's actual
projection. Project/Crown exponential fog uses density .008; complete toon
surface radiance, including specular/emission, approaches the fog colour.
D3D12 and Vulkan GPU pixel regressions pass for disabled/1 px/8 px black ink,
metre-based fading and exponential-fog colour convergence. Exact receipts and capture
paths are in [current/TOON_OUTLINES_REPORT.md](current/TOON_OUTLINES_REPORT.md).

The 18 October Royal Crown material cleanup shares identical imported material
and texture subasset products while preserving per-model slot keys. The 88
material products reduce to five; 94 Crown texture products reduce to six.
Cross-model reuse is allowed only for matching material/texture product roles,
and package assembly rejects mismatching shared content. The importer rebuilt
the combined registry and `MaterialTests` passed. See
[current/ROYAL_CROWN_MATERIAL_DEDUP_REPORT.md](current/ROYAL_CROWN_MATERIAL_DEDUP_REPORT.md).
