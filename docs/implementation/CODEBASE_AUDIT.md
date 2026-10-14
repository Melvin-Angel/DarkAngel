# Codebase audit and refactor proposals (14 October 2026)

Requested by the user: "begin an audit of the project and suggest refactors to keep the code base clean, modular and scalable, e.g. reducing the size of main.cpp". This is a read-only audit with proposals in priority order. Nothing here has been refactored yet. Every proposal keeps behaviour unchanged and can be landed alone, with the existing gates as the safety net.

## Measurements

| Area | Size | Notes |
|---|---|---|
| `apps/editor/main.cpp` | 357 KB, ~1,550 lines | `main()` alone is about 317 KB. It holds 73 `--exercise-*` flags, 147 option fields, 193 preprocessor lines, 69 local lambdas, 19 `shell.*` and 8 `editor.*` callback assignments. |
| `engine/runtime` | 1.12 MB | The largest files are `world_session.cpp` (112 KB), `scene_authority.cpp` (64 KB), `scene_client.cpp` (62 KB), `character_motor.cpp` (60 KB) and `ability.cpp` (45 KB). |
| `apps/editor` (all) | 823 KB | Apart from main.cpp: `editor_authoring.cpp` (105 KB), `editor_ui.cpp` (63 KB), `native_authoring.cpp` (55 KB). |
| `tests` | 1.09 MB | `native_authoring_tests.cpp` (117 KB) takes about 3 minutes to compile and about 24 minutes to run; `character_combat_scene_tests.cpp` is 113 KB. |
| Line length | 1,513 lines over 300 characters in engine and apps | Many statements are packed onto one line, and there is no `.clang-format`. |
| Local `require` helpers | 54 files | Each file defines its own `void require(bool,const char*)`. |
| Build | See BUILD_PERFORMANCE_REPORT.md | main.cpp compiles in 130–200 s and the editor links in 95–175 s. `world_session.hpp` reaches 106 object files. |

## Findings

1. **main.cpp is the application.** Window and device setup, the renderer, resource uploads, Play and travel, the party host and guest, HUD and menus, cue, VFX and audio binding, navigation baking, gameplay debug drawing, picking, every Inspector callback, saves, receipts, captures, and about 70 scripted exercises all live inside one function. Its state is a pile of locals captured by reference into lambdas, so any change recompiles 317 KB at `/O2`, and the coupling between features is invisible.
2. **Gate exercises are mixed into the product.** 73 `exercise_*` flags and their assertions run inside the frame loop. They are valuable but belong to a test harness, not the editor's main loop.
3. **Hub headers.** `world_session.hpp`, `scene_definitions.hpp` and `character_scene.hpp` pull in most of the runtime, so a field added to `CharacterSceneCombat` recompiles roughly 45 objects.
4. **Two very large runtime implementation files.** `WorldSession::Impl` mixes transport, replication, the ability host, effects, projectiles, the collision stream and owner channels. `SceneAuthority::Impl` mixes the party, actors, controllers, encounters, interactables, portals and now world objects and lifecycle. The M8 work started the right pattern (`scene_world_objects.cpp`, `party_lifecycle.cpp`, `inventory_session.cpp` as separate units with narrow interfaces).
5. **Dense formatting.** Very long lines make review, diffs and merge conflicts harder. Codex and Claude sessions repeatedly struggled to match edits exactly.
6. **Duplicated small utilities.** `require`, JSON field helpers and byte/string conversions are re-declared per file.
7. **Editor UI helpers in anonymous namespaces.** Helpers in `editor_ui.cpp` (add_object, set_component, committed_text, list_travel_scenes) cannot be reused, so new inspectors either grow that file or need exported wrappers (added this session as `scene_add_object` and related functions).
8. **CMakeLists.txt is 754 lines** in one file, with per-test boilerplate repeated.

## Proposals, in priority order

### 1. Split main.cpp into an `EditorApp` with feature modules (highest value)

Target: main.cpp under about 15 KB (argument parsing and `return EditorApp(args).run();`).

