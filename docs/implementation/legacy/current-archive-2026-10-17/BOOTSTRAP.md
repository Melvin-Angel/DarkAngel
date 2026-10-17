# Reproducing M0

Supported baseline: Windows x64, Visual Studio 2026 18.10.3/MSVC 14.51.36231, Windows SDK 10.0.26100.0, CMake 4.4.4, Ninja 1.13.2, Python 3.13.5 and PowerShell 7.6.5. Git 2.49.0.windows.1 and Git LFS 3.6.1 are reused. Tool/source versions, official URLs and verified checksums are in `cmake/*.lock.json`. Portable tools and caches remain ignored. Paths are process-scoped; nothing changes global PATH.

From the project root:

```powershell
pwsh -NoProfile -File scripts/bootstrap.ps1 -Mode Detect
pwsh -NoProfile -File scripts/bootstrap.ps1 -Mode Acquire
pwsh -NoProfile -File scripts/bootstrap.ps1 -Mode Verify -Profile m0-relwithdebinfo
pwsh -NoProfile -File scripts/bootstrap.ps1 -Mode Verify -Profile m0-debug
pwsh -NoProfile -File scripts/bootstrap.ps1 -Mode Verify -Profile m0-release
```

`All` combines acquisition, detection and verification for one profile. `Acquire` continues independent steps after failures and returns a failure code if any step failed. Each step has a bounded subprocess log and exit code in `.cache/evidence/`; `.cache/bootstrap-<Mode>.json` records resumable results. A second run checks all checksums, source revisions and extracted files before reusing them. It does not reset dirty checkouts, pull a newer branch, change pins, wipe a mismatch or erase caches. Repair requires a deliberate new destination/pin or restoring the recorded artifact; bootstrap reports what differs.

The native helper activates the exact detected MSVC installation through `vswhere` and Microsoft's `vcvars64.bat`, using a child environment restricted to compiler variables. It validates the selected toolset and SDK. Native concurrency is two jobs, downloads three; these conservative limits fit the detected machine. Native builds use separate Debug, RelWithDebInfo and Release directories and /MDd versus /MD consistently. Profiles use the same frozen manifest graph, with only the `m0` feature selected for engine configuration.

The initial machine lacked C++ tools. The user installed them during M0. An official signed Visual Studio 2026 installer (18.10.12224.181) was downloaded but never launched unattended. Future missing compiler/SDK detection leaves an explicit blocker. Install the matching supported toolset/SDK interactively through Visual Studio Installer; `cmake/windows-buildtools.vsconfig` is a component reference, not automatic acceptance of license terms. Actual selected versions must match `cmake/native-toolchain.lock.json`.

For later EOS integration, copy `local.config.example.json` to ignored `local.config.json` and point `eos_sdk_root` to the authorized SDK's `SDK` directory. This machine reuses the user's EOS 1.19.2.1 download; the version/header/import-library/runtime inventory is pinned. On another machine, obtain that Windows C SDK through Epic's official Developer Portal if it is not already authorized and installed. No login, credential, SDK archive or overlay is copied into the engine repository. M0's headless build does not require EOS linking.

The committed `port-assets.lock.json` supports prefetching actual source/cache assets without redundant clones. Bootstrap copies checked archives into the expected vcpkg cache names. Full manifest resolution/prefetch uses:

```powershell
# After scoped portable CMake/Ninja and compiler detection:
.tools/vcpkg/vcpkg.exe install --triplet x64-windows-darkangel --host-triplet x64-windows-darkangel --x-feature=m0 --x-feature=m1-tools --x-feature=later --x-feature=amplitude-deps --x-feature=evaluation-flac --only-downloads
```

This does not compile the entire graph. `cmake/port-recipes.lock.json`, the acquisition report and actual vcpkg plan preserve resolved recipes, versions, features and patches. Compiler-dependent resolution and later additional downloads are distinct from archive prefetch. The upstream `HEAD_REF` fields in pinned port recipes are dormant; none of these commands passes `--head`.

MCP and glTF tooling use exact npm versions and checked-in package-lock files, installed with `npm ci --ignore-scripts`. No endpoint runs in M0. Inactive effekseer-ai wheels are isolated downloaded files, never installed globally. `scripts/prepare_amplitude.py` prepares its static source-build/import exception, described in [later recipes](LATER_DEPENDENCY_RECIPES.md).

Native verification configures/builds/tests, performs a no-change build, appends/restores a controlled comment in `engine/runtime/bootstrap.cpp` and records what rebuilt. Optimized profiles install only the HeadlessShell component, create a CPack ZIP, compare its extracted files to the explicit runtime inventory and launch from a separate directory with development PATH removed. This is a relocatable M0 smoke, not clean-machine/device/online qualification or a Shipping game package.

Git tracks sources, supplied architecture inputs, locks, generated notices and reports. SDKs, archives, node_modules, tools, credentials, machine-specific SDK configuration, build outputs and stage directories are excluded. Re-extract the complete decision log with `scripts/extract_architecture.py` when its supplied DOCX deliberately changes.
