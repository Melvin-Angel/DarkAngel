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
2. Effekseer runtime built and linked; the Diligent renderer for sprites and ribbons; one owned placeholder effect each for a hit spark on the Light attack's hit and a Burn aura on its effect cue, in Editor Play, with a capture looked at. One chunk, the largest. It checks the Effekseer editor's command-line export, which the cooker and the placeholder effects both depend on (see "VFX tab").
3. Amplitude rebuilt and linked; the adapter; placeholder tones for swing and hit, the swing predicted for the owner; a no-device run. One chunk.
4. The VFX tab: catalog, "Edit in Effekseer" with re-cook on save, preview viewport, the cue form. Then rings/tracks/models, trails and decals, footsteps, ambient and music.

Runtime UI as authored content ([UI_PLAN.md](../../future/UI_PLAN.md)) is not part of this order: the user deferred it on 12 October 2026.

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

- A **VFX tab** in the editor for Effekseer effects. Later the same day the user chose how: Effekseer's own editor as a companion tool opened from the tab, no particle form of our own, no live link and no window embedding; see "VFX tab" below.
- **Runtime UI as authored content**: a UI tab for editing UI documents, UI documents attached to NPCs (world space) and to the Player (screen-space HUD, world space later), and a place for main menus in the same system. That is its own plan, [UI_PLAN.md](../../future/UI_PLAN.md), which the user deferred the same day.

### VFX tab (added 12 October 2026, user request; revised the same day)

A top-level **VFX** workspace (`Workspace::Vfx`), item 9 of the vision ("VFX Designer"). User decision of 12 October 2026: effects are built in Effekseer's own editor, opened from the tab as a companion tool. The tab does not have its own particle form, Effekseer's window is not placed inside ours, and there is no live link between the two; an edit shows up when the file is saved.

What the tab holds:

- **Catalog** of effect assets under `content/vfx/`: New (from a small template effect), Duplicate, Delete, Import of an existing `.efkefc` or `.efkproj`.
- **Edit in Effekseer**: opens the selected effect's source in the acquired Effekseer editor (`.tools/effekseer-editor`). The editor watches the source file; when it is saved there, the effect is cooked again and the preview and any running Play pick up the new product. A failed cook keeps the previous product and shows the reason in the tab.
- **Preview viewport**, drawn by our own Diligent renderer, so it shows what the game will show: the effect in a loop on its own, or attached to the preview character at a chosen joint; play, pause, restart, a time scrubber, live instance and particle counts. No session is involved.
- **What our renderer does not draw.** An effect can use features that look right in Effekseer and that our renderer lacks (distortion and custom materials in the first slice). The cook refuses such an effect, as already designed, and the tab names the feature and the node, so the difference between the two tools is never silent.
- **Cues**: the cue asset (`.dacue`) form lives here (not in a Cues section of the Ability tab), because a cue is where an effect and a sound are bound to an attachment and a tail, and the preview is here. The Ability tab's effect form and the Composer's cue blocks pick a cue by asset; that is also when a cue becomes a real dependency of what names it.

How a source becomes what the runtime loads, and what is not checked:

