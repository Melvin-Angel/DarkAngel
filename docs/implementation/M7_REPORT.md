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
- Placement: superseded on 12 October 2026 by the user's request for a VFX tab. Effects and cues are authored in a top-level **VFX** tab (see "VFX tab" below), not in a Cues section of the Ability tab.

### Order of work

1. `CuePlayer`, the two sink interfaces, the cue asset and its cooker, lifecycle tests with recording sinks (no new dependency used). One chunk.
2. Effekseer runtime built and linked; the Diligent renderer for sprites and ribbons; one owned placeholder effect each for a hit spark on the Light attack's hit and a Burn aura on its effect cue, in Editor Play, with a capture looked at. One chunk, the largest. It settles how an effect source becomes `.efk` (see "VFX tab").
3. Amplitude rebuilt and linked; the adapter; placeholder tones for swing and hit, the swing predicted for the owner; a no-device run. One chunk.
4. The VFX tab: catalog, effect form, preview viewport, the cue form. Then rings/tracks/models, trails and decals, footsteps, ambient and music.

Runtime UI as authored content ([UI_PLAN.md](UI_PLAN.md)) is not part of this order: the user deferred it on 12 October 2026.

### Gates the milestone row requires, and where they come from

- Real effect and sound through an accepted activation, and stopped with a bounded tail through a rejected one: steps 2 and 3, with step 1's lifecycle tests underneath.
- Bounded tails and resource retirement: `CuePlayer` bounds and a test that leaves no live instance after its actors are removed.
- No-device and headless operation: null sinks in step 1, null audio driver in step 3, CTest running the scene tests unchanged.
- An agent changes actual effect or event content, not only wrapper parameters: step 4 (the cue asset and imported Effekseer source are both real sources with history).

### User answers (12 October 2026)

The three questions that stood here are answered:

1. Sounds: author simple placeholder tones for swing and hit. They are generated by a script kept in the repository, so they can be regenerated and are plainly placeholders, and are replaced by real sources later.
2. Effects: author simple Effekseer effects for the hit spark and the Burn aura, to be replaced later.
3. Prediction: the owner's own swing cue starts on prediction and is cut with the short tail if the activation is rejected. Hits wait for confirmation. Cues of other actors are always confirmed, since nothing predicts them.

The same message asked for two additions to the plan:

- A **VFX tab** in the editor where Effekseer effects are created. This replaces the earlier placement decision ("Effekseer's own editor stays the tool for building an effect"); see "VFX tab" below.
- **Runtime UI as authored content**: a UI tab for editing UI documents, UI documents attached to NPCs (world space) and to the Player (screen-space HUD, world space later), and a place for main menus in the same system. That is its own plan, [UI_PLAN.md](UI_PLAN.md), which the user deferred the same day.

### VFX tab (added 12 October 2026, user request)

A top-level **VFX** workspace (`Workspace::Vfx`), matching item 9 of the vision ("VFX Designer"). It is where an effect is created, edited, previewed and bound, without leaving the editor.

What it holds:

- **Catalog** of effect assets under `content/vfx/`, with New, Duplicate and Delete as ordinary draft operations (Undo/Redo, validation, coordinated Save), like every other native source.
- **Effect form.** An effect is a tree of nodes; each node has spawn (count, rate, lifetime, delay), shape and motion (position, velocity, acceleration, rotation, scale, each fixed, random in a range or eased over life), and a drawing kind (sprite, ribbon, ring, track, model) with texture, colour over life and blend mode. The form edits the subset the Diligent renderer draws; a node kind or feature the renderer does not support cannot be created here, and an imported effect that uses one is refused at cook time, as already designed.
- **Preview viewport**: the effect playing in a loop on its own, or attached to the preview character at a chosen joint; play, pause, restart, a time scrubber and live instance and particle counts. No session is involved.
- **Cues**: the cue asset (`.dacue`) form moves here from the planned "Cues section of the Ability tab", because a cue is where an effect and a sound are bound to an attachment and a tail, and the preview is here. The Ability tab's effect form and the Composer's cue blocks still pick a cue by asset.
- **Import** of `.efkefc`/`.efkproj` made in Effekseer's own editor stays supported; the tab does not replace that tool for effects beyond the supported subset.

The decision that has to be made before building it, and what is not checked:

