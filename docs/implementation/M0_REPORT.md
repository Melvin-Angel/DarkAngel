# M0 project bootstrap report

M0 is verified on Windows x64. Project root: `C:\DarkAngel`. Prepared and resolved 7 October 2026 (Europe/Copenhagen). M1 has not started.

The small C++20 shell and real Flecs/Luau/SQLite probes pass in Debug, RelWithDebInfo and Release. A separate clean Git checkout of revision `768806bd57f59522e054215100660b1451756258` also configured, built, passed CTest, rebuilt incrementally and produced a relocatable ZIP. Existing verified tools/source archives were reused through junctions; dependency libraries rebuilt where the ABI/cache key differed. This is clean source-checkout verification, not a clean-machine/device qualification or a bit-for-bit native reproducibility claim.

## Project and targets

The repository contains `cmake/`, bounded bootstrap/verification scripts, static Foundation and bootstrap Runtime libraries, `apps/headless/`, real tests, reserved gameplay/editor/cooker/agent folders, Ashen Roots README, pinned source exceptions, complete architecture inputs, notices and evidence. Local tools, caches, SDKs, build/stage output, npm installs and machine SDK configuration are ignored. The original user files and EOS SDK remain intact. No external repository, push, publication or deployment was created.

Targets: `DarkAngelFoundation`, `DarkAngelRuntime`, `DarkAngelHeadless`, `FlecsProbe`, `LuauProbe`, `SQLiteProbe`. The Amplitude import proof is an optional standalone support check, with no SDK/audio linkage added to Headless.

The complete supplied decision log is retained as DOCX and a faithful ordered searchable extraction (1,308 paragraph/table-row records, 1,388 source text nodes). Tables, statuses, complete decision records and caveats are retained; the extraction is not a summary.

## Frozen toolchain

