# M7: VFX and audio

Started 12 October 2026. M7 In progress; nothing in it is built or Verified yet. This file opens with the dependency step and the design, written before any code.

## Dependencies (12 October 2026)

Neither library is a vcpkg port in this project (LATER_DEPENDENCY_RECIPES.md):

- **Effekseer 1.80.7** is a pinned source release in `.tools/effekseer-runtime` (authoring tool in `.tools/effekseer-editor`). Its own renderers (DX9, DX11, DX12, GL, Vulkan, Metal) stay off; M7 supplies a Diligent renderer. It is not built yet.
- **Amplitude** (commit `ff5f2f8`) is a source exception built with XMake by `scripts/build_amplitude.py`. An SDK built at M0 is in `.cache/amplitude-sdk` and passed an import/link/run probe then; it has not been initialised or built against today's toolchain.

Amplitude's providers are the manifest feature `amplitude-deps` (flatbuffers, xsimd, miniaudio, eigen3, lz4, dylib). Dry run first, as the build rule requires: `vcpkg install --dry-run` with the manifest step's arguments (triplet and host triplet `x64-windows-darkangel`, the overlay ports and triplets, the build tree's `vcpkg_installed` as install root, the recorded buildtrees root, `--x-no-default-features`, features `m1 m2 m3 m4 m5-ui m6-nav m6-bt amplitude-deps`). The plan listed every existing package as already installed and five to build and install: dylib 3.0.1, eigen3 5.0.1, lz4 1.10.0, miniaudio 0.11.25, xsimd 13.2.0. Nothing rebuilt, nothing removed. They were then installed (85 seconds in all). `VCPKG_MANIFEST_INSTALL=OFF` is unchanged; the CMake cache's feature list was not touched, so a future manifest install would need `amplitude-deps` added to it.

## Design

### What already exists to drive presentation

The runtime already publishes every moment a visual or a sound should hang on, and none of it is new:

- **Action cue blocks.** `ActionBlockKind::Cue` blocks on an action's timeline, with a key, delivered as `ActionEvent` edges when the timeline crosses them.
- **Effect cues.** An effect definition lists `EffectCueBinding {id, key}`; `EffectPresentation` turns checked public effect frames into `EffectCueUpdate` edges (`Begin`, `Update`, `End`) with a stable `EffectCueKey` (session, actor, avatar epoch, effect handle, cue asset). `CharacterSceneSession::effect_cues()` hands them to the host every step.
- **Commitments.** `commitments()` reports reserved, committed, released and rejected activations, which is what "accepted or rejected" means for a cue.
- **Motor and observed pose.** Positions, facing and joint matrices for attaching an effect to a character or a joint, and ground contact for footsteps.

So M7 adds no gameplay events. It adds two presentation services that consume the edges above, and the data that says which effect or sound a cue key means.

### The rule that shapes everything

Presentation never decides gameplay and the server plays nothing. Both services live on the client side of the session, read confirmed or checked public state only, and can be absent: a headless or no-device process runs the same session with the services constructed in a null mode that accepts every call and does nothing. A predicted ability may start a cue early for responsiveness only where the cue is marked as predictable; if the server rejects the activation the cue is stopped with a short bounded tail. That is the "accepted/rejected" gate of the milestone row.

### One seam: cues

A **cue** is the unit both services share: a cooked asset (`.dacue`) that a cue key resolves to, with an optional visual part and an optional audio part:

- visual: an Effekseer effect asset, attachment (world point, actor root, a named joint), offset, scale, whether it follows or stays where it started, and a maximum tail after `End`;
- audio: an Amplitude event name within a sound bank, attachment for spatialisation, and the same tail rule.

A `CuePlayer` (engine/runtime, renderer-independent) owns the lifecycle: it maps `EffectCueKey` and action cue edges to live cue instances, enforces bounds (a cap on live instances, a cap on tails, deduplication of a repeated `Begin`), retires instances when their actor leaves or its avatar epoch changes, and forwards start/update/stop to two interfaces, `VisualCueSink` and `AudioCueSink`. The sinks are the adapters. Tests drive `CuePlayer` with recording sinks, so lifecycle is verified without a GPU or an audio device.

### Effekseer adapter

- Build the pinned runtime as a static library with every bundled renderer off, as the recipe says.
- Simulation: one `Effekseer::Manager` per client session, updated on the render clock (effects are presentation; they do not follow the fixed tick exactly, only start and stop on it).
- Rendering: implement Effekseer's renderer interfaces (`SpriteRenderer`, `RibbonRenderer`, `RingRenderer`, `TrackRenderer`, `ModelRenderer`, plus texture and model loaders) on Diligent: one dynamic vertex buffer, a small set of pipeline states keyed by blend mode, depth test/write and culling, and the engine's cooked textures through Effekseer's loader callbacks. Drawn after opaque scene geometry and before the HUD, with the scene's depth. Distortion and Effekseer materials (custom shaders) are out of the first slice; an effect that needs them is refused at cook time with a message, not drawn wrongly.
- Assets: `.efkefc` sources are imported like any other source; the cooker stores the runtime `.efk` bytes and records its texture and model dependencies in the scene closure.
- Where it lives: `engine/render/effekseer_*` behind `VisualCueSink`. The editor and a future game executable both construct it; the runtime library does not link Effekseer.

Not checked yet: the interface names above are Effekseer's as I know them from its documentation, not read from the pinned 1.80.7 source. Read `.tools/effekseer-runtime/src/Effekseer` and `EffekseerRendererCommon` before step 2 and correct this section if they differ.

### Amplitude adapter

- Link the SDK built by `scripts/build_amplitude.py` through `cmake/Amplitude.cmake` (rebuild it first against today's toolchain and installed providers, and re-run its import probe).
- One engine instance per process, initialised with a project file and sound banks cooked from authored sources; a listener that follows the active camera; a game object per actor that has live audio.
- `AudioCueSink` maps cue start/stop to Amplitude events on the actor's entity, with positions updated each frame.
- No device: when the process has no audio device or was started with a no-audio flag the adapter runs without output (through a null driver if the pinned SDK has one, otherwise by not initialising the engine); it still tracks instances so the lifecycle tests are the same. Which of the two is not checked against the pinned source yet.
- Where it lives: `engine/audio/amplitude_*` behind `AudioCueSink`.

### Authoring (after the two adapters play something)

- Cue asset as a native source type (catalog, drafts, Undo/Redo, validation, Save), edited in a form; the existing effect form's cue list and the action Composer's cue blocks pick a cue by asset instead of a bare key.
- Preview in the editor: play a cue on the preview character without a session.
- Placement decision (mine, as allowed): a **Cues** section inside the Ability tab's asset list rather than a new top-level tab, because cues are referenced only from abilities and effects; Effekseer's own editor stays the tool for building an effect.

### Order of work

1. `CuePlayer`, the two sink interfaces, the cue asset and its cooker, lifecycle tests with recording sinks (no new dependency used). One chunk.
2. Effekseer runtime built and linked; the Diligent renderer for sprites and ribbons; one owned effect (a hit spark on the Light attack's hit, a Burn aura on its effect cue) in Editor Play, with a capture looked at. One chunk, the largest.
3. Amplitude rebuilt and linked; the adapter; one owned sound (swing, hit); a no-device run. One chunk.
4. Authoring form, preview, rings/tracks/models, trails and decals, footsteps, ambient and music.

### Gates the milestone row requires, and where they come from

- Real effect and sound through an accepted activation, and stopped with a bounded tail through a rejected one: steps 2 and 3, with step 1's lifecycle tests underneath.
- Bounded tails and resource retirement: `CuePlayer` bounds and a test that leaves no live instance after its actors are removed.
- No-device and headless operation: null sinks in step 1, null audio driver in step 3, CTest running the scene tests unchanged.
- An agent changes actual effect or event content, not only wrapper parameters: step 4 (the cue asset and imported Effekseer source are both real sources with history).

### Open questions for the user (not blocking steps 1 and 2)

1. Sounds: there are no audio sources in the repository. Which sounds should the first owned cues use, or may I generate simple placeholder tones for swing and hit?
2. Effects: the same for Effekseer effects. The authoring tool is acquired; may the first hit spark and Burn aura be simple effects I author, to be replaced later?
3. Should a predicted cue (the swing's own sound and trail) start on the owner's prediction and be cut if rejected, or wait for confirmation? The design supports both per cue; the default I would pick is predicted for the owner's own swing, confirmed for hits.
