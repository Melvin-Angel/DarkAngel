# Animation editor overhaul and camera routing

Date: 17 October 2026

This is the implementation report for the user-requested animation-editor and
preview/camera regression work. The native graph runtime now supports a
serialized multi-state machine, typed parameters and nested transition
conditions while retaining the existing clip/blend evaluation path.

## Root causes found

### Preview routing

The editor used one display texture, but it did not have one authoritative
isolated-content route. Character and graph previews only changed the selected
skin while the renderer continued to start from scene-owned `drawn_objects`.
Composer filtering was conditional on `composer_visible` and the current
workspace, and action previews did not provide an uploaded authoring skin for
camera bounds. Scene-origin lights and rendering settings were also collected
for preview frames. This allowed scene placement/camera/context to leak into
authoring views and, for action previews, the terrain radius could determine
framing.

The correction keeps the existing display render target but makes content
ownership explicit: preview frames clear the scene object list, add a synthetic
frame-local preview actor, use the prepared character skin when available, use
the scene character skin only as the action-preview fallback, skip scene lights,
and orbit around the selected skin bounds. Preview state remains in
`PosePreviews`; no gameplay world or persistent scene object is created.

### Play camera

`PlayController::game_camera()` returned a camera only when
`Shell::workspace == Workspace::Game`. A valid runtime session could therefore
have a possessed player and live camera while Animation, Ability, Character or
Scene continued to render the editor camera. The gate now uses active Play plus
the possessed character. The Game workspace also reports the live camera name
and explicitly reports the restored Scene-camera state when stopped.

## Implemented in this increment

- Shared `PreviewRouting` policy and `R7.preview_routing` regression test.
- Synthetic isolated preview actor, bounds-based character framing, scene-light
  exclusion, and action-preview character-skin fallback in
  `apps/editor/render/scene_viewport.cpp`.
- Play camera selection independent of the current authoring workspace.
- Graph canvas pan, wheel zoom, auto-layout reset, larger canvas, and readable
  graph-first toolbar. Empty-space context commands add 1D/2D blend nodes;
  node context commands duplicate/delete; existing drag-to-input connection
  operations remain native authoring commands with history/serialization.
- Composer timeline sizing increased substantially, with timeline zoom and
  context commands for adding tracks/blocks, paste, block copy/duplicate/delete.
  Mutations call `NativeAuthoring` and therefore update the real draft/history,
  not an editor-only model.
- Ability tab now has a shared action preview and an explicit
  `Open in Animation Composer` route. Composer block editing remains in the
  Animation workspace.
- Schema-2 locomotion assets now serialize parameters, multiple named states,
  state roots, transition priority/crossfade/exit-time fields and recursive
  `Always`, `HasTag`, typed comparison, `NOT`, `ALL`, `ANY`, `NONE` and `XOR`
  conditions. Schema-1 graphs remain loadable and can be upgraded from the
  editor.
- `AnimationGraphInstance` exposes validated Float/Integer/Boolean setters and
  defaults; `ScriptRuntime` exposes matching `Context:set_anim_*` and
  `Context:get_anim_*` bindings backed by a shared parameter store.
- Blend-space samples can be selected and dragged directly in the editor. The
  drag is constrained to valid 1D ordering and records native authoring history.
- Composer block taxonomy now includes VFX, Audio, Gameplay, Projectile, AoE
  and Hitbox blocks in decoding, serialization, colors, context menus and the
  authoring form. Action preview recomputes active presentation blocks on every
  scrub/reset, so leaving a block deterministically clears its isolated
  presentation state without executing gameplay authority.
- Character and Ability workspaces use the same right-side character-focused
  preview column as the Animation workspace; Ability keeps Composer editing in
  Animation and routes only the assigned action preview into that panel.
- Game workspace camera-status text and real D3D12 captures for the changed
  workspaces.

## Real editor captures

These are captures from the built `DarkAngelEditor.exe`, not generated
mockups:

- [Locomotion graph](../evidence/animation-overhaul-locomotion.png)
- [Composer timeline](../evidence/animation-overhaul-composer.png)
- [Character preview](../evidence/animation-overhaul-character.png)
- [Ability shared preview](../evidence/animation-overhaul-ability.png)
- [Locomotion graph, current wide capture](../evidence/animation-overhaul-locomotion-shared.png)
- [Composer timeline and preview, current wide capture](../evidence/animation-overhaul-composer-shared.png)
- [Character shared right-panel preview, current wide capture](../evidence/animation-overhaul-character-shared.png)
- [Ability shared right-panel preview, current wide capture](../evidence/animation-overhaul-ability-shared.png)
- [Play gameplay camera](../evidence/animation-overhaul-play-camera.png)
- [Stopped Scene camera](../evidence/animation-overhaul-scene-camera-restored.png)

The screenshots show the graph/Composer/Ability preview subjects framed apart
from RoyalCombat scene placement. The Play capture visibly says
`Gameplay camera: FreeLook`; the stopped capture is back in Scene / Level
Design with the scene viewport and inspector.

## Verification

Passed in this worktree:

- `DarkAngelEditor` and `PreviewRoutingTests` build with the pinned MSVC/CMake
  environment.
- `GraphAssetTests.exe` passed schema-1 compatibility plus serialized
  multi-state graph cooking, typed parameters, nested conditions and return
  transitions.
- `AnimationGraphTests.exe` passed native 1D/2D graph, tag and deterministic
  fixed-tick coverage.
- `ScriptPackageTests.exe imports`, `tasks`, `bindings`, `state` and `cooked`
  passed; `bindings` includes a Lua Float/Integer/Boolean parameter round trip.
- `R7.preview_routing`.
- `R1.scene_camera`.
- `M5.graph_assets`.
- `M5.graph_runtime`.
- `M5.composer_preview`, `M5.native_authoring_graph_commands`, and `M5.camera`
  direct executable checks.
- Focused CTest selection passed 10/10: `M5.graph_assets`, `M5.graph_runtime`,
  `M5.composer_structure`, `M5.composer_preview`, `M5.native_authoring`,
  `M5.native_authoring_graph_commands`, `M5.native_authoring_effect_preview`,
  `R7.preview_routing`, `R1.scene_camera`, and `M5.camera`.
- `NativeAuthoringTests.exe --fixture-only` completed and produced the current
  `authoring-fixture.json` used by the Composer preview test.
- `RendererProbe` created and cleared offscreen targets on both D3D12 and
  Vulkan.
- Real D3D12 editor captures listed above, including Play and stopped Scene
  camera frames.

The old `scripts/verify_character_preview.py` wrapper is
stale: its native fixture no longer contains the hard-coded
`characters/gui_character.dacharacter`, so its capture setup failed before the
editor was launched. This is a verification-harness issue, not evidence that
the preview renderer failed.

## Explicitly incomplete

The following acceptance items remain open and must not be inferred from this
increment:

- any-state transition evaluation still needs a dedicated snapshot-validation
  and interruption-policy test matrix; the serialized `from: 0` form is
  accepted by the runtime but has not yet received a full visual qualification
  pass.
- visual transition-condition editing is present, but graph breadcrumbs,
  graph multi-selection and graph-wide copy/paste remain incomplete.
- Composer taxonomy and deterministic isolated block cleanup are implemented;
  actual VFX/audio/projectile/AoE/hitbox asset playback and debug geometry in
  the character image remain incomplete.
- dedicated preview studio lights/floor/grid and Vulkan visual capture;
- complete automated repeated Play/Stop/camera movement and Scene/Game switch
  qualification on both D3D12 and Vulkan;
- ability-effect presentation coverage beyond the shared animation preview.

