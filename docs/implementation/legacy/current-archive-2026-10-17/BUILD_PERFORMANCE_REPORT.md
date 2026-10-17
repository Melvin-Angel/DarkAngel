# Build performance investigation — 13 October 2026

The useful work before M8 stage 2 is to shorten editor compilation, reduce shared-header rebuilds, and benchmark a cheaper development link. The build already uses Ninja incrementally; the problem is the cost and breadth of the work that an edit invalidates.

This investigation used the existing `build/m5-editor-relwithdebinfo/.ninja_log`, Ninja's recorded include dependencies, generated compile/link commands, Git snapshots, and verification logs. Raw measurements are in [build-performance-2026-10-13.json](../evidence/build-performance-2026-10-13.json). No build, editor, compiler benchmark, dependency installation, cache change, or source change was started. Claude's existing final verification was running CTest during the investigation. Its stage 1b result remains that runner's responsibility.

## Measured costs

The two most recent recorded editor rebuilds took 256.8 and 260.5 seconds. In the latter, compiling `main.cpp` took 134.8 seconds and the executable link/deployment edge took 101.7 seconds: together about 91% of elapsed build time. The accompanying `scene_authority.cpp` compilation took 19.8 seconds.

| Recorded operation | Recent durations |
| --- | --- |
| Editor `main.cpp` compilation | 133.4–196.0 s across the last four recorded compilations |
| Editor executable link/deployment | 94.0–174.2 s across the corresponding four links |
| `native_authoring_tests.cpp` compilation | 167.8–198.5 s |
| `world_session.cpp` compilation | 55.3–62.9 s |

Executable-edge timings include vcpkg's post-build DLL deployment. They establish a slow link/deployment step, but a future `/TIME` linker receipt is needed to separate code generation, PDB writing, and deployment precisely. These are historical command timings, not controlled before/after benchmarks. The retained Ninja log is not a complete time series.

## Why compilation has become slower

### The editor entry point has grown substantially

`apps/editor/main.cpp` is currently 341,440 bytes, with about 303,238 bytes from `main()` to the end of the file. It combines window/device setup, rendering, Play and travel integration, scripted exercises, captures, and diagnostics inside a very large function with many lambdas. It compiles with `/O2 /Ob1` and debug information.

| Snapshot | Whole source size | Size from `main()` to EOF |
| --- | --- | --- |
| Royal combat checkpoint `ab55e4d` | 49,644 bytes | 27,684 bytes |
| Camera checkpoint `93c816d` | 147,318 bytes | 116,011 bytes |
| Stage 1b checkpoint `732533a` | 329,332 bytes | 292,101 bytes |
| Current working source | 341,440 bytes | 303,238 bytes |

The whole source is about 2.3 times the camera checkpoint's size and 6.9 times the Royal combat checkpoint's size. The latest networking changes account for only about 12 KB of growth after `732533a`; this is accumulated editor growth, not solely the latest networking increment. Optimizing one large function is a likely contributor to its serial compile cost, alongside template instantiation and header parsing. The exact distribution requires compiler profiling.

`tests/native_authoring_tests.cpp` is another large optimized source, at 116,699 bytes, and takes roughly three minutes to compile when shared headers invalidate it.

### Frequently changed headers reach many objects

Ninja's existing dependency records show:

| Header | First-party object files depending on it | Targets |
| --- | --- | --- |
| `world_session.hpp` | 106 | 42 |
| `assets.hpp` | 160 | 45 |
| `character_scene.hpp` | 44 | 16 |
| `scene_client.hpp` / `scene_authority.hpp` | 46 each | 16 each |
| `party_control.hpp` | 8 | 3 |
| `editor_ui.hpp` | 12 | 1 |
| `nlohmann/json.hpp` | 192 | 44 |

Counts refer to this configuration's recorded object dependencies. A shared session-header change can therefore recompile much more than the runtime implementation being edited. The complete build also relinks its test executables after the affected static library changes. `character_preview.cpp` is separately compiled into four targets with distinct commands/definitions.

There is no first-party precompiled header in the generated build; the existing PCH belongs to glslang. Many sources repeatedly parse the same stable standard-library and JSON headers. C++20 module scanning is also enabled on first-party targets, producing `.ddi`, `.modmap`, and dyndep work, although a search found no module declarations or imports in the owned sources. Module scanning is a smaller opportunity than the large compilation/link costs.

### The editor's link performs whole-program optimization

Diligent enables `/GL` for release configurations, including this RelWithDebInfo build. The generated graph contains 556 `/GL` object rules across Diligent and its bundled tools/libraries. This does not mean those sources are rebuilt on every edit; their compiled libraries still participate in the editor's link.

The current editor link options already include `/LTCG:INCREMENTAL /INCREMENTAL:NO`, added in `CMakeLists.txt`. Incremental LTCG is therefore already in use; recommending that switch again would not fix the current cost. Microsoft distinguishes incremental LTCG from ordinary incremental linking and documents that `/GL` inputs implicitly trigger LTCG. Simply removing the explicit LTCG switch can cause an automatic linker restart. [Microsoft LTCG documentation](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170).

The current editor has an approximately 543 MiB PDB, 232 MiB incremental code-generation object, and 62 MiB incremental code-generation PDB. Their size makes debug-information/code-generation work plausible contributors, but size alone does not measure their share of the delay.

### Available parallelism is limited