- The Effekseer runtime loads compiled `.efk` binaries; the sources Effekseer's editor saves (`.efkefc`, and `.efkproj` in XML) are compiled by that editor. The cooker therefore needs a way to compile without a person: the acquired editor's command-line export, with the exported `.efk` stored as the cooked product so a cook on a machine without the tool reuses it. Not checked: whether Effekseer 1.80.7's editor exports from the command line without opening a window. Step 2 checks it first. If it cannot, the fallback is that the user exports `.efk` from Effekseer next to the source and the cooker takes that file; our own `.efk` writer is no longer planned.
- The first placeholder effects (hit spark, Burn aura) are authored by me without the GUI, by writing the `.efkproj` XML directly, starting from one of the samples shipped with the editor, and compiling it through the same export. They are the first test of that path. Not checked: the `.efkproj` schema; read it from the samples and the pinned source.

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
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 88 of 93, the five others being the known unrelated ones (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`); `python scripts/verify_effect_reaction.py` ran 52 gates (the earlier 50 and the two cue gates) and all pass. Both cue captures were looked at ([swing](../../evidence/cues-swing-d3d12.png), [Burn](../../evidence/cues-burn-d3d12.png)).

Not verified: a predicted cue that authority then rejects, in a real session (loopback never rejects a prediction on its own; the cut is covered by the tracker and cue player tests only); more than one frame's worth of ticks in one editor frame; a project with no cue assets beyond the code path that returns an empty list.

## Step 2: Effekseer on Diligent (12 October 2026)

A cue's visual is now a real effect: authored as an Effekseer project, cooked through the Effekseer tool, simulated by the Effekseer runtime and drawn by our own renderer in Editor Play. The owned Burn aura is the first one.

### What was checked first

- **Command-line export.** The acquired Effekseer 1.80.7 editor compiles a project without opening a window: `Effekseer.exe -cui -in <source> -e <output.efk>`, for both `.efkproj` (XML) and `.efkefc` (packed). It exits with 0 even when it fails: a malformed project writes no output, and a missing input writes an empty effect. The cooker therefore trusts only the output file, never the exit code, and never passes a path that does not exist.
- **Renderer interfaces** (correcting the design above). Effekseer's shared renderer code (`EffekseerRendererCommon`) already builds the geometry of sprites, ribbons, rings and tracks; those classes are templates over a backend renderer and need only its camera, a quad budget and a "standard renderer" with three functions that hands out vertex memory per render state. So this renderer does not implement the node renderers itself, as the design said; it supplies that small backend and reuses Effekseer's node renderers unchanged.

### Built

- **Effekseer runtime in the build** (`darkangel_add_effekseer` in cmake/later-recipes.cmake): the pinned source's `Effekseer` and `EffekseerRendererCommon` static libraries, every bundled renderer, sound backend, network and example off. No vcpkg package was touched.
- **Effect assets** (engine/assets/vfx_cooker.cpp, `darkangel/vfx_assets.hpp`). `.efkproj` and `.efkefc` are asset sources with the usual import sidecar (importer `effekseer-v1`). Cooking compiles the source with the tool and writes one product: the compiled effect together with its colour textures, each cooked by the existing texture cooker and stored under the path the effect asks for. The textures a source names are read without the tool (from the XML, or from the packed file's resource list), so a changed texture is a changed effect. Refused by name at cook time: normal maps (lit particles), distortion, models, custom materials, and sounds inside the effect.
- **Placeholder effects** (`content/vfx/placeholder`, written by `scripts/generate_placeholder_vfx.py`): a soft glow texture, a hit spark and a Burn aura. They are plainly stand-ins and are replaced by editing them in Effekseer.
- **Visual effect service** (engine/vfx, library `DarkAngelVfx`, `darkangel::VfxService`): loads a cooked effect, plays it at a position, moves it, stops it spawning or removes it, advances all effects, and draws them into the bound render targets against their depth. One dynamic vertex buffer, one pipeline per blend mode and depth state made on first use, unlit shading (texture times colour, with Effekseer's emissive scaling). It reports loaded effects, playing effects, particles, quads, draw calls and skipped batches.
- **Cues play effects** (apps/editor/vfx_cue_sink.hpp). A cue's `visual` names an effect asset by its source path. The cue player's start, stop and release become play, stop spawning and remove; an effect follows its actor (root position plus one metre). A one-shot cue plays out and is removed on release. The Burn cue now names `vfx/placeholder/burn_aura.efkproj`.
- **Editor Play** creates one service on the device, cooks and loads the effects its cues name when Play starts, advances effects on the session's tick clock (so a gate is repeatable) and draws them after the opaque scene. The Cues tab shows the service's counts.

### Limits

- **Unlit only.** Lit particles, distortion, models and custom materials are not drawn. Cooking refuses the effects that are known to need them; a batch that needs other shading at run time is skipped and counted, never drawn wrongly. Of Effekseer's "advanced" unlit options (flipbook blending, UV distortion, alpha cutoff, soft particles) only the plain texture and colour are used.
- **No culling.** Both faces are always drawn.
- **Attachment** is the actor's root plus one metre. Joints and authored offsets come with the cue form.
- **The hit spark is authored and cooked but not bound.** Binding it means adding a cue to the owned Flinch effect, which changes owned gameplay content that many tests pin; it is done together with the hit sound in step 3 so that content changes once.
- **Editor only, D3D12 looked at.** The service is renderer-independent of the editor, but nothing else constructs it yet; the Vulkan backend was not run.
- **Placeholder projects say they come from Effekseer's oldest tool version**, because they use that schema's element names (the shipped samples'); Effekseer upgrades them on load. Naming the current tool version made it skip the upgrade and silently drop the colours.
- **Cook needs the tool.** A machine without `.tools/effekseer-editor` cannot cook an effect source that is not already cooked.

### Verified

- `VfxAssetTests` (`M7.vfx_assets`, new): the owned placeholders in the catalog; compile through the tool, package and load with the texture under its path; a warm cook does nothing; a changed texture cooks again and a missing one is refused; a malformed source is refused; a packed sample project with three textures cooks; a sample that uses distortion is refused by name.
- Editor Play, D3D12, gate `cues-burn-d3d12` (extended): after the Heavy hit the Burn aura is loaded, playing, has particles and is drawn in at least one draw call with nothing skipped and no cue left without its effect. The [capture](../../evidence/cues-burn-d3d12.png) was looked at: an orange glow and embers around the burning dummy, hidden where the player stands in front of it, so the scene depth is respected.
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 89 of 94 with the five known unrelated failures; all 52 verify gates pass.

Not verified: ribbons, rings and tracks on screen (the shared code is built and reached the same way as sprites, but no owned effect uses them yet); blend modes other than additive; Vulkan; an effect stopping and fading at the end of Burn seen by eye; many effects at once.

## Step 3: audio on Amplitude (12 October 2026)

A cue's sound is now a real sound: a WAV asset, cooked, loaded into the Amplitude engine and played at the actor when the cue starts. The owner's swing plays on the predicted press; the hit plays when the Light's hit applies Flinch.

### What was checked first

- **The SDK built at M0 still works.** `scripts/build_amplitude.py --import-only` configures, links and runs the import probe against today's toolchain and the installed `amplitude-deps` packages. The SDK was not rebuilt.
- **A silent driver exists.** The pinned Amplitude has a `null` driver beside `miniaudio`; a config that names it starts the whole engine without opening a device, and sounds still start, last their length and end. That is the no-device mode; nothing has to be faked above the engine.
- **No HRTF data is needed** when the mixer's panning mode is stereo, and the WAV decoder is built in.

### Built

- **Sound assets** (engine/assets/sound_cooker.cpp, `darkangel/sound_assets.hpp`). A `.wav` file with an import sidecar (importer `sound-wav-v1`) is a source; a WAV file without a sidecar is not, like an image. Cooking validates it (16 or 24 bit PCM or 32 bit float, mono or stereo, 8 to 96 kHz, at most 16 MB) and stores it as it is.
- **Placeholder sounds** (`content/audio/placeholder`, written by `scripts/generate_placeholder_audio.py`): a swing (swept noise) and a hit (a falling sine with a noise burst). Plainly stand-ins.
- **Audio service** (engine/audio, library `DarkAngelAudio`, `darkangel::AudioService`). Amplitude wants a project on disk in its own binary formats; the service writes the smallest one it accepts into a folder of its own (engine config, a master bus with one child, the stereo pipeline, one distance attenuation: full gain within 2 m, silent at 40 m) and one sound definition and bank per loaded set, compiled in memory from JSON with the SDK's own schemas through the FlatBuffers parser. No `flatc` run and no Amplitude authoring tool is involved. It loads cooked sounds, plays one at a position, moves it, fades it out, follows a listener, advances, and reports the driver, loaded sounds, playing sounds and how many were started. A process has one; it can be destroyed and made again; it removes its folder.
- **Cues play sounds** (apps/editor/audio_cue_sink.hpp). A cue's `audio` names a sound by its source path, as `visual` names an effect. A one-shot cue's sound plays out; a lasting cue stopped later fades by the cue's tail.
- **The hit** (content): a new cue `hit.dacue` (key `Status.Flinch`, the hit spark and the hit sound, not predictable) and a cue binding on the owned Flinch effect. Flinch is what the Royal kit's Light hit applies, so the spark and the thud appear on the actor that was hit, from confirmed state.
- **Editor Play** makes the audio service when the first Play needs it, on the audio device, or on the silent driver for a hidden run or with the new `--no-audio` flag. The listener is the Play camera. The Cues tab shows the driver and the counts.

### Differences from the design, and limits

- **Sounds are added, never unloaded** while the service lives. Unloading a sound bank while the mixer still referred to it crashed in the first version, so `load` only adds: a sound in a generation already loaded is left alone and a new generation of an asset is played from then on. A service holds at most 512 sound generations before it must be made again.
- **No events, buses, RTPCs, switches or collections are authored.** Amplitude's own authoring model (events and banks as assets a designer edits) is not exposed; a cue plays one sound directly. Bus routing, mixing and randomised collections are step 4 work.
- **One attenuation for every sound**, stereo panning only (no HRTF), no reverb, occlusion or environments.
- **Positions**: the listener orientation is passed as forward and up vectors in our right-handed, Y-up space. That the left and right channels are the right way round was not heard.
- **Not heard at all.** Every run so far used the silent driver, or started the device without playing anything. Whether the placeholder tones sound acceptable needs a person at the machine: start Play in the editor and swing.
- **The schemas are read from the SDK folder in `.cache`** at run time; a packaged game needs them shipped (M9).
- **Clock.** In the editor the audio engine is advanced on the session's tick clock, like effects; the mixer itself runs on Amplitude's own thread in real time.

### Verified

- `AudioTests` (`M7.audio`, new): the placeholder sounds in the catalog, cook, package and load with their format; a file that is not WAVE, a broken source and a WAV file without a sidecar are refused or ignored; the service on the silent driver loads, plays two sounds, moves one, lets a sound end by its length, ends a stopped sound early, leaves loaded sounds alone on a repeated load, takes a new generation, refuses a second service, starts again after being destroyed and leaves no folder behind. It also starts the service on this machine's audio device without playing anything ("Audio device: started"); a machine without a device reports that and passes.
- Editor Play, D3D12, gate `cues-swing-d3d12` (extended): the Light press starts `Attack.Start` on the audio side at once; the hit applies Flinch and `Status.Flinch` starts the hit spark and the hit sound on the dummy; two sounds played, none missing, on the silent driver because the run is hidden. The [capture](../../evidence/cues-swing-d3d12.png) was looked at: sparks around the hit dummy and the Cues tab's audio and effect counts.
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 90 of 95 with the five known unrelated failures; all 52 verify gates pass.

Not verified: any sound through loudspeakers; stereo placement; a lasting sound fading by a cue's tail (no owned cue has a lasting sound); many sounds at once; what happens when the audio device disappears during Play.

Seen in passing, not part of M7: three of the five long-standing "known unrelated" CTest failures (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`) fail with "Asset staging write failed" on a file under `content/royal_district/RoyalCombat.dascene.records/`, which is the user's uncommitted scene. They copy the whole content folder; the long record file names under the deep test folder are the likely cause (path length), not checked.

