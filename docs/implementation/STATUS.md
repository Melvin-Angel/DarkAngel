# Implementation status

M0 is **Verified** on native Windows x64. No M0 blocker remains. Full evidence, exact pins and limitations are in [M0 report](M0_REPORT.md), [dependency inventory](dependencies.json) and [verification receipts](evidence/verification.json).

| Milestone | Status | Evidence / remaining gate |
|---|---|---|
| M0 project and dependencies | Verified | Three profiles each 4/4 CTest; clean source checkout 4/4; repeat acquisition; rebuild and relocated package checks; static /MD SDK import proof |
| M1 foundation and script world | Verified | Handoff gates pass in the supported profile: owned metadata/IDs, spatial scene migration/remapping, world phases, multiple assets/attachments in one VM, typed per-binding config/state/factories, waits/lifecycle/writer validation, atomic reload/fault preservation and cooked Headless. Current shared Debug suite 22/22; optimized integration 26/26. Extended service/save/physics/network hooks remain later work. See [M1 report](M1_REPORT.md) |
| M2 assets and editor shell | Verified | Initial static asset/assembly/editor handoff gates pass: catalog/cook/CAS, warm reconciliation without conversion, cooked scene/binding preparation and rollback, source-authoritative transactions/save/Open/Play, hierarchy/Inspector/ImGuizmo, resource/shader replacement and lease retirement. Both backends capture real geometry; normalized UI/CLI commands agree. Source FBX unchanged. Full DAE editor/device/package extensions remain explicitly scoped in [M2 report](M2_REPORT.md) |
| M3 sessions and agent foundation | In progress | Shared transport boundary and bounded LocalLoopback admission/round-trip/epoch/ownership checks pass. Authoritative replication/baseline/late join, GNS LAN, EOS online and scoped native endpoint/official MCP bridge remain. See [M3 starter](M3_REPORT.md) |
| M4 character simulation | Not started | Jolt motor and authority/prediction gates |
| M5 animation and combat | Not started | Ozz, Animation Graph/Action Composer and native combat |
| M6 world state and NPCs | Not started | Streaming/ledger, Recast and BT.CPP adapter |
| M7 VFX and audio | Not started | Effekseer and Amplitude service/device/authoring lifecycle gates |
| M8 saves and campaign | Not started | Coherent host SQLite checkpoints/migrations |
| M9 release slice | Not started | Shipping closure, clean-machine, multiplayer/device qualification |

The M0 selected stack is acquired; nlohmann/json 3.12.0#2 was deliberately added and build-integrated for M1 through the frozen manifest baseline. Flecs and Luau back Foundation/Runtime. M2 now builds the selected cgltf, meshoptimizer, DirectXTex, DiligentCore/Tools and sole docking ImGui provider with recorded source patches. Vulkan rendering passes without the unavailable Khronos validation layer. Heavy gameplay subsystems and inactive candidates remain unverified. Headless has no renderer/editor/audio/network SDK integration. A developer editor viewport is present; no production editor, agent endpoint or gameplay is claimed.

Verified marks the implementation-handoff gates for the documented initial profile. The locked DAE records also describe production/extended features that span later milestones; those remain explicit roadmap items rather than being implied by this status. Current completion evidence uses Debug plus one optimized M2 profile; older 17-test optimized M1 receipts predate completion.

Reproduce M0 acquisition with [bootstrap instructions](BOOTSTRAP.md), native checks with `python scripts/verify_m1.py --profile m1-debug`, and the full asset/editor path with `python scripts/verify_m2.py` (or `--fbx PATH` to regenerate the local prop). Next work is M3 session authority/replication and transport adapters, followed by the shared scoped command endpoint. Reports identify exact remaining architecture and qualification work.
