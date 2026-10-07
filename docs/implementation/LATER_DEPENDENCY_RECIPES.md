# Acquired dependency recipes

This is an acquisition/build recipe inventory, not subsystem acceptance. The immutable records in `cmake/` and actual upstream sources govern flags and target names. Later integrations must re-run their architecture gates.

## Providers

The vcpkg manifest is the sole provider of its selected ports and their transitive libraries. `m0` selects Flecs/Luau/SQLite; `m1-tools` adds the matching Luau CLI; `later` selects renderer-independent later runtime/cooker libraries; `amplitude-deps` selects dependencies of the audio source exception. `evaluation-flac` is inactive and only acquires the FLAC/Ogg candidate dependency. Graph-wide overrides freeze versions and port revisions, including transitive packages.

Flecs 4.1.6 uses `flecs::flecs_static` and nullable `try_get<T>()`. Luau 0.729's patched export uses `unofficial::luau::Luau.Compiler`, `Luau.VM` and `Luau.Analysis` in that namespace. SQLite 3.53.4 uses `unofficial::sqlite3::sqlite3`; external extension loading is disabled. These APIs/targets are checked against actual pinned source/port recipes, then exercised by M0 probes.

GNS uses the actual vcpkg OpenSSL/protobuf recipe with ICE off for LAN; protobuf's Abseil/utf8-range and build tools are recorded. RmlUi enables FreeType, with its resolved font dependencies recorded in the plan. No Lua binding, SVG or Lottie feature is selected. BT.CPP is 4.x, static, without Groot, SQLite logging, samples or tests. cgltf/meshoptimizer and Recast/Detour use their actual ports. DirectXTex `tools` includes official offline texconv/texassemble/texdiag binaries; their recipe hashes are pinned. Jolt uses the triplet CRT policy with samples/viewers/tests disabled by its port.

## Diligent and developer UI

DiligentEngine umbrella commit `efe5da2270c6edfc8b0d2e39e946b35acbeaa874` records compatible Core `074060cfd1db73064d7a18cf40df86af6531520c` and Tools `4508c9ea7457209e204dc84b18ef67272fb415d0`. Only those two modules are acquired with required submodules. DiligentFX and Samples are excluded.

Tools' bundled ImGui 1.92.1 has no docking. Official `ocornut/imgui` v1.92.1-docking commit `44aa9a4b3a6f27d09a4eb5770d095cbd376dfc4b` is the only active compiled ImGui provider, supplied using the actual `DILIGENT_DEAR_IMGUI_PATH` override. The inactive upstream bundled copy remains as part of the unmodified submodule graph. No separate vcpkg ImGui target is selected. ImGuizmo is a source exception because its port would select a second ImGui; compile its source against Diligent's chosen provider in M2.

`cmake/later-recipes.cmake` prepares Core/Tools with unrelated backends, tests, archiver, super resolution and RenderStatePackager disabled. D3D12 and early Windows Vulkan remain selected; no Vulkan SDK is required for M0. Existing ZLIB::ZLIB and PNG::PNG targets from vcpkg prevent compiling the bundled zlib/libpng duplicates. Bundled JSON parsers are private upstream implementation dependencies and do not select the engine's M1 native serialization parser.

## Ozz and Effekseer

Ozz is an approved source exception because this baseline has no Ozz port. Configure its pinned source with Ninja, `BUILD_SHARED_LIBS=OFF`, `ozz_build_msvc_rt_dll=ON`, `ozz_build_tools=ON`, `ozz_build_fbx=OFF`, `ozz_build_gltf=ON`, `ozz_build_samples=OFF`, `ozz_build_howtos=OFF`, `ozz_build_tests=OFF`, `ozz_build_data=OFF`. Its vendored jsoncpp and cgltf importer code are inventoried private offline-tool dependencies; the runtime cgltf provider remains the selected vcpkg port. No Autodesk SDK acquisition is required.