## Step 4, first part: the VFX tab (12 October 2026)

The editor has a top-level **VFX** tab (`Workspace::Vfx`, between Animation and Game), built as the user decided: effects are made in Effekseer, the tab manages and previews them.

- **Effects**: every `.efkproj` and `.efkefc` of the project. Picking one cooks it and plays it in a loop in the preview.
- **New effect**: a name, and Create. The new effect is a copy of the placeholder spark under `content/vfx/`, adopted as an asset, so there is something to open and change. (Effekseer's "new project" cannot be started from the command line with a file name that does not exist yet.)
- **Edit in Effekseer**: opens the selected source in the Effekseer editor from `.tools`. The tab looks at the source file every half second; when it changes on disk (what saving in Effekseer does) the effect is cooked again and the preview restarts with the new version. **Cook again** and **Restart** do the same by hand.
- **Refusals**: when the cooker refuses the effect (a feature the renderer does not draw, a missing texture, a project Effekseer cannot compile) the tab shows the cooker's sentence in red and keeps previewing the last version that cooked.
- **Preview**: drawn by `VfxService` into the scene render target, at the origin of the open scene, with turn, distance and height sliders. Particle, quad and draw counts are shown, and skipped batches if there are any.

Limits: the preview stands in the open scene rather than in an empty stage, and has no attached-to-a-character mode or time scrubber yet; only the source file is watched, not its textures (use Cook again); Import of an existing effect from outside the project still goes through copying the files into `content/vfx` and Create or adoption by hand, the Import dialog does not offer effects; no Delete or Duplicate; the cue form is not here yet (next part).

Verified: editor-only change, so its own gate only. `vfx-tab-d3d12` (new, in `scripts/verify_effect_reaction.py`): the tab opens on the Burn aura, it is cooked, has its texture, plays and draws quads; the source file is then changed on disk and the tab cooks it a second time by itself. The [capture](../../evidence/vfx-tab-d3d12.png) was looked at. While building the gate a deliberately broken edit also showed the refusal path: the cooker's message appeared and nothing was previewed from the broken version. Not verified: the Edit in Effekseer button itself (it starts a GUI program; the gate does not click it), Create, and a real save from Effekseer rather than a file change.

## Step 4, second part: footsteps (12 October 2026)

An actor that walks is heard: a step for every stride of ground it covers.

- `FootstepTracker` (engine/runtime/cue_player.cpp) is shown each actor's position and whether it is grounded, once a tick, and reports a step every 0.9 m of ground covered while grounded. Time in the air covers no ground, and a jump of more than a metre in one tick (a teleport or a correction) is not walking. The session feeds it the player's predicted position, so the owner hears its own steps at once, and the other actors' public positions.
- `CuePlayer::moment` plays a one-shot that is neither an action block nor an effect edge, identified by actor and a serial, so the same step fed twice plays once.
- `CharacterSceneSession::footsteps()` gives the steps of the latest tick; a session with a cue player attached plays them under the cue key `Footstep`.
- Content: a placeholder step sound and `footstep.dacue`.

Limits: one stride length and one sound for every actor and every surface (no surface types, no walk and run difference, no left and right foot); steps follow distance, not the animation's foot contacts, so they are not in sync with the feet; a running player takes a step about every 0.15 s, which may be too quick to the ear.

Verified: `CuePlayerTests` (three steps for three metres, none in the air, none for a teleport, a new avatar starts again, a retired actor forgets; moments play once per actor and serial); `CharacterCombatSceneTests` (the player walking for two seconds takes the steps its covered ground gives, within one, all its own; the dummy that stands still takes none); editor gate `cues-footsteps-d3d12` (new: the player runs for two seconds and only footsteps are played, 18 of them, none missing). One full pass: the build shows only the known `RoyalCollisionTests` link failure; CTest 90 of 95 with the five known unrelated failures; all 54 verify gates pass. Not heard.

## Plan change: an Audio tab (12 October 2026, user request)

The user asked for a dedicated **Audio** tab for audio authoring, as an MVP now and grown later: edit audio clips (for example adding effects), edit the mixer (volumes for tracks such as background music, ambient sounds, NPC sounds and player sounds, and if possible bus effects such as a limiter, a compressor or a reverb to level the mix), audio profiles per scene, and different audio for encounters. The full workflow is open; the tab is not.

This replaces item (2) of what was left of M7 ("lasting sounds, ambient and music, bus volumes as project settings"): those become the content of the Audio tab instead of settings scattered elsewhere. Nothing below is built.

### What Amplitude offers, and what it does not (read from the pinned source, not tried)

- **Buses** with a gain each, parent and child buses, and ducking (one bus lowering another while it plays, with fade times). Our service creates only `master` and `effects` today.
- **Effects on a sound** (`.amfx`): bass boost, equaliser, compressor, delay, flanger, lo-fi, pitch shift, robotize, wave shaper, low and high pass, DC removal. An effect is chosen per sound, with parameters.
- **A mixing pipeline** for the whole output, built from nodes, among them a limiter, a hard clip, reverb and reflections.
- **Environments** (regions with an effect such as reverb that sounds inside them pick up) and **attenuation** shapes.
- **Collections** (random or sequenced variants of a sound), **switches** (a state that selects among sounds, for example a surface type) and **RTPCs** (a game value driving a gain or a parameter).
- **Not in its model: an effect chain on a bus.** A compressor or a reverb on the music bus alone is not something Amplitude's buses do. A limiter on the final mix and a reverb through the pipeline or an environment are; a per-bus compressor would have to be applied per sound on that bus, or built by us. This limits "bus effects" in the MVP to the master output.

### The Audio tab, MVP

A top-level **Audio** workspace (`Workspace::Audio`) with three sections.

1. **Sounds.** The project's sound assets with Import (a WAV file into `content/audio`), play and stop, length and format. A sound gets a small native definition beside its WAV (`.dasound`): the bus it plays on, gain, pitch, loop, whether it is positioned in the world or plays flat (music, UI), its distance range, and a list of Amplitude effects with their parameters. Editing is a native draft like every other asset (Undo, Redo, validation, Save). Audition plays the sound with its effects exactly as the game will. No waveform editing (trim, fade, normalise) in the MVP: that is destructive editing of the file and belongs to an audio editor; if wanted later, non-destructive trim and fades can be definition fields.
2. **Mixer.** One project asset (`.damix`): the bus list, fixed to begin with as Master, Music, Ambient, Player, NPC and UI, each with a volume and mute; ducking rules between buses (for example Music lowered while a boss encounter stinger plays); and the master chain: limiter on or off with its threshold. Live meters per bus are wanted and depend on whether Amplitude exposes bus levels (not checked). Changing a volume during Play is heard at once.
3. **Scene audio.** A profile (`.daaudioprofile`) that a scene names: its music track, its ambient loops with volumes, an optional reverb environment for the whole scene, and per-bus volume offsets over the project mixer. Starting Play applies the scene's profile.

Cues stay the link from gameplay to sound: a cue's audio names a sound asset, and the sound's definition decides bus, effects and looping.

### Encounters and other changes during play

An encounter (and later other world events) can name an audio profile to **layer** while it runs: the profile's music replaces the scene's with a crossfade, its ambient loops are added, and when the encounter completes the scene's profile returns. This reuses what encounters already publish (start and completion as world events), so the audio side listens to world events and needs no new gameplay state. Authored in the encounter's Inspector as one "Audio profile" field.

### Order of work

1. Lasting sounds in the service: loop, flat (not positioned) playback, stop with fade, a second handle type for music and ambience. Buses beyond `effects`. The service's project generation gains buses, effects and the pipeline's limiter.
2. `.dasound` definition, cooker, and its form in the Audio tab with audition. Cues play through it.
3. `.damix` and the Mixer section; volumes applied live.
4. `.daaudioprofile`, the Scene audio section, a scene naming its profile, applied at Play.
5. Encounter audio profile: layered on the encounter's world events.
6. Later, not MVP: collections for variation (several footstep samples), switches for surfaces, RTPCs, environments placed in a scene as volumes, meters, per-bus effect chains if we build them, non-destructive trim and fades.

### Questions for the user (none blocking)

1. Buses: is Master, Music, Ambient, Player, NPC, UI the right starting set, or should buses be free to add and rename from the start?
2. Is a limiter on the master output enough for "levelling" in the MVP, given that Amplitude has no per-bus effect chain?
3. Clip editing: are effects, gain, pitch and loop per sound enough for now, with trimming and fades left to an external audio editor?

## Step 4, third part: the cue form, and cues as dependencies (12 October 2026)

### Design (written before the code)

**What a reference is today.** An effect lists `cues: [{id, key}]`, but nothing checks that `id` is a cue asset, and a cue names its effect and its sound by source path without the cooker looking at either. Play therefore packages every cue of the project on its own and searches the catalog by path.

**Effect to cue.** An effect source may give a locator for a cue it binds, in the place every other locator already lives: `sources.cues` is an object from the cue's id to the path of its `.dacue`. For a binding with a locator the effect cooker cooks the cue, refuses the effect when the cue's identity or key differs from the binding, lists the cue in the effect product's `required` and carries the cue product in the effect's closure. So a kit, a player or an NPC that reaches the effect carries the cue, a changed cue re-cooks what names it, and coordinated Save finds the effect as a consumer of the cue (it already follows `sources`). A binding without a locator stays what it is today, a key that some cue of the project may answer; tests and old sources that bind a bare key keep working.

**Cue to effect and sound.** The cue cooker resolves `visual` and `audio` to asset identities: the named file must exist beside its import sidecar and be an Effekseer effect or a sound, and the cooked cue product gains `visual_asset` and `audio_asset`. A cue that names a missing file no longer cooks. The effect and the sound are *not* carried inside the cue's closure. Reason: every root in this asset service holds its whole closure, and nested cooks are not cached, so carrying the compiled effect would run the Effekseer tool inside every cook of every ability, kit, player and NPC that reaches a cue, on every machine and in every test. Instead the sidecars are inputs of the cue (a re-identified file re-cooks it), and whoever packages a cue adds the two identities it reads from the product as package roots. This is the one place the dependency is by identity rather than by closure, and it is deliberate.

**Play.** Cues come from the Play package first (those the kit closure reached through its effects); project cues not in it are added for the keys nothing references by asset (action cue blocks and footsteps name a key only). Effects and sounds are loaded by the identities in the cue products.

**Not in this part:** action cue blocks picking a cue by asset. An action block carries a key; changing that is a change to the action schema and its runtime, and the user's order asks for effects only.

**The cue form** sits in the VFX tab under the effect list: the project's cues, New cue, and for the selected cue its name, key, effect (a list of the project's effects, or none), sound (the project's sounds, or none), tail in ticks and "starts on prediction", with the cooker's refusal shown as text and Save through the ordinary draft path (Undo, Redo, Save All). Picking an effect in the cue form also selects it in the preview. The Ability tab's effect form gains a **Cues** list: add a cue by choosing a cue asset (which writes id, key and locator together), remove one.

### Built

- **Effect to cue.** `sources.cues` in an effect source (`{cue id: path}`); the effect cooker cooks a located cue, refuses a locator for a cue the effect does not bind and a binding whose identity or key differs from the cue's, lists the cue in the effect product's `required` and carries its product. The owned Burn and Flinch effects locate their cues. A binding without a locator is still a bare key.
- **Cue to effect and sound.** The cue cooker resolves `visual` and `audio` through the named file's import sidecar and writes `visual_asset` and `audio_asset` into the product (`CueDefinition` has both). A cue naming a file that is not in the project, or the wrong kind of file, no longer cooks; a source that already carries the two fields is refused.
- **Play** reads the cues of the Play package first and adds the project's other cues for keys nothing names by asset; effects and sounds are loaded by the identities in the cue products.
- **The cue form** in the VFX tab, below the effect controls: the project's cues (a star marks an unsaved one), New cue (under `vfx/cues/`), and for the selected cue name, key, effect, sound, tail and "starts on prediction", the decoder's refusal in red, and Save. Edits are ordinary drafts (Undo, Redo, Save All). Picking an effect also selects it in the preview.
- **The Ability tab's effect form** has a Cues list: Add cue (a list of the project's cue assets; writes identity, key and locator together) and Remove. A key-only binding is shown as such.
- Duplicating an effect keeps a located cue (the copy uses the same cue asset); a key-only binding still gets a fresh identity.