- The Effekseer runtime loads compiled `.efk` binaries. The editable sources (`.efkproj`, XML; `.efkefc`, a container around it) are compiled by Effekseer's editor. For the tab to create an effect, the engine needs one of: (a) a native effect source (`.davfx`, JSON, the form's own data) that our cooker writes to `.efk` directly for the supported subset; (b) the form writing `.efkproj` and the cooker running the acquired Effekseer tool's command-line export. (a) has no tool dependency at cook time and keeps headless cooks simple, but means owning a writer for a binary format that changes between Effekseer versions (the runtime is pinned, so the format is fixed for now). (b) reuses Effekseer's exporter and keeps files openable in Effekseer's editor, but puts a Windows GUI tool in the cook path. My default is (b) if the pinned editor has a working command-line export, with the exported `.efk` stored as the cooked product so a cook without the tool can still reuse it; otherwise (a). Not checked: whether Effekseer 1.80.7's editor exports from the command line without a window, and the `.efkproj` schema for the node features listed above. Check both when step 2 reads the pinned source; the placeholder effects of step 2 need the same answer, since I cannot use the GUI.
- The first placeholder effects (hit spark, Burn aura) are authored through whichever path is chosen, so they are the first test of it.

## Step 1, first part: the cue player (12 October 2026)

Built: `engine/runtime/cue_player.hpp/.cpp`. `CuePlayer` takes effect cue edges, action cue block events, rejected activations and actor retirement, resolves a cue key to a `CueDefinition` through a resolver the host supplies, and forwards start, stop (with a tail) and release to a visual and an audio `CueSink`. Either sink may be absent.

Rules it enforces, each with a test in `CuePlayerTests` (CTest `M7.cue_player`):

- An effect cue starts once however often its `Begin` arrives, stops with its tail on `End`, and is released when the tail is over. Cues of different effect instances or actors are separate. A cue that begins again during its tail replaces the fading instance.
- An action cue block with length starts and stops; a marker is a one-shot that fades by its tail; hit windows and other block kinds are not cues.
- A cue goes only to the sinks it names; tails are held to the player's limit (180 ticks).
- A rejected activation's cues are cut with a short tail (6 ticks), fading ones included; other activations and actors are untouched.
- An actor that leaves has its cues released at once.
- Bounds: at most 128 live instances by default; a full player gives the place of the instance that fades out first to a new cue, and drops the new cue (counted) when nothing is fading.
- A key without presentation is counted and holds nothing.
- Without sinks the lifecycle is identical; destroying the player releases what the sinks hold.

Differences from the design: one `CueSink` interface serves both the visual and the audio side (the design named two); the cue definition is a plain struct until the cue asset exists.

**Not built yet in step 1:** the `.dacue` asset and its cooker, and feeding a player from `CharacterSceneSession` (effect cues are published there; action cue block events of observed actors are not surfaced to the host yet and need an accessor). Nothing calls the cue player outside its test.

**Verified:** `CuePlayerTests`; a full build; CTest with only the five known unrelated failures (`M7.cue_player` passes). The 50 editor gates were not re-run: this adds a file nothing else calls.

## Step 1, second part: action cues from the session, the cue asset, cues in Editor Play (12 October 2026)

This finishes step 1. A Play session now drives a cue player from what the client itself holds, the cue is a cooked asset, and the editor shows what a visual and an audio service would be told to play. Nothing is drawn or heard yet: that is steps 2 and 3.

### Built

**Action cues, derived on the client.** `ActionCueTracker` (engine/runtime/cue_player.cpp) is shown each actor's running action once a tick (its identity and clock) and reports the cue blocks the clock passed since the tick before. `CharacterSceneSession` feeds it from three places, none of them a server timeline:

- every other actor: its public ability frame, brought to the tick the same way its pose is;
- the player, confirmed: the owner correction;
- the player, predicted: the owner's ability prediction.

`CharacterSceneSession::action_cues()` and `action_cue_cuts()` give the result per tick; `present_cues(CuePlayer*)` lets the session drive a cue player directly on every fixed tick (effect cue edges, action cues, cuts, actors leaving, the tick), so nothing is lost when one frame runs several ticks. `identity(network)` maps the network identity in a cue record back to a scene identity.

Rules, each with a test:

- A cue block is reported once, on the tick the clock passes it; the block at the start of an action on its first tick; the blocks of the last tick when the action runs out.
- A clock that moves back and forward again (a replayed prediction) repeats nothing.
- An action that stops before its time (interrupted, cancelled, replaced, or predicted and then rejected) is reported as a **cut**, and the cue player cuts its cues with the short rejection tail (6 ticks).
- An action first seen more than four ticks in does not replay its earlier cues. An actor that leaves, or a new avatar, reports nothing.

