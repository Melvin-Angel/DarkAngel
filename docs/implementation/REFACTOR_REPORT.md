# Audit-driven refactor

Requested through `CODEX_REFACTOR_PROMPT.md` on 14 October 2026. Starting commit: `65db8aa`.

## Scope and sequence

Preserve existing behavior, CLI spellings, authoring commands, runtime ownership and authority. Prefabs, Add component and Scene camera modes remain separate TODOs. Follow the prompt's order: baseline; exercises and ScriptRunner; EditorApp services; PlayController and Gameplay Inspector; header narrowing; runtime responsibility splits; shared utilities; CMake and authoring test split. Each increment must pass its required verification before committing or starting the next step. Formatting changes have separate commits.

## Step 0: baseline, verified

- No compiler, build, CTest or editor process was running at orientation.
- Git started with only the user's untracked RoyalCombat and RoyalTraining `.records/` folders. These are protected, together with the scene sources.
- Pinned MSVC activation returns the environment passed to CMake. Cache: `D3D12_SUPPORTED=TRUE`, `WINDOWS_SDK_VERSION=10.0.26100.0`, `VCPKG_MANIFEST_INSTALL=OFF`.
- Guarded dry-run initially listed 296 potential first-party compile/dyndep/link operations, without Diligent, Effekseer or glslang object rebuilds. Actual build passed in 13.890 s; after dependency scanning, no compilation or linking was needed.
- `.clang-format` establishes LLVM-based formatting with four-space indentation and a 140-column limit. No existing source is reformatted at this point.
- Full guarded build and fresh M8 slice run serially. Logs: `evidence/refactor-baseline-build.log`, `evidence/refactor-baseline-slice.log`. Measurements and completion receipt will be recorded in `evidence/refactor-baseline-metrics.json` and `evidence/refactor-baseline.json`.

Measured starting source: 360,484 bytes, 1,613 lines, 77 distinct exercise option spellings (including `--exercise` and the value-bearing import option). Recorded Ninja object dependencies: world_session 65 objects / 45 targets; scene_definitions 32 / 16; character_scene 28 / 16; scene_client 30 / 16; scene_authority 29 / 16. These counts use object outputs only, excluding dependency-scan outputs; they are not directly comparable to older counts using a different output selection. Most recent recorded main compilation: 177.083 s. Historical executable edges include link and DLL deployment; they are not controlled benchmarks.

### Regression exposed by the first fresh slice

The initial slice failed in 92.718 s after placement, solo and save/load passed. The party log showed a downed guest still at zero Health at tick 132, then both members alive after wipe/respawn. `hold-interact:420` consumed 420 rendered frames, which can represent fewer than the 180 simulation ticks required for a three-second revive. Gate success depended on rendering speed.

The bounded baseline repair keeps requesting the same authenticated Interact command and measures the hold budget in simulation ticks. It leaves keyboard input, party rules, request cadence and authority unchanged. Original failed logs/receipts/captures are retained in `evidence/refactor-baseline-failure/`.

Confirmation guarded build passed in 64.968 s. M8/M6/HUD CTest passed 14/14 in 23.594 s. The fresh slice passed all six gates in 201.860 s, including party revive to 50 Health, wipe/respawn, party travel, fast travel preserving the checkpoint, and respawn in the other scene. Host and guest receipts both show five arrivals at epoch 5 with no travel error. Both scenes generated their own `.dacollision` files in the disposable fixture copy. Network evidence remains same-machine UDP/D3D12 only. Full CTest is not rerun for this bounded baseline repair.

### Controlled incremental measurements

Command for each measurement: pinned `cmake --build build/m5-editor-relwithdebinfo --target DarkAngelEditor --parallel 4 -- -k 0`, with the returned MSVC environment and a guarded dry-run first. One source was touched at a time; its original timestamps were restored afterwards to avoid invalidating unrelated test binaries. Source bytes and compiler/linker flags were unchanged.

| Touched source | Editor target rebuild | Relevant source compile, Ninja log |
| --- | ---: | ---: |
| `apps/editor/main.cpp` | 70.890 s | 39.562 s |
| `world_session.hpp` | 121.218 s | main.cpp 51.621 s, with other compilations sharing four jobs |
| `apps/editor/editor_world_objects.cpp` | 45.188 s | 9.031 s |

Logs and dry-runs are `evidence/refactor-baseline-touch-*.log`; totals are in `refactor-baseline-incremental.json`. Warm controlled times differ considerably from the audit's older 130–200 s compilation observations. Do not attribute that difference to this refactor: no extraction or compiler optimization has occurred yet.

## Ownership and dependencies before extraction

`main()` owns the platform window/device, renderer resources, editor Controller/Shell, Play session, party adapters, script state, runtime inventory/altar UI and saves. Its closures share these locals. Runtime libraries retain their existing public boundaries. The refactor will replace this implicit sharing with service ownership and narrow contexts, without relocating the entire function into EditorApp.

## Verification boundary

The baseline and timer repair are freshly verified as described above. No source extraction or refactor improvement is claimed yet. The broader 107-test checkpoint remains historical; the current subset is 14 tests. Network gates establish same-machine UDP/D3D12 behavior only.
