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

- A **VFX tab** in the editor for Effekseer effects. Later the same day the user chose how: Effekseer's own editor as a companion tool opened from the tab, no particle form of our own, no live link and no window embedding; see "VFX tab" below.
- **Runtime UI as authored content**: a UI tab for editing UI documents, UI documents attached to NPCs (world space) and to the Player (screen-space HUD, world space later), and a place for main menus in the same system. That is its own plan, [UI_PLAN.md](UI_PLAN.md), which the user deferred the same day.

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
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 88 of 93, the five others being the known unrelated ones (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`); `python scripts/verify_effect_reaction.py` ran 52 gates (the earlier 50 and the two cue gates) and all pass. Both cue captures were looked at ([swing](evidence/cues-swing-d3d12.png), [Burn](evidence/cues-burn-d3d12.png)).

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
- Editor Play, D3D12, gate `cues-burn-d3d12` (extended): after the Heavy hit the Burn aura is loaded, playing, has particles and is drawn in at least one draw call with nothing skipped and no cue left without its effect. The [capture](evidence/cues-burn-d3d12.png) was looked at: an orange glow and embers around the burning dummy, hidden where the player stands in front of it, so the scene depth is respected.
- One full pass on the final build: the build shows only the known `RoyalCollisionTests` link failure; CTest 89 of 94 with the five known unrelated failures; all 52 verify gates pass.

Not verified: ribbons, rings and tracks on screen (the shared code is built and reached the same way as sprites, but no owned effect uses them yet); blend modes other than additive; Vulkan; an effect stopping and fading at the end of Burn seen by eye; many effects at once.
