# Codex prompt: DarkAngel audit-driven refactor (written 14 October 2026)

Give the block below to Codex as its task. It is self-contained.

---

You are refactoring the DarkAngel engine and editor (C++20, Windows, CMake/Ninja, vcpkg, Diligent, RmlUi, Dear ImGui) in `C:\DarkAngel`. The goal is a **clean, modular, scalable codebase with no change in behaviour**. This is not feature work: do not implement new features (prefabs, Add component and the Scene camera modes are documented TODOs; leave them alone).

## Read first

1. `docs/implementation/current/CODEBASE_AUDIT.md` (measurements, findings, proposals). Treat it as input, not gospel.
2. `docs/implementation/current/BUILD_PERFORMANCE_REPORT.md` (compile and link costs, hub-header fan-out).
3. The last two sections of `docs/implementation/current/M8_REPORT.md` (current state; what is verified and how).
4. `docs/implementation/future/EDITOR_UX_BACKLOG.md` and `PREFABS_AND_COMPONENTS_TODO.md`. Do not implement them, but keep the editor structure you create friendly to them: Inspector component sections, a future Prefabs tab, editor camera modes.

## Architectural direction

- **`EditorApp` coordinates; it does not implement.** Introduce `EditorApp` to own lifecycle and orchestration of independently owned services, for example `RenderSystem`, `PlayController`, `EditorUI` (workspaces and inspectors), `ExerciseRunner`, `AssetWorkspace` and `PartyNetworking`, plus the existing `RuntimeInventory`, `RuntimeAltar` and `PlaySaves`. Do not move the 300 KB of `main()` into another god object. The point is explicit ownership and dependency boundaries, not just new files.
- **Narrow contexts instead of the whole app.** Each subsystem receives a small context struct or interface exposing only the operations it needs. For example, the Gameplay Inspector gets the session and authoring commands, not the renderer, networking internals or save database. Replace the 69 capture-everything lambdas in `main()` with these explicit dependencies.
- **Exercises drive the product's public commands.** The exercise or script runner must act through the same commands a designer invokes in the editor (Place, Inspector edits, Save, Play, menu actions), not through private shortcuts, so gates test the intended functionality.
- **Layering to move towards** (do not build all of it now, but do not add dependencies that cross it):
  - `DarkAngelRuntime`: platform-independent gameplay, simulation, assets, networking.
  - `DarkAngelPlatform`: OS integration, windows, input, filesystem, graphics-device setup.
  - `DarkAngelEditor`: authoring, debugging, inspectors, tooling.
  - `DarkAngelGame` (future): the standalone app on runtime and platform, with no editor dependency.

  Runtime code must not gain editor or ImGui dependencies. Where code you touch already violates this, note it in the report rather than fixing everything at once.

## Order of work (one step, then verify, then commit)

0. **Baseline.** Build, then run a fresh `python scripts/verify_m8_slice.py` (about 25 minutes, nothing else running). It includes `m8-slice-party-travel`, which has not yet passed in full: the fast-travel checkpoint and other-scene respawn were only verified by hand. Fix only genuine regressions found there, then commit. Record baseline metrics (below).
1. **Extract the exercises.**
   - Move the 73 `--exercise-*` flags, their assertions and the script verbs (walk-to, use, attack, altar-*, inventory-*, hold-interact, down, approach, log-party, await-*, capture, quit) out of `main()` into `apps/editor/exercises/`. Give them an `Exercise` interface and a registry; the scripts get a `ScriptRunner`.
   - Keep every existing command-line spelling working (aliases are fine), because the gate scripts under `scripts/` depend on them.