- Introduce `apps/editor/editor_app.hpp/.cpp` holding today's locals as members, grouped into structs: `RenderContext` (device, swap chain, pipelines, uploads), `PlayState` (character session, travel coordinator, party host and guest, ledger, saves, HUD), `PresentationBindings` (cues, VFX, audio), and `DebugState` (senses, navigation, world debug).
- Move each feature into a module with an explicit interface that the app calls each frame, for example `PlayController` (start, stop, travel, the receipt), `GameplayInspector` (today's `shell.draw_gameplay_inspector` body), `WorldDebugDraw` (volumes, entries, movers and similar), `ScenePicking`, `RenderScene` (the drawn-object loop), `PartyNetworking` (wrapping network_play), and the existing `RuntimeInventory`, `RuntimeAltar` and `PlaySaves`.
- Order of extraction, smallest risk first: the receipt writer, world debug draw, Gameplay Inspector, picking, the HUD/menu frame, the Play start/stop/travel hooks, the render loop, and device setup last.
- Each step moves code without editing it, builds, and runs the existing editor gates. Expected effect: an edit in one feature recompiles a 20–40 KB file instead of 357 KB, and features stop sharing hidden locals.

### 2. Move gate exercises into a test harness

- Create `apps/editor/exercises/` with an `Exercise` interface (`frame(EditorApp&, unsigned frame)`, `done()`, `result()`) and a registry keyed by the flag name. The 73 flags become one `--exercise <name>` plus parameters.
- Script verbs (walk-to, use, attack, altar-*, inventory-*, down, approach and so on) move into `ScriptRunner` in its own file, used by gates and by future in-editor test playback.
- Effect: the frame loop loses roughly 100 KB of assertions, and new gates stop touching main.cpp.

### 3. Narrow the hub headers

- Split `scene_definitions.hpp` into `scene_content.hpp` (prepared content structs) and small headers per content kind; use forward declarations and `std::shared_ptr<const T>` where only pointers cross.
- Give `world_session.hpp` a slimmer public face: move ability, effect and projectile host calls behind a `WorldSession::Abilities` sub-interface declared in its own header, so scene code that does not touch abilities stops including them.
- Measure with `ninja -t deps`: the target is under 30 objects per hub header.

### 4. Split WorldSession and SceneAuthority by responsibility

- `WorldSession`: replication and transport core; ability host; collision stream; owner channels (already partly separate as `owner_mailbox.cpp`). Keep `Impl`, but move each group's methods into its own .cpp file that includes a private `world_session_impl.hpp`.
- `SceneAuthority`: follow the world-objects pattern. Extract `ActorDirector` (actors, controllers, sensing and decisions), `EncounterHost` (directors, spawn queue, encounter records) and `PartyMembers` (seats, admission, departure). Each gets the authority's narrow context, as `SceneWorldObjects::Who` and `Context` do.

### 5. Formatting and shared utilities

- Add a `.clang-format` (for example LLVM style with a 140-column limit) and apply it **one directory per commit**, at a time when no other work is in flight, to keep merges sane. This is the change that most improves reviewability, and it is purely mechanical.
- Add `engine/foundation/include/darkangel/check.hpp` with `require(bool, std::string_view)` and a typed `json_field` helper set, and replace the 54 local copies over time.

### 6. Editor UI building blocks

- Move the reusable Inspector helpers (`add_object`, `set_component`, `entity_source`, `committed_text`, `half_fields`, travel scene lists, asset combos) out of `editor_ui.cpp`'s anonymous namespace into `editor_scene_edit.hpp/.cpp`. Give each component Inspector its own file, as `editor_world_objects.cpp` does now.
- This also prepares for the planned editor-wide UX/UI overhaul: one place for widgets and edit primitives.

### 7. Build hygiene

- Split CMakeLists.txt into `cmake/engine.cmake`, `cmake/editor.cmake` and `cmake/tests.cmake`, with a `dae_test(name SOURCES ... LIBS ... LABELS ...)` function to remove the per-test boilerplate.
- Split `native_authoring_tests.cpp` into several executables by area, so CTest can run them in parallel and a failure narrows quickly. That brings the 24-minute run down to the slowest part.
- Add a first-party precompiled header (standard library and nlohmann/json) for the engine and editor targets. BUILD_PERFORMANCE_REPORT.md measured repeated parsing as a cost.

## Suggested sequence

1. Formatting pass (5) and the shared `require` header (mechanical, one session).
2. Extract the exercises and script runner (2): main.cpp loses about 30%.
3. EditorApp split (1), module by module, with gates after each.
4. Header narrowing (3) and runtime splits (4), driven by measured rebuild counts.
5. Editor UI building blocks (6) at the start of the UX/UI overhaul.
6. CMake and test splits (7) alongside.

None of these needs a new dependency.