| Tool | Exact selected version | Verification |
|---|---|---|
| Visual Studio | 2026 Community 18.10.3 / 18.10.12224.181 | User-installed; supported vswhere detection |
| MSVC | toolset directory 14.51.36231; serviced compiler 19.51.36260.0/linker 14.51.36260.0 | Executable hashes and /Bv compile proof |
| Windows SDK | 10.0.26100.0 | Scoped compiler environment and actual native builds |
| CMake / CTest / CPack | 4.4.4 | Official Windows ZIP digest, configure/build/test/package |
| Ninja | 1.13.2 | Official release digest, actual build |
| Git / Git LFS | 2.49.0.windows.1 / 3.6.1 | Reused; exact version checks |
| Python / PowerShell | 3.13.5 / 7.6.5 | Reused; exact version checks |
| vcpkg | source/baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`; tool 2026-07-27-98d7cb0cf1f4686a3e43aa5672b6230c1d56bce8 | Exact revisions, local tool hash, valid Microsoft signature |
| DXC | v1.9.2609; dxcompiler.dll/dxil.dll 1.9.2609.5 | Same official ZIP; DXIL and SPIR-V compiler smokes |
| FlatBuffers / flatc | 25.12.19 / 25.12.19 | Matching library pin and official compiler; 13 SDK schemas |
| Node / npm | 24.21.0 / 11.19.0 | Official Node artifact checksum and npm lockfiles |
| XMake | 3.1.1 (binary reports +HEAD.3ba37a0d4) | Official release artifact digest; static SDK build |
| Effekseer authoring/runtime | 1.80.7 / 1.80.7 | Matching official release ZIP digests; UI/device not tested |
| glTF Validator | 2.0.0-dev.3.10 | Official Khronos npm package, lockfile and fixture |
| OpenSSL build tools | Strawberry Perl 5.42.2.1; NASM 3.01 | Actual pinned vcpkg recipe hashes |

vcpkg also uses its pinned CMake 4.4.0/7-Zip 26.02 host-tool recipes; exact used metadata and local hashes are retained in `cmake/vcpkg-host-tools.lock.json`. No global PATH change, Rust, additional engine/IDE installation, or mandatory Vulkan SDK was introduced. A signed Build Tools installer was acquired but not run unattended; the user supplied the installation.

## Acquired packages and exact pins

Every named selected public package and all three inactive candidates are acquired. The full actual Windows vcpkg plan and best-effort pre-download completed successfully. All pinned source/cache archives are hash checked; source exceptions include required recursive submodules and LFS checks where declared. Exact source/tool/archive hashes, versioned port trees, transitive features and patch hashes are in `cmake/*.lock.json`, `vcpkg.json`, `dependencies.json` and the evidence logs.

| Package | Version/revision | M0 result |
|---|---|---|
| abseil | 20260107.1#3 | acquired; not integrated |
| behaviortree-cpp | 4.9.1 | acquired; not integrated |
| cgltf | 1.15 | acquired; not integrated |
| directxmath | 2026-06-12 | acquired; not integrated |
| directxtex | 2026-05-07 | acquired; not integrated |
| dylib | 3.0.1 | build-verified; Amplitude private build/import dependency |
| eigen3 | 5.0.1 | build-verified; Amplitude private build/import dependency |
| flatbuffers | 25.12.19 | build-verified; Amplitude private build/import dependency |
| flecs | 4.1.6 | integrated; M0 isolated probe |
| freetype | 2.14.3 | acquired; not integrated |
| gamenetworkingsockets | 1.6.0 | acquired; not integrated |
| itlib | 1.12.2 | acquired; not integrated |
| joltphysics | 5.6.0 | acquired; not integrated |
| luau | 0.729 | integrated; M0 isolated probe |
| lz4 | 1.10.0 | build-verified; Amplitude private build/import dependency |
| meshoptimizer | 1.2 | acquired; not integrated |
| miniaudio | 0.11.25 | build-verified; Amplitude private build/import dependency |
| minitrace | 2023-04-23 | acquired; not integrated |
| openssl | 3.6.3 | acquired; not integrated |
| protobuf | 6.33.4#2 | acquired; not integrated |
| recastnavigation | 1.6.0#1 | acquired; not integrated |
| rmlui | 6.2 | acquired; not integrated |
| robin-hood-hashing | 3.11.5#2 | acquired; not integrated |
| sqlite3 | 3.53.4 | integrated; M0 isolated probe |
| tinyxml2 | 11.0.0 | acquired; not integrated |
| utf8-range | 6.33.4 | acquired; not integrated |
| vcpkg-cmake | 2024-04-23 | acquired; not integrated |
| vcpkg-cmake-config | 2026-07-21 | acquired; not integrated |
| vcpkg-cmake-get-vars | 2025-05-29 | acquired; not integrated |
| xsimd | 13.2.0 | build-verified; Amplitude private build/import dependency |
| libflac | 1.5.0 | acquired; not integrated |
| libogg | 1.3.6#1 | acquired; not integrated |
| brotli | 1.2.0 | acquired; not integrated |
| bzip2 | 1.0.8#6 | acquired; not integrated |
| libpng | 1.6.58 | acquired; not integrated |
| zlib | 1.3.2#1 | acquired; not integrated |
| diligent-core | `074060cfd1db73064d7a18cf40df86af6531520c` | acquired |
| diligent-tools | `4508c9ea7457209e204dc84b18ef67272fb415d0` | acquired |
| amplitude | `ff5f2f8be59e0b74445c10f73eb11a7134eb6fd5` | build-verified |
| ozz | `744eb9d99f606eda849acb0b1204f7a3dc20bca1` | acquired |
| imguizmo | `b796ac3b861afc6e91ca74e4611effd9c9527367` | acquired |
| manifold | `b92f06be117765cc16b47451fcfa3e8145cea984` | acquired; inactive |
| effekseer-ai | `208922ef192220322c2a79e1243ed51ff7d2b7af` | acquired; inactive |
| amplitude-flac | `7bf4533dc077dc35b8285e187cd05ed372d700b0` | acquired; inactive |
| imgui-docking | `44aa9a4b3a6f27d09a4eb5770d095cbd376dfc4b` | acquired |
| EOS Windows C SDK | 1.19.2.1 | Acquired, local hashes and valid Epic signature; integration pending |
| Effekseer runtime/editor | 1.80.7 | Acquired matching release artifacts; adapter pending |
| Official TypeScript MCP SDK | 1.32.1 | npm ci + import passed; no bridge endpoint |
| Khronos glTF Validator | 2.0.0-dev.3.10 | npm ci + real fixture passed |

Diligent compatible Core/Tools revisions come from umbrella commit `efe5da2270c6edfc8b0d2e39e946b35acbeaa874`. The bundled non-docking ImGui is inactive; official matching 1.92.1 docking is the sole planned compiled provider. ImGuizmo uses an approved source route to avoid a second vcpkg ImGui. DiligentFX/samples are excluded. GNS/OpenSSL/protobuf, RmlUi/FreeType, SDK codecs and other private transitive code are covered by actual recipes and notices, without selecting an engine JSON parser.

Amplitude static Release /MD build/install and CMake import/link/run passed. Its build uses the selected vcpkg providers, matching flatc, and a recorded patch instead of XMake moving package ranges. The CMake wrapper carries required platform/SIMD definitions and LZ4/FlatBuffers/dylib static dependencies. This recipe currently builds AVX2 code; later CPU/device and AudioService qualification is unverified. Amplitude remains the only audio engine, with its internal miniaudio backend. Candidate FLAC remains inactive: this source pin defines a shared plugin, and static registration/seek/loop gates are still pending.

## Measured native checks

Commands, exit codes, elapsed seconds and complete relevant logs are retained under `docs/implementation/evidence/`. Times describe this machine/run and are not promised rebuild budgets.

| Configuration | Configure s | Build s | CTest s | No-change s | Incremental s | Result |
|---|---:|---:|---:|---:|---:|---|
| m0-debug | 16.905 | 11.723 | 0.833 | 0.098 | 1.818 | Exit 0, 4/4 tests |
| m0-relwithdebinfo | 5.854 | 6.931 | 0.622 | 0.069 | 1.244 | Exit 0, 4/4 tests |
| m0-release | 12.314 | 7.485 | 0.941 | 0.065 | 1.239 | Exit 0, 4/4 tests |
| clean-release | 366.829 | 6.288 | 0.712 | 0.081 | 1.238 | Exit 0, 4/4 tests |

All four no-change builds reported `ninja: no work to do`. The controlled change compiled only `engine/runtime/bootstrap.cpp` (dependency scanning and compilation), rebuilt `DarkAngelRuntime.lib` and relinked Headless. Foundation and the three probe sources did not recompile. The original source was restored and rebuilt; the clean checkout remained clean.

Flecs creates/uses/destroys an isolated world/component. Luau compiles and executes `return 6 * 7` with an interrupt budget and closes the isolated VM. SQLite creates an exclusive temporary fixture, commits a transaction, reads 42 back, closes and deletes it. Tests have explicit diagnostics and timeouts, and retain checks in optimized builds.

Both final bootstrap acquisition runs returned exit 0 for every step. Each validates existing archive/extracted-file hashes and source pins, installs exact npm lockfiles and performs audited full-graph prefetch without revision updates or cache deletion. The corrected prefetch command waits for a competing vcpkg operation instead of failing immediately. The initial missing compiler and failed compiler activation/cache setup were corrected before these final checks; the known failed CMake cache was preserved before a targeted `--fresh` reset.

| Bootstrap acquisition | Elapsed s | Steps | Result |
|---|---:|---:|---|
| Final first | 82.485 | 10 | Every exit code 0 |
| Final second / idempotence | 139.998 | 10 | Every exit code 0 |

Offline checks also passed: tool/source hashes, exact reused/native toolchain versions, EOS input inventory, preset schema, official MCP import, glTF validation fixture, DXIL/SPIR-V compilation, matching schema compilation and docking presence. These are tool/input checks, not later engine adapter acceptance.

## Stage and package

The HeadlessShell install component stages exactly three files. Release/RelWithDebInfo package output directories are separate; Debug packaging is excluded from the supported verification profile. Actual Release PE imports were inspected: only Windows API sets, KERNEL32 and MSVC/UCRT runtime imports occur. No renderer/audio/editor/EOS DLL is required by the shell. The observed x64 runtime DLL version is 14.51.36247.0; a compatible serviced Visual C++ Redistributable is an external prerequisite.

The CPack ZIP was extracted into a separate directory, audited against `cmake/runtime-inventory.json`, and launched from the TEMP directory with development PATH removed. The clean package was copied byte-for-byte to `stage/packages/Release/DarkAngel-M0-0.0.1-win64-HeadlessShell.zip` for delivery. SHA256: `7488b9d7ab9320620089d4403225fc136a58656e1e2fe9d21b9464f52e3d337d`. Its console identifies clean source revision `768806bd57f5`.

## Blockers and remaining gates

No M0 blocker remains. Acquired later libraries are not described as integrated merely because probes compile. Renderer/D3D12/Vulkan device behavior, EOS/GNS sessions, Jolt/Ozz runtime, RmlUi surfaces, BT.CPP execution, Effekseer adapter/authoring round trips, audio devices/banks/codecs, candidate acceptance, Shipping endpoints/content closure and clean-machine qualification remain their later architecture gates. Debug Amplitude /MDd is prepared but not built/validated in this pass.

508 acquired-source/tool/candidate notice records were collected from actual inputs; EOS notice files are inventoried without redistributing the entire SDK. The inventory is not a blanket redistribution/license certification, and engine source licensing remains a product decision.

## Next M1 task

Implement stable IDs and owned reflection/versioned serialization metadata backed by Flecs Meta, WorldDomain/epoch/generation handles, explicit single-threaded phases and staged writes, bounded work/authority roles, and one declared typed Luau object behavior. Select a native JSON parser deliberately through a recorded M1 choice. Prove stale/wrong-domain rejection, independent world/script state and failed reload preserving the old generation. Do not begin gameplay or renderer implementation as part of M0.
