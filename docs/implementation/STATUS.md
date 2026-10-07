# Implementation status

M0 is **Verified** on native Windows x64. No M0 blocker remains. Full evidence, exact pins and limitations are in [M0 report](M0_REPORT.md), [dependency inventory](dependencies.json) and [verification receipts](evidence/verification.json).

| Milestone | Status | Evidence / remaining gate |
|---|---|---|
| M0 project and dependencies | Verified | Three profiles each 4/4 CTest; clean source checkout 4/4; repeat acquisition; rebuild and relocated package checks; static /MD SDK import proof |
| M1 foundation and script world | Not started | IDs, metadata, domains, phases, typed bounded Luau, deliberate JSON parser choice |
| M2 assets and editor shell | Not started | Cook/catalog, Diligent D3D12 and early Vulkan, authoring/undo |
| M3 sessions and agent foundation | Not started | LocalLoopback/GNS/EOS and scoped official MCP bridge |
| M4 character simulation | Not started | Jolt motor and authority/prediction gates |
| M5 animation and combat | Not started | Ozz, Animation Graph/Action Composer and native combat |
| M6 world state and NPCs | Not started | Streaming/ledger, Recast and BT.CPP adapter |
| M7 VFX and audio | Not started | Effekseer and Amplitude service/device/authoring lifecycle gates |
| M8 saves and campaign | Not started | Coherent host SQLite checkpoints/migrations |
| M9 release slice | Not started | Shipping closure, clean-machine, multiplayer/device qualification |

The full selected stack is acquired; heavy subsystem and inactive-candidate acceptance remains unverified. Source and device acquisition never imply game/runtime integration. Engine Headless links only its small static Foundation/Runtime shell; dependencies are exercised by isolated probes. No gameplay implementation or production editor/agent/audio service is present.

Reproduce with [bootstrap instructions](BOOTSTRAP.md). Continue with the M1 task described at the end of the M0 report.
