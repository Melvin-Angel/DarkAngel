# M0 acquisition plan

Resolved on 7 October 2026. Acquire before compiling. Never install vendor tools unattended or accept terms automatically.

One pinned vcpkg checkout (2026.07.29, c76c06644034521fb761a39f8f52d8e87d1103d5) supplies suitable ports. M0 selects Flecs, Luau and SQLite only. Later features select Jolt, Ozz offline/runtime, GNS, RmlUi/FreeType, ImGuizmo, cgltf, meshoptimizer, DirectXTex and Recast/Detour, BehaviorTree.CPP. Confirm names/features/recipes at this baseline before writing the manifest. Exact overrides freeze the resolved graph. Port source archives go in the vcpkg download cache, not redundant source checkouts.

Approved source exceptions: compatible DiligentCore/Tools pins from DiligentEngine's recorded submodule graph (exclude FX/samples); DiligentTools supplies the sole ImGui provider, requiring docking verification. Amplitude has an XMake source-build recipe, then CMake import; pin its dependencies and matching flatc. Effekseer runtime and authoring tool use the same official release. EOS reuses the user's authorized SDK, never commits it. MCP uses the official npm package with package-lock.json, no second SDK checkout.

Inactive candidate checkouts: Manifold, effekseer-ai, Amplitude plugin-flac. Acceptance gates remain pending; acquire their required submodules/LFS without enabling engine linkage.

Portable tools: official CMake/Ninja/DXC/XMake/Effekseer/Node packages, matching flatc after Amplitude recipe inspection. Reuse Git/Python/PowerShell where suitable and record exact versions. glTF Validator uses the official Khronos npm distribution. Do not install Rust or a Vulkan SDK for M0 headless.

Windows compiler/SDK detection uses vswhere. An absent compiler is a concrete native-validation blocker; still finish public acquisition, source checks and prepared verification scripts. Sources, hashes, recipes, licenses and per-package statuses are recorded separately from integration results. Downloads use at most three workers; native jobs at most four on this machine (about 17 GiB available memory and 142 GiB disk at initial detection).