2. **Introduce `EditorApp`** with the service split above and explicit ownership: who creates and destroys the device, Play session, party and saves.
3. **Extract `PlayController` and the Gameplay Inspector**, separating gameplay orchestration (Play start/stop, travel hooks, party host/guest, receipts) from authoring UI.
4. **Narrow the hub headers** (`world_session.hpp`, `scene_definitions.hpp`, `character_scene.hpp`, `scene_client.hpp`, `scene_authority.hpp`) using forward declarations and smaller headers. Measure with `ninja -t deps` before and after.
5. **Split `WorldSession` and `SceneAuthority`** by responsibility (follow the existing pattern of `scene_world_objects.cpp`, `party_lifecycle.cpp` and `inventory_session.cpp`: narrow interfaces, separate translation units).
6. **Shared utilities and formatting.**
   - Add `.clang-format` at the start, but **do not reformat the repository in one go**. Format only files you extract or rewrite, and keep formatting-only changes in their own commits, separate from semantic changes.
   - Add a shared `require`/check header and JSON field helpers, replacing the 54 local copies as you touch files.
7. **Split CMake and the tests.**
   - Move to `cmake/*.cmake` with a `dae_test(...)` helper.
   - Split `tests/native_authoring_tests.cpp` (about 24 minutes, 117 KB) into parallelisable executables.
   - Consider a first-party precompiled header.

If a step turns out bigger than expected, split it into smaller commits. If a step needs a product or design decision, stop and ask the user.

## Measure; don't trust estimates

- Verify the numbers before setting targets. `main.cpp` is about 357 KB but only about 1,550 lines, because statements are packed onto very long lines, so **line counts are a poor metric**. Use file size, compile time from `.ninja_log`, object counts per hub header, and the time of a few representative incremental rebuilds (touch `main.cpp`; touch `world_session.hpp`; touch one inspector file).
- Indicative targets, not acceptance criteria: `main.cpp` 357 KB → about 15 KB; its compile time 130–200 s → about 15 s; exercise flags in the application entry 73 → 0. Record before and after for every step in a new `docs/implementation/current/REFACTOR_REPORT.md`, with honest notes where a target was not met.

## Build and test rules on this machine (important)

- **Building:** always build through the pinned MSVC environment. `scripts/msvc_environment.py` `activate()` **returns** an environment dict, which must be passed to every cmake call (`subprocess.run(..., env=activate())`). Use `.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe --build build/m5-editor-relwithdebinfo [--target X]`. Without that environment, CMake reconfigures Diligent without D3D12 and recompiles everything.
- **Dry-run first:** run `-- -n` before a build. If third-party objects (Diligent, Effekseer, glslang) show up in the plan, stop and check `D3D12_SUPPORTED` and `WINDOWS_SDK_VERSION` in `CMakeCache.txt`.
- **Never** delete `build/`, `vcpkg_installed/` or the vcpkg buildtrees, and do not change `VCPKG_MANIFEST_INSTALL=OFF`. No new dependencies.
- **One heavy job at a time.** Never run two builds or two editor sessions at once, and do not run editor gates while CTest runs: contention made `NativeAuthoringTests` time out after 3 hours, whereas alone it takes about 18 minutes. `M5.attribute_authoring` needs about 154 s, which is over its 90 s CTest limit; run it directly as well.
- **Do not edit run-time files while tests or gates run:** `games/AshenRoots/ui/*.rml`, `content/`, `scripts/`.
- `near` and `far` are Windows macros. Do not use them as identifiers.
- **Verification per step:** build; the M8, M6 and M5.hud CTest subset (`ctest -R "M8\.|M6\.|M5\.hud"`); and the editor gates affected by the step. Before the final commit of steps 2, 3 and 4, also run full CTest (with NativeAuthoringTests alone), `scripts/verify_m8_slice.py`, `scripts/verify_network_party.py` and `scripts/verify_items_world_ui.py`.
- **Leave the user's uncommitted hand-test edits alone:** `content/royal_district/RoyalCombat.dascene`, `RoyalTraining.dascene` and their `.records/` folders. Do not stage, revert or overwrite them.

## Commits and reporting

- One commit per verified step, with conventional messages (`refactor(editor): ...`), plus separate formatting-only commits.
- Do not push unless the user asks.
- Keep `docs/implementation/current/REFACTOR_REPORT.md` current: what moved where, the new ownership and dependency diagram, metrics before and after, what was verified, and anything deferred.
- At the end, update `CODEBASE_AUDIT.md` with which proposals are done.
