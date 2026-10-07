# Implementation status

M0 is **Verified** on native Windows x64. No M0 blocker remains. Full evidence, exact pins and limitations are in [M0 report](M0_REPORT.md), [dependency inventory](dependencies.json) and [verification receipts](evidence/verification.json).

| Milestone | Status | Evidence / remaining gate |
|---|---|---|
| M0 project and dependencies | Verified | Three profiles each 4/4 CTest; clean source checkout 4/4; repeat acquisition; rebuild and relocated package checks; static /MD SDK import proof |
| M1 foundation and script world | In progress | 17/17 native tests in each Debug/RelWithDebInfo/Release profile. IDs/Flecs Meta, phases/jobs, scenes/remapping, strict Luau closures/shared aliases, dependency reload, declared nested state, bounded coroutine waits/cancellation and cooked Headless pass. Remaining: multiple ScriptAssets/binding IDs per domain/object, per-instance configuration and lifecycle, assembly reference injection, native migrations/optional fields and broader DAE acceptance. See [M1 report](M1_REPORT.md) |
| M2 assets and editor shell | In progress | 21/21 full native CTest, plus cooked-only D3D12/Vulkan viewport captures; GLB validator has zero errors/warnings. SQLite catalog, UUID sidecars, bounded static glTF/texture cook, required closure/SHA-256 CAS, runtime loading and EditorService transactions implemented. Real FBX converted with Blender; original unchanged. Remaining: scene assemblies/patches/lifecycle, document/hierarchy/gizmo workflow, resource reload/pinning/retirement and broader device/package qualification. See [M2 report](M2_REPORT.md) |
| M3 sessions and agent foundation | Not started | LocalLoopback/GNS/EOS and scoped official MCP bridge |
| M4 character simulation | Not started | Jolt motor and authority/prediction gates |
| M5 animation and combat | Not started | Ozz, Animation Graph/Action Composer and native combat |
| M6 world state and NPCs | Not started | Streaming/ledger, Recast and BT.CPP adapter |
| M7 VFX and audio | Not started | Effekseer and Amplitude service/device/authoring lifecycle gates |
| M8 saves and campaign | Not started | Coherent host SQLite checkpoints/migrations |
| M9 release slice | Not started | Shipping closure, clean-machine, multiplayer/device qualification |

The M0 selected stack is acquired; nlohmann/json 3.12.0#2 was deliberately added and build-integrated for M1 through the frozen manifest baseline. Flecs and Luau back Foundation/Runtime. M2 now builds the selected cgltf, meshoptimizer, DirectXTex, DiligentCore/Tools and sole docking ImGui provider with recorded source patches. Vulkan rendering passes without the unavailable Khronos validation layer. Heavy gameplay subsystems and inactive candidates remain unverified. Headless has no renderer/editor/audio/network SDK integration. A developer editor viewport is present; no production editor, agent endpoint or gameplay is claimed.

Reproduce M0 acquisition with [bootstrap instructions](BOOTSTRAP.md), M1 with `python scripts/verify_m1.py --profile m1-debug` (also RelWithDebInfo/Release), and M2 with `python scripts/verify_m2.py --fbx PATH`. Reports and command/log receipts identify the supported profiles and exact remaining gates. Next work is the shared assembly/binding/document contract, then resource generations; the working asset/viewport slice does not mark the broader milestone complete.