**Predicted and confirmed (user decision of 12 October).** The owner's action is seen twice. `CuePlayer::action` takes a `CueTiming`: a cue marked `predictable` plays from the predicted stream and every other cue from the confirmed stream, so each plays once. Other actors' cues are always from confirmed public state. Rejection needs no extra message: when the prediction drops an activation before its end, that is a cut.

**The cue asset.** `.dacue` is a native source (`decode_cue`): name, the key it answers to, a visual name, an audio name, tail and `predictable`; at least one of visual and audio is named. `engine/assets/cue_cooker.cpp` cooks it as a leaf product (`cue.json`); the asset service knows the extension for the catalog, cook, native edit, create and coordinated Save. `load_cooked_cues` returns every cue of a cooked registry and refuses two cues answering to one key. Owned content: `content/royal_district/combat/cues/swing.dacue` (key `Attack.Start`, audio, predictable) and `burn.dacue` (key `Status.Burn`, the cue identity the Burn effect already names, visual). Their visual and audio fields are names of an effect and a sound that do not exist yet.

**Editor Play.** Starting Play builds a cue player for the session with two recording sinks (`apps/editor/cue_debug.hpp`) standing in for the visual and the audio service. The cues are every cue asset of the project, cooked and loaded from their frozen products. The Gameplay Inspector has a **Cues** tab: number of cue assets, live cues (key, actor, effect or action, start, fade), keys without a cue asset, and what each sink was told, latest last.

### Differences from the design, and limits

- **Cue blocks are markers.** The action runtime only allows a cue block without length, so an action cue is always a one-shot that fades by its tail. Cues with a length come from effects. `CuePlayer` still accepts begin and end edges for action cues, but nothing produces them.
- **Cuts instead of rejection messages.** The design fed the cue player from `commitments()`. That list is drained from the server side of the loopback session, so the session does not use it for cues; the cut derived from the client's own prediction covers rejection, and also covers an action interrupted by a hit.
- **Cues reach Play outside the package closure.** An action names a cue by key only, and an effect's cue identity is not a dependency of the effect product, so cues are not in the Play package through what uses them. The editor packages all cue assets on their own (at most 64, a limit of the package call). Making a cue a real dependency of the action or effect that names it belongs with the cue form in step 4.
- **No hit cue.** A cue on the hit (the "confirmed for hits" half of the decision) needs a moment to hang on. Today that is an effect the hit applies (Burn, Flinch); there is no public hit event without an effect. The first hit spark in step 2 is bound to an effect cue; a plain hit event is a follow-up if that is not enough.
- **Reaction and death timelines** have cue blocks too and are not tracked yet; only ability actions are.
- **No cue form.** A `.dacue` is in the catalog and cooks, but has no editor form until the VFX tab (step 4).
- An effect whose cue identity has no cue asset falls back to its key, so the authoring fixture's copied Burn (another identity, same key) resolves to the Burn cue.

### Verified

- `CuePlayerTests` (`M7.cue_player`): the tracker rules above, the predicted and confirmed streams, the cue source and its refusals.
- `CueAssetTests` (`M7.cue_assets`, new): the owned cues and the bindings they answer to, catalog, cook, package and load, a failed cook keeping the frozen product, native edit and create, one cue per key.
- `CharacterCombatSceneTests`: in a real session the owner's Light reports its cue once predicted and once confirmed, the predictable swing cue starts on the tick of the press and plays once; an NPC's Light is reported once, on its tick, from its public state.
- Editor Play, D3D12, two new gates in `scripts/verify_effect_reaction.py`: `cues-swing-d3d12` (Light at tick 0: audio sink told to start `Attack.Start` once, faded and released by tick 30, nothing on the visual sink) and `cues-burn-d3d12` (Heavy hit: visual sink told to start `Status.Burn`, still live at tick 30, nothing on the audio sink). The Cues tab was looked at in the Burn capture.
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 88 of 93, the five others being the known unrelated ones (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`); `python scripts/verify_effect_reaction.py` ran 52 gates (the earlier 50 and the two cue gates) and all pass. Both cue captures were looked at ([swing](evidence/cues-swing-d3d12.png), [Burn](evidence/cues-burn-d3d12.png)).

Not verified: a predicted cue that authority then rejects, in a real session (loopback never rejects a prediction on its own; the cut is covered by the tracker and cue player tests only); more than one frame's worth of ticks in one editor frame; a project with no cue assets beyond the code path that returns an empty list.