The machine is an i7-1185G7 with four physical cores, eight logical processors, and about 32 GiB RAM. The current runner uses four build jobs. At the snapshot, about 10.6 GiB RAM and 51.1 GiB disk space were available. More Ninja jobs cannot parallelize one `main.cpp` compilation. Thermal throttling, paging during compilation, antivirus scanning, and competing workloads were not measured and should not be presented as established causes.

The older build presets cap jobs at two, but the current M8 runner explicitly overrides that with four. `VCPKG_MAX_CONCURRENCY=2` concerns package builds and does not cap the current project build.

## Recommended work before stage 2

1. **Split the editor entry point into focused compilation units.** Move rendering/upload/capture helpers, Play/session integration, and verification exercises out of `main.cpp`; retain the existing lifecycle, deterministic scripts, diagnostics, and captures. Give these units narrow interfaces rather than one header containing every editor dependency. Keep the actual entry point as orchestration. This addresses both the long individual compilation and the fact that unrelated editor changes repeatedly invalidate it. Split the largest native-authoring test source by coherent test groups as a separate, measured change.

2. **Add a small, opt-in first-party build-performance configuration.** Disable module scanning on owned targets that use no modules. Add private, target-specific PCHs containing stable standard-library and JSON headers. Start with the editor, runtime, and asset library; exclude frequently changing engine headers from PCHs. Do not force a shared PCH across targets whose definitions/options differ. These changes entail a one-time rebuild of affected owned targets, but do not require rebuilding installed packages. [CMake module scanning](https://cmake.org/cmake/help/latest/prop_tgt/CXX_SCAN_FOR_MODULES.html), [CMake precompiled headers](https://cmake.org/cmake/help/latest/command/target_precompile_headers.html).

3. **Benchmark a development-only link mode using the existing libraries.** After the current verifier finishes, compare the current editor link against an isolated `/LTCG:OFF` link, with separate executable, PDB, import-library and cache outputs. Microsoft documents this option for treating `/GL` modules without whole-program optimization. Keep the current optimized verification configuration intact until behavior and performance are compared. Do not rebuild Diligent or any installed dependency just to investigate this option. Add `/TIME` to the profiling link to determine what actually consumes the time. The speedup and runtime impact remain unmeasured. [Microsoft LTCG options](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170), [Microsoft linker timing](https://learn.microsoft.com/en-us/cpp/build/reference/time-linker-time-information?view=msvc-170).

4. **Reduce header coupling as stage 2 types are introduced.** Keep item/inventory contracts in focused headers. Avoid adding all inventory and persistence implementation details to `world_session.hpp`, `assets.hpp`, or `scene_definitions.hpp`. Use forward declarations and move implementation-only includes into `.cpp` files where contracts allow. Preserve existing authority, serialization, and source-of-truth boundaries; this is not a reason for a broad rewrite.

5. **Use focused build targets during iteration, followed by the required stage verification.** Build the editor and the tests relevant to the coherent increment while developing it. The existing workflow already sometimes does this. Retain the full build/CTest/editor/party pass at the integration checkpoint. Changes to runtime state or replication still require the appropriate regression coverage. A build flag change must not silently substitute new binaries into a verification already in progress.

A temporary editor-only `/Od /Ob0` development experiment could also quantify the optimizer's contribution while leaving the engine optimized. It should remain opt-in and measured; it is not a substitute for splitting the source.

Avoid treating more jobs, a global unity build, or a clean build as the first remedy. The observed bottleneck includes one serial source file and a large link. A global unity build can make incremental invalidation larger. There is no evidence here that deleting build state would help.

## Compilation versus the 75-minute verification estimate

The current final runner's summary recorded its build command at **57.8 seconds**, or **70.2 seconds** including wrapper/toolchain setup. That phase only linked remaining tests and reported the existing RoyalCollisionTests unresolved symbol. It did not compile the editor again.

The previous complete stage 1b verification recorded:

| Verification phase | Time |
| --- | --- |
| Full CTest | 1,446.78 s, about 24 minutes |
| NativeAuthoringTests within CTest | 1,117.51 s, about 19 minutes |
| Full 67 editor gates | 1,627.0 s, about 27 minutes |
| Direct actor/attribute reruns after their CTest timeouts | 133.7 / 157.5 s |

The total verification estimate is therefore dominated by test execution and fixture/cook work as well as compilation. Build improvements will shorten the edit/rebuild loop substantially if confirmed, but cannot by themselves turn that full verification into a few minutes. The long authoring fixture test deserves a separate runtime investigation; changing its scheduling or fixtures was outside this build investigation.

## Measurement and acceptance for the follow-up

Once Claude's final verification has ended, establish a small baseline on the pinned MSVC/CMake/Ninja environment, preserving `VCPKG_MANIFEST_INSTALL=OFF` and the installed dependency trees. Measure a no-change build, an editor-only edit, a scene implementation edit, and a representative shared-header edit. Compare the same scenarios after each optimization, recording compile count, elapsed time, individual compile timings, link `/TIME` output, and whether vendor objects were scheduled.

Use fresh private profiling outputs when comparing link modes; do not overwrite the binaries or LTCG/PDB outputs used by the current verifier. Confirm both development iteration and the existing optimized build still work. Focus behavior checks on the extracted editor paths and retain the normal stage integration gates. Report measured improvements rather than assuming a speedup from a flag name.

This report proposes the follow-up; no performance optimization has been applied or benchmarked yet. Stage 2 remains the items/inventory/masks/pickups work described in M8_REPORT.md, after stage 1b qualification.