Effekseer uses official matching 1.80.7 C++ runtime and Windows authoring release ZIPs, each checked against its official release asset digest. Its runtime source is in `.tools/effekseer-runtime`; the authoring tool and resources are in `.tools/effekseer-editor/Effekseer1.80.7Win`. The authoring package bundles its required .NET runtime. Configure the C++ runtime with `BUILD_EXAMPLES=OFF`, `BUILD_GL=OFF`, `BUILD_VULKAN=OFF`, `BUILD_DX9=OFF`, `BUILD_DX11=OFF`, `BUILD_DX12=OFF`, `BUILD_METAL=OFF`, `USE_MSVC_RUNTIME_LIBRARY_DLL=ON`. M7 supplies the Diligent adapter; no second renderer is implemented here.

## Amplitude source exception

Amplitude commit `ff5f2f8be59e0b74445c10f73eb11a7134eb6fd5` has no official SDK release. Its actual XMake recipe requires C++20, XMake >=3.0.0, FlatBuffers ^25.2.10, xsimd ^13.2.0, miniaudio ^0.11.22, Eigen ^5.0.0, LZ4 ^1.9.4 and dylib ^3.0.1 on Windows. XMake 3.1.1 is pinned. FlatBuffers and official flatc are both 25.12.19. xsimd is overridden to 13.2.0 rather than substituting the baseline's incompatible 14.x.

`scripts/prepare_amplitude.py` copies the pinned SDK to an ignored, recipe-specific build source and generates a recorded local patch. It removes the XMake package resolver and imports headers/static archives directly from the exact vcpkg providers, sets `MD`/`MDd`, and disallows optional sample/CLI package acquisition. The SDK's miniaudio device backend remains internal; no DarkAngel miniaudio playback service exists.

After installing the `amplitude-deps` feature into `vcpkg_installed`, run the preparation script. Add `.tools/flatc` and pinned Python to the process PATH. In the generated source directory, configure with the pinned XMake executable: `xmake f -p windows -a x64 -m release -k static --as_package=n --build_samples=n --build_tools=n --unit_tests=n --build_assets=n`, then `xmake -j 2` and `xmake i -o <ignored SDK install prefix>`. The checked helper `python scripts/build_amplitude.py` reproduces the dependency/configure/build/install/import workflow; `--import-only` checks an already installed SDK. Use Debug/MDd for developer debug compatibility (not build-tested here). Preserve the generated patch and actual build/compiler log. Static/CRT acceptance requires successful compilation and imported-target linkage; acquisition and schema generation alone do not prove it.

Upstream's actual `cmake/FindAmplitudeAudioSDK.cmake` imports `SparkyStudios::Audio::Amplitude::SDK::Static` with `AM_SDK_PATH` and `AM_SDK_PLATFORM=x64-windows`. Engine targets stay under CMake/Ninja. The installed static archive layout is `lib/x64-windows/static/Amplitude.lib` (Debug `Amplitude_d.lib`). `cmake/Amplitude.cmake` attaches the actual Windows/MSVC/SIMD definitions and static LZ4/FlatBuffers/dylib transitive targets missing from the upstream imported target. The qualified M0 SDK recipe uses AVX2, matching the upstream build output; future CPU/device qualification remains a separate gate. Static Release /MD build/install and a CMake imported-target link/run have passed without initializing the SDK engine. Audio-device/service acceptance remains M7 work.

## Vendor SDK and inactive gates

EOS 1.19.2.1 reuses the user's authorized Windows C SDK. `cmake/EOS.cmake` checks headers, import library and `EOSSDK-Win64-Shipping.dll`; `cmake/eos.lock.json` records local SHA256 inventory. The x64 DLL's Epic Authenticode signature was checked. SDK location stays in ignored `local.config.json`; M0 headless neither links nor stages EOS. No overlay, privileged credentials or online account configuration are added.

Manifold source stays inactive until terrain Boolean correctness, topology and bounded offline performance gates pass. Its initial serial candidate recipe disables samples/tests, Python/C bindings, TBB, Tracy and downloads. effekseer-ai source plus locked isolated Python wheels stay inactive until matching official Effekseer Core assembly authoring round trip, semantic editing, undo/preview and safety gates pass; wheels are not installed or started as a server. The official FLAC plugin source declares a shared target in this pin. Static registration/seek/loop behavior and the exact Amplitude integration must be proved before activation; acquiring FLAC/Ogg does not satisfy that gate. The engine's active MCP provider remains the official TypeScript package.