### Two fixes found by the first check of this session

- `M5.actor_assets`, `M5.attribute_authoring` and `M5.composer_structure` failed with "Asset staging write failed" on a record of the user's uncommitted scene. Cause, confirmed: the staged file's temporary suffix was 43 characters, which took a 227 character path over Windows' 260. The suffix is now 11 characters (`.s-` and eight hex digits).
- `M5.composer_preview` failed with "Tagged graph requires an explicit actor tag snapshot": the test evaluated the kit's stance graph directly without a tag snapshot, and that graph has become tagged. The test now passes an empty snapshot to its reference instance, as the preview does.

### Limits

- Action cue blocks and footsteps still name a key, so their cues come from the project's cue set, not from a closure.
- An effect's or a sound's product is not in the cue's closure (see the design); a packaged game has to add the two identities of each cue as package roots, as the editor does.
- Up to four located cues per effect (the effect's existing limit of four cues).
- The cue form is at the bottom of the VFX panel; in the 800 pixel high gate window its fields are below the fold, so the capture shows the cue list but not the form.

### Verified

- `CueAssetTests` (`M7.cue_assets`, extended): resolved effect and sound identities in the cooked cue; refusal of a missing sound, of an effect named as a sound and of a source carrying resolved identities; an effect's package carrying its cue; a changed cue cooking the effect again; refusal when binding and cue disagree on the key, and of a locator for an unbound cue.
- Editor gate `cue-form-d3d12` (new): a cue is staged through the form's creation path, edited as a draft (undoable), saved, and its cooked product names its effect and sound by identity. `cues-burn-d3d12` now also requires that at least two cues came through the Play package.
- One full pass on this build: the build shows only the known `RoyalCollisionTests` link failure; **CTest 94 of 95**, the one failure being `M4.royal_mesh_collision` (its executable does not link); all **55** verify gates pass. The first CTest run of the pass failed `M5.native_authoring` (a test that emptied an effect's cue list and left the new locator behind); the test was corrected and it and its seven dependants were run again, all passing. The [cue form capture](../../evidence/cue-form-d3d12.png) was looked at: the Cues section and the cue list are there; the form's fields are not in view.

Not verified: the cue form's widgets by hand (the gate drives the same draft calls, not the widgets); the effect form's Add cue and Remove; a project with more than 64 cues outside the Play package.

A build mistake of this session, for the record: one build was started without the pinned toolchain's environment, which made CMake reconfigure with Direct3D 12 detected as unsupported and marked the whole tree out of date. No dependency was installed, removed or rebuilt. A reconfigure in the correct environment restored the previous command lines and nothing third-party was recompiled.

## The Audio tab, MVP: design (12 October 2026, written before the code)

The user listened on 12 October 2026: swing, hit and steps are heard, and "Edit in Effekseer" works. The sounds are not good and footsteps come too quickly; both are polish the user put off ("we can make it pretty later"), so neither is tuned here. Whether a sound comes from the correct side was not reported.

This section fixes what the plan above left open, after reading the pinned Amplitude source for each point.

### What the pinned Amplitude does, as read

- **Buses** are a tree with a gain and a mute each, changeable while the engine runs (`Bus::SetGain`, `SetMute`, `FadeTo`), and ducking rules declared in the buses file. The tree and the ducking rules are read when the engine starts.
- **A sound has one effect, not a list** (`SoundDefinition.effect` is a single id). The plan above said "a list of effects"; the MVP has one effect per sound.
- **The limiter is a pipeline node, and the pipeline runs per sound.** So Amplitude's limiter holds each sound under a threshold; it is not a limiter on the summed output. There is no master effect chain in the pinned engine. The Mixer exposes it as what it is: "Limit each sound to". The pipeline is read when the engine starts.
- **No level meters**: a bus reports its gain, not its signal level. No meters in the MVP.
- **Looping** (`loop.enabled`), **unpositioned playback** (`spatialization: None`) and **distance range** (an attenuation asset) are fields of the sound definition, fixed when the sound is loaded.
- Sounds are still only added to a running engine, never unloaded (step 3), so a changed definition is a new entry; a service holds 512 of them.

What follows from that: bus volumes and mutes are live; the limiter and the ducking rules take effect when the audio service is next started (the editor restarts it when the mixer asset changed and nothing is playing: at the next Play or audition).

### Assets

- **Sound definition `.dasound`** (kind `sound`): `name`, `wave` (the WAV asset's id, with its path in `sources.wave`), `bus` (`music`, `ambient`, `player`, `npc`, `ui`), `gain` (0 to 4), `pitch` (0.25 to 4), `loop`, `positioned`, `range` (metres at which it is silent, 1 to 500), and `effect`: `{kind, parameters}` with kind one of `none`, `low_pass`, `high_pass`, `delay`, `lofi`, `compressor`. The cooked product carries the WAV in its closure (a real dependency; it is a byte copy, no tool runs). Because a definition is its own asset, one WAV can have several (the same swing on the Player bus and on the NPC bus).
- A cue's `audio` may name a `.wav` (played with default settings: positioned, Player bus, 40 m) or a `.dasound`.
- **Mixer `.damix`** (kind `mix`), one per project at `audio/project.damix`: for each of the five buses and Master a `volume` (0 to 2) and `mute`; `limit_each_sound` on or off with `threshold_db`; `ducking`: a list of `{when, lower, to, fade_in, fade_out}` (while a sound plays on bus `when`, bus `lower` goes to gain `to`). The bus set is fixed in the MVP (question 1 of the plan stays open; nothing here prevents free buses later, since buses are data in the generated project).
- **Audio profile `.daaudioprofile`** (kind `audio_profile`): `name`, `music` (a sound definition or none) with `music_volume`, up to eight `ambient` loops (sound definition and volume), `bus_offsets_db` per bus (-60 to +12), `crossfade_seconds`. Its sound definitions travel in its closure.

### Where a scene and an encounter name a profile

- **Scene**: a scene object component, like a spawn point or an encounter: `"audio": {"profile": id}`, at most one per scene. It is placed and changed from the Audio tab's Scene audio section (and is an ordinary object in the outliner). This follows how the scene already holds gameplay data and needs no change to the scene file format.
- **Encounter**: one optional field of the encounter component, `"audio_profile": id`, edited in the encounter's Inspector. The simulation ignores it. The audio side does not listen for world event names: it asks the session for each encounter's state every tick, so it works for an encounter that emits nothing.

### Runtime pieces

- `AudioService` gains: bus tree and mixer at start (`AudioOptions::mix`), `mix()` for live volumes and mutes, sounds loaded with their settings (`AudioSound`), `gain(handle)`, and the limiter and ducking written into the generated project.
- `AudioDirector` (engine/audio): holds the scene profile and a stack of layered profiles. The top profile's music plays; when the top changes the old music fades out over the crossfade time and the new one starts. Ambient loops of every profile on the stack play together. Bus offsets of the top profile are applied over the mixer. Nothing in it knows about encounters; the editor pushes and pops.

### The tab

`Workspace::Audio`, three sections in one panel: **Sounds** (WAV assets and sound definitions; Import copies a WAV into `content/audio` and adopts it; New definition for a WAV; the definition form; Play and Stop audition through the real service), **Mixer** (the project mixer's volumes, mutes, limiter and ducking; created on first use), **Scene audio** (profiles; New profile; the profile form; "Use for this scene"; audition of a whole profile).

### Limits known before building

One effect per sound; the limiter is per sound; no meters; music starts without a fade-in (the old track fades out under it); a profile's bus offsets are not themselves crossfaded; changing the limiter or ducking needs an audio restart, which the editor does only while nothing is playing.

### Order, one verification pass at the end

Service and director with their tests on the silent driver; the three cookers and their tests; cues through definitions; scene and encounter fields; the tab; gates for audition, a mixer volume changed during Play, a scene profile at Play and an encounter layering its profile.

## The Audio tab, MVP: built (12 October 2026)

### Built

- **Audio assets** (engine/assets/audio_cooker.cpp, `darkangel/audio_assets.hpp`): the sound definition `.dasound`, the mixer `.damix` and the audio profile `.daaudioprofile` as native sources with decoders, cookers and loaders, in the catalog, coordinated Save, native edit and create like every other native source. A definition's product carries its WAV; a profile's carries its definitions. A cue's `audio` may name a WAV or a sound definition.
- **Audio service** (engine/audio): the six buses with the mixer's volumes and mutes, live (`mix`); ducking rules and the per-sound limiter written into the generated Amplitude project at start; sounds loaded with settings (`AudioSound`: bus, gain, pitch, loop, positioned or flat, range through one attenuation per distinct range, one effect: low pass, high pass, delay, lo-fi, compressor); a gain per playing sound.
- **Fades are done by the service.** Amplitude's own timed stop crashed in the pinned build the first time it was used (every stop before this was immediate; step 3 listed a fading sound as not verified). The cause was not found: the crash is inside `Channel::Stop` with a duration, on the silent driver. `AudioService::stop` with a fade now lowers the channel's gain over `update()` time and then stops it at once.
- **Audio director** (engine/audio/audio_director.cpp): the scene's profile and a stack of layered ones; the top profile's music plays, the leaving track fades out over the crossfade time while the new one starts; the ambient loops of every held profile play together; the top profile's bus offsets apply over the mixer and are taken off again.
- **Scene and encounter**: a scene object with `"audio": {"profile": id}` (at most one; `SpawnPlan::audio_profiles`), and the optional `"audio_profile"` of an encounter component (`EncounterDefinition::audio_profile`, which the simulation never reads; the encounter's Inspector has the field). In Play the scene's profile starts with the session and an encounter's is layered while the session reports that encounter as running.
- **The Audio tab** (`Workspace::Audio`, between VFX and Game): **Sounds** (WAVs and definitions, Import of a WAV by path, New definition for a WAV, the definition form, Play and Stop; a definition is auditioned as its draft has it, saved or not), **Mixer** (volumes and mutes of the six buses, heard at once and also during Play; "Limit each sound to"; ducking rules; created on first use), **Scene audio** (profiles, New profile, the profile form, "Use for this scene", "Scene uses none", audition of a saved profile). Every form is an ordinary draft with Undo, Redo and Save All.
- **Content** (`scripts/generate_placeholder_audio.py`, which writes missing files only): two placeholder music loops and a wind loop, their definitions, the project mixer `content/audio/project.damix`, and the profiles `royal_calm` (calm music, wind) and `royal_battle` (battle music, ambient bus 6 dB down). No owned scene uses a profile yet: the user's scene is theirs, and the training scene is what the gates derive their scenes from. "Use for this scene" in the Audio tab sets it.
- The cue form of the VFX tab could not open a cue that already existed on disk (the draft opener did not know the cue's kind; the cue-form gate had only opened a cue it had just staged). Fixed here with the same table entry the audio sources needed.

### Differences from the design, and limits

- One effect per sound; the limiter holds each sound, not the sum; no meters; no master effect chain. All three are what the pinned Amplitude offers (see the design).
- The limiter and the ducking rules apply when the audio service is next started; the editor restarts it by itself only when nothing is playing and no Play session runs. The tab says so when they differ.
- New music starts at its full volume while the old track fades out under it; there is no fade-in.
- A sound plays on the one bus its definition names, whoever triggers it. The same WAV on the Player bus for the player and on the NPC bus for an NPC needs two definitions and two cues; nothing chooses between them by actor yet.
- A WAV named directly by a cue plays positioned, on the Player bus, silent at 40 m.
- Import takes a path typed into the tab; there is no file dialog. Imported names are limited to letters, digits, '_' and '-'.
- A profile is auditioned as saved (the profile is cooked from disk), unlike a single definition.
- Ducking was loaded and the engine accepted it; that the second bus is in fact lowered while the first plays was not measured (the engine reports a bus's own gain, not its ducked gain).
- Not built from the plan's later list: collections, switches, RTPCs, environments, meters, trim and fades.

### Verified

- `AudioAuthoringTests` (`M7.audio_authoring`, new): the three sources created, cooked, packaged and loaded with their values and their refusals; a definition locating another WAV than it names refused; a changed definition cooking its profile again; a cue resolving a sound definition; the service on the silent driver starting with the mixer (limiter, one ducking rule), a looped sound outlasting its length, a fade lasting its time and then ending, every effect kind loading and playing, live volumes and mutes, a structural mixer change told apart from a live one; the director starting a scene profile, layering a second, applying and removing its bus offset, counting a sound that is not loaded, and leaving the mixer as it found it.
- `AudioTests` and `CueAssetTests` pass unchanged in behaviour.
- Editor gates, D3D12, all new: `audio-tab-d3d12` (the tab lists six WAVs, three definitions, two profiles and the mixer; a definition is auditioned; the Music volume moved in the mixer's draft is applied; a profile is auditioned with its music and one ambient loop); `audio-play-d3d12` (Play starts the scene's profile: calm music, wind, no layer); `audio-encounter-d3d12` (the fight starts: battle music, the wind kept, the ambient bus at -6 dB, one layer, and an NPC bus volume changed in the mixer's draft during Play is applied). The [Audio tab capture](../../evidence/audio-tab-d3d12.png) and, now that its form is scrolled into view, the [cue form capture](../../evidence/cue-form-d3d12.png) were looked at.
- One full pass: the build shows only the known `RoyalCollisionTests` link failure; **CTest 95 of 96**, the one failure being `M4.royal_mesh_collision`; all **58** verify gates pass. CTest ran before a one-line editor fix (the draft opener's kind table); the gate script, which runs `NativeAuthoringTests` and nine other test programs itself, ran on the final build.

Not verified: anything by ear (every run used the silent driver): how the loops, the crossfade and the effects sound; the limiter's and the ducking's audible result; the tab's widgets by hand (the gates drive the same calls); Import; a profile on one of the user's scenes; the audio device disappearing during Play.

### Questions for the user (none blocking; the first three are the plan's, still open)

1. Buses: Master, Music, Ambient, Player, NPC, UI are fixed. Should buses be free to add and rename?
2. The engine has no limiter on the summed output, only on each sound. Is that enough for now, or should a master limiter of our own be planned (it means mixing through our own output stage)?
3. Trimming and fades inside a clip are left to an external audio editor. Acceptable?
4. Should a sound's bus follow who triggers it (player or NPC) automatically, instead of needing two definitions?

## Weapon trails and decals: design (12 October 2026, written before the code)

Decision 018 keeps weapon trails and decals native, beside Effekseer, and says what each must do. This is the first slice of both.

**Where they hang.** On a cue, like an effect and a sound: a cue gains two optional fields, `trail` (a `.datrail`) and `decal` (a `.dadecal`), resolved by the cue cooker to identities as its sound definition is. The visual side of the cue player presents a cue that has an effect, a trail or a decal.

**Weapon trail** (`.datrail`): a socket of the rig (or a joint), a base and a tip offset in that joint's space, a lifetime in ticks, a point cap and a colour. While the cue is live the host samples the two points once a presented tick from the pose the client already draws; the strip between consecutive pairs fades by age and from tip to base. A pair more than three metres from the one before breaks the strip (teleport, correction, new avatar), so a trail cannot span the map. The trail is visual only: hits stay with the combat queries.

- The decision opens and closes emission with an Action Composer interval. Action cue blocks are markers in this runtime (a limit recorded in step 1), so emission lasts as long as the cue lives, which for a marker is the cue's tail. The swing cue's tail is lengthened to cover the sweep.

**Decal** (`.dadecal`): a size, a lifetime, a fade at the end of it and a colour, drawn as a soft disc (opaque in the middle, clear at the rim) lifted two centimetres off its surface, in a pool of 64 that replaces its oldest.

- The decision spawns impact decals at the confirmed impact position and normal. The session publishes no hit position (step 1: there is no public hit event without an effect), so the first decal is laid at the feet of the actor whose cue starts, facing up. It is confirmed (the hit cue is not predictable), but it is not projected onto the collision: on a slope or a step it floats or sinks. A material (texture), receiver masks, attachment to moving surfaces are not in this slice.

**Drawing.** `VfxService` gains `quads()`: plain coloured quads drawn after the effects with the pipelines it already has, depth tested and not depth written; decals alpha blended, trails additive. The logic that builds them (`WeaponTrails`, `DecalPool` in engine/runtime) knows no renderer and is tested without one.

**Authoring.** Trails and decals are native drafts listed under the cue form in the VFX tab, with New trail, New decal and a small form each; the cue form picks them.

## Weapon trails and decals: built (12 October 2026)

### Built

- **Definitions and logic** (engine/runtime/presentation_primitives.cpp): `decode_trail`, `decode_decal`, `WeaponTrails` (begin, one sample a tick, break on a jump of more than three metres, point cap, fade by age, end, retire, a bound of 32 trails) and `DecalPool` (a soft disc of twelve slices in the plane of its surface, a fade at the end of its life, a bound of 64 that replaces the oldest). Both build plain coloured quads and know no renderer.
- **Assets**: `.datrail` and `.dadecal` are native leaf sources (engine/assets/primitive_cooker.cpp, `load_cooked_trails`, `load_cooked_decals`). A cue has two optional fields, `trail` and `decal`; the cue cooker resolves them to `trail_asset` and `decal_asset`. A cue with only a trail or a decal is valid and goes to the visual side.
- **Drawing**: `VfxService::quads` queues plain coloured quads for the next draw; they are drawn after the effects with the existing pipelines, depth tested and not depth written, decals alpha blended and trails additive.
- **In Play** (apps/editor/primitive_cue_sink.hpp): a cue's trail emits while the cue is live and is sampled once a presented tick from the socket's joint in the pose the client draws; a cue's decal is laid at the feet of the cue's actor when the cue starts. The Cues tab shows live trails, points, decals and quads drawn.
- **Authoring**: the cue form has Weapon trail and Decal pickers; below it the VFX tab lists trails and decals with New trail, New decal and a form each (socket, base and tip offsets, lifetime, points, colour; size, lifetime, fade, colour). The cue form's Sound picker now also lists sound definitions.
- **Content**: a placeholder swing trail on the right weapon socket and a placeholder scuff decal; the swing cue names the trail and its tail is 24 ticks so emission covers the sweep; the hit cue names the decal.

### Limits

- Emission follows the cue's life, not an interval of the action (cue blocks are markers). The swing trail therefore also covers the wind-up.
- One sample per presented frame: a frame that runs several ticks samples once.
- The decal is at the actor's feet facing up, not projected onto the collision; no material, no receiver mask, no attachment to moving surfaces.
- The trail's offsets were chosen by looking at one capture; the character holds no visible weapon, so the strip stands for a blade from the hand.
- No width or colour curves, no subdivision between samples.
- Late join does not reconstruct a trail in progress, as the decision says it need not.

### Verified

- `PresentationPrimitiveTests` (`M7.presentation_primitives`, new): sources and refusals; a trail holding one pair a tick, fading from tip to base and from new to old, breaking at a teleport with no quad across the gap, keeping to its point cap, dropping aged pairs, ending and being forgotten once faded, the set's bound, an actor leaving; a decal's disc (opaque centre, clear rim at half its size, flat on a floor, in the plane of a wall), its fade and expiry, the pool's bound; a cue naming a trail and a decal cooking with their identities, a missing trail and a decal named as a trail refused; the cue player sending a trail-only cue to the visual side alone and releasing it after its tail.
- `CueAssetTests` updated for the swing cue's trail.
- Editor gate `cues-swing-d3d12` (extended): after the Light press and its hit there is a live trail with at least two pairs, a decal, quads of both drawn, and nothing missing. The capture was looked at: a pale ribbon swept from the player's weapon hand, and a dark soft disc under the dummy that was hit.
- One full pass: the build shows only the known `RoyalCollisionTests` link failure; **CTest 96 of 97** (only `M4.royal_mesh_collision`); all **58** verify gates pass.

Not verified: the trail and decal forms' widgets by hand; a trail on an NPC's swing by eye; a decal on anything but flat ground; many trails at once; Vulkan.

## The M7 gates: design (12 October 2026, written before the code)

The milestone row asks for four things. Two are met and stay as they are: bounded tails and resource retirement (the cue player's bounds and its tests; trails, decals and sounds are bounded too), and no-device and headless operation (the silent audio driver, null sinks, every scene test without a device). Two are open.

**A rejected activation cutting a real effect and sound, in a live session.** A loopback session never refuses a prediction by itself, which is why step 1 could only test the cut below the session. Authority already has the path a refusal takes: `WorldSession::advance_abilities` passes an optional rejection into `request_wire` (it uses it for an avatar mismatch and for an expired input), and the receipt that comes back corrects the owner's prediction. The gate uses that path through a fault injection, named as one: `WorldSession::refuse_next_ability(network, reason)` makes authority refuse that owner's next pressed intent, and `CharacterSceneSession::refuse_next_activation()` is the same for the player. Nothing else is special: the client predicts the press, starts its predictable cue, receives the refusal, drops the activation, and the cue tracker reports a cut.

- A scene test (`CharacterCombatSceneTests`): the predicted cue starts on the press, is released within the rejection tail instead of its own, is never confirmed, and the next press is accepted.
- An editor gate (`cues-rejected-d3d12`): in Play the swing's real sound and real trail start on the press and are cut; no hit follows, so no hit cue, spark or decal.

What this does not show is a refusal caused by real latency: that needs two processes or a delayed transport, which is the host and client qualification postponed on 11 October.

**An agent changing actual effect and sound content, not only wrapper parameters.** The gate script plays the agent: it runs Play, then edits the hit spark's own node in its Effekseer project (how many sparks it emits) and replaces the hit sound's samples in its WAV, runs Play again and restores the files. Play reports the product generation of each effect and sound it loaded, so the gate can require that both changed, that an untouched effect did not, and that more particles are alive than before.

## The M7 gates: built (12 October 2026)

### Built

- **Fault injection for one refusal.** `WorldSession::refuse_next_ability(network, reason)` (authority only) and `CharacterSceneSession::refuse_next_activation()`: authority refuses that owner's next pressed intent through the `rejection` argument `request_wire` already had, with an ordinary receipt.
- **The owner's prediction is observed before the tick's answer.** The first run of the new scene test showed nothing at all for a refused press: on loopback the refusal arrives inside the same tick, and the session looked at the owner's predicted action only after applying it. A client on a real connection presents its prediction first. `CharacterSceneSession::step` now remembers the predicted action as it stood right after the prediction advanced and gives that to the cue tracker as the owner's predicted stream. An accepted press is seen exactly as before; a refused one is seen for the tick it was predicted and reported as a cut on the next.
- **Editor**: `--exercise-reject` arms one refusal at the start of Play. With it the combat exercise requires the target unharmed and the press's cost not spent.
- **The capture writer retries.** During this increment's gate runs the editor twice failed to write a screenshot into the evidence folder ("PNG capture failed"), on two unrelated gates. Reproduced: replacing an existing PNG in that folder failed about one time in three, while a scratch folder never failed, so another program on the machine briefly holds a capture that was just replaced (which program was not found). The editor now encodes the PNG in memory and tries the file write for up to two seconds. Six of six replacements then succeeded and the full run passed.
- **Play reports what it loaded**: the cue readout line lists the product generation of each effect and sound of the session.

### Verified

- `CharacterCombatSceneTests` (extended): with a refusal armed, the press starts the predicted swing cue on its tick; the cue is released by tick 11 (cut with the six tick rejection tail; its own tail of twelve would have kept it) with exactly one cut reported; it is never confirmed; and the next press is predicted and confirmed as usual. Every earlier check of that test passes unchanged, which covers the changed observation point for accepted presses.
- Editor gate `cues-rejected-d3d12` (new): in a live Play session the refused Light starts the swing sound on the audio service and the weapon trail, both are stopped (two stops on the audio side, no trail left, no live cue at tick 30), there is no hit cue, spark or decal, the dummy's health is 100 and the player's stamina is unspent.
- Editor gates `content-before-d3d12` and `content-after-d3d12` (new): between the two runs the gate script changes the hit spark's emitter in its `.efkproj` from 14 to 40 sparks and replaces the hit WAV's samples. Play then loaded a new product for the spark (`2dfc0fdec910` to `cc1cb0fd30c2`) and for the hit sound (`834794ce560a` to `890f683cc3c1`), the untouched Burn aura kept its product, and 34 particles were alive at tick 30 against 14 before. The script restores both files afterwards.
- One full pass: the build shows only the known `RoyalCollisionTests` link failure; **CTest 96 of 97** (only `M4.royal_mesh_collision`); all **61** verify gates pass. CTest ran before two editor-only changes (what the combat exercise requires when a refusal is armed, and the capture writer below); the gate script ran on the final build. Two earlier runs of the gate script on this increment stopped at a capture that could not be written, on two different gates; see the capture writer.

### Where the milestone row stands

| Gate of the row | State |
|---|---|
| Real effect and sound through an accepted activation | Shown in Editor Play (`cues-swing-d3d12`, `cues-burn-d3d12`): Effekseer effects, a native trail and decal, sounds through the Amplitude engine. |
| The same through a rejected activation | Shown in a live session with an injected refusal (`cues-rejected-d3d12`, the scene test). Not shown with a refusal caused by real latency. |
| Bounded tails and resource retirement | Cue player bounds and tails, trail and decal bounds, the service's own fades: tests listed in the sections above. |
| No-device and headless operation | The silent audio driver in every hidden run, null sinks, all scene tests without a device. |
| An agent changes actual effect and sound content | Shown (`content-before-d3d12`, `content-after-d3d12`). |

What keeps M7 from being called Verified without qualification:

- **Heard by a person: swing, hit and steps only** (the user, 12 October). The Audio tab's music, ambience, crossfade, effects, limiter and ducking were never heard; every automated run uses the silent driver.
- **D3D12 only.** The effect service and the primitives were not run on Vulkan.
- **Editor Play only.** No game executable constructs the two services yet (M9 packaging).
- A refusal from real latency, and whether stereo placement is the right way round, are open.

