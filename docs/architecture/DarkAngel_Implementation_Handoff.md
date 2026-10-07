# DarkAngel Engine Implementation Handoff

Prepared 7 October 2026 for Ashen Roots. DAE-001 through DAE-021 are locked. This handoff turns those decisions into a sequence of implementation milestones; it does not claim that integrations, benchmarks or acceptance tests have passed.

Start with `DarkAngel_Codex_Prompt_001_Bootstrap.txt`. Its first operation creates the project folder. Acquire and pin the selected dependency stack up front, then compile a small bootstrap. Complete each milestone's observable gates before building substantial systems on it.

## Source of truth

Use the current `DarkAngel_Engine_Architecture_Decision_Log_v0.1.docx` alongside this handoff. The decision records contain details that this summary intentionally does not repeat. Put a copy and a faithful searchable text/Markdown extraction under `docs/architecture/` in the new repository. Preserve tables, IDs, status and caveats in that extraction. The bootstrap prompt governs its immediate scope; the locked architecture governs subsystem behavior. Resolve conflicts explicitly instead of quietly replacing a selected framework.

Keep `docs/implementation/STATUS.md` with each milestone marked Not started, In progress, Blocked or Verified, plus evidence and remaining gates. A dependency can be acquired without being built, and built without being integrated. Keep those statuses separate.

## Locked contracts to carry into code

| Decision | Implementation contract |
|---|---|
| 001 | Owned reflection and versioned serialization with stable TypeId/PropertyId; use Flecs Meta as infrastructure |
| 002 | Flecs worlds, explicit single-threaded phases, staged structural writes; handles include domain/epoch and generation |
| 003 | C++20, CMake, Ninja, Windows x64/MSVC, pinned vcpkg; static internal targets; native changes rebuild and restart |
| 004 | Luau VM per domain, declared typed APIs/dependencies/state, bounded execution, staged migration and cleanup |
| 005 | DiligentCore, D3D12 first and early Vulkan validation; HLSL/DXC; owned stylized rendering |
| 006 | Jolt CharacterVirtual at a 60 Hz authoritative simulation; swept motor movement and isolated owner replay |
| 007 | Ozz plus owned Animation Graph/Action Composer; one human skeleton; unique nonhuman rigs have no retargeting |
| 008 | Native abilities/effects/tags/attributes, atomic validation, activation identities and presentation prediction |
| 009 | EOS Connect/Lobbies/P2P online; GNS LAN; LocalLoopback solo; shared transport interface and server authority |
| 010 | RmlUi runtime UI, native view models and approved commands; Dear ImGui is developer tooling |
| 011 | UUID AssetIDs, JSON source, SQLite rebuildable catalog, SHA-256 cooked artifacts, typed dependency closure and generation retirement |
| 012 | Owned scene assemblies, stable local IDs and patches; prepare/populate/wire/activate; Luau script bindings on objects |
| 013 | Native EditorService commands/queries, revision checks, prepare/commit, semantic diffs and transactional undo |
| 014 | Curated local JSON-RPC surface, named pipe on Windows, separate official TypeScript MCP bridge; scoped authoring and bounded jobs |
| 015 | Mesh-first terrain, offline geometry products, splines and batched scatter; stable IDs for interactive vegetation |
| 016 | One WorldSession, staged cell residency, bounded pins, authoritative RAM ledger, stable references and explicit retirement |
| 017 | Recast/Detour, owned NPC profiles and BT.CPP executor adapter; JSON topology and Luau tasks/conditions/scorers editable without C++ rebuilding |
| 018 | Effekseer through VfxService/Diligent adapter; native weapon trails and decals; full effect authoring remains an acceptance gate |
| 019 | Amplitude through AudioService; SDK events/buses/RTPC/banks; declared JSON authoring and cooked products; null headless audio |
| 020 | Host/server SQLite save slots with versioned JSON records, coherent checkpoint capture, backups and explicit migrations |
| 021 | Install rules, CTest and CPack; separate game/tools/headless staging; Shipping excludes authoring endpoints; user data outside install |

Identity, authority, ownership, content generations and bounded work are foundation concerns. Do not implement offline gameplay first and convert it to multiplayer later. Scripts compose approved services; they do not bypass native combat validation, write raw ECS memory or drive damage from presentation cues.

## Milestones and evidence

| Milestone | Scope | Gate before proceeding |
|---|---|---|
| M0 Project and dependencies | Create repository, tool detection/bootstrap, complete acquisition inventory, immutable pins, CMake presets, small console/headless shells and dependency probes | Clean native Windows configure/build/CTest, repeat bootstrap without duplicate or destructive downloads, relocatable staged shell, truthful acquisition/build status |
| M1 Foundation and script world | IDs, reflection/serialization, WorldDomain handles, single-threaded phases, staged writes, bounded jobs, authority roles and Luau bindings | Reject stale/wrong-domain handles; serialize/remap a scene; two instances have independent state; a declared Luau spin behavior runs in an isolated test; invalid reload keeps prior generation |
| M2 Assets and observable editor shell | SQLite asset catalog, minimal glTF/texture cook, dependency graph/CAS, Diligent D3D12, Dear ImGui shell, basic scene assemblies and transactional property editing | Import a mesh/material and load from cooked files; basic stylized scene draws; command undo/redo and stale revision rejection; failed reload retains valid data; early Vulkan smoke when the renderer slice exists |
| M3 Session and agent foundation | LocalLoopback, GNS LAN and EOS adapter; protocol/content readiness, identities/baseline and session epochs; local command endpoint and official TS MCP bridge | Host/client round trip, authoritative create/move/despawn and late join; offline modes need no EOS login; scoped agent edits use the same transaction as the UI; Shipping has no agent endpoint |
| M4 Character simulation | Jolt scene, 60 Hz motor, input stream, server snapshots, owner prediction/reconciliation, observer interpolation and query history | Slopes/steps/platforms, obstruction and teleport boundaries; two processes move consistently; replay uses isolated query state; missing history and excessive work fail in bounded ways |
| M5 Animation and combat | Human rig/Ozz, Animation Graph, Action Composer, native abilities/effects/tags, authoritative hit windows/projectiles and runtime health UI | Attack, dodge, stagger, damage and death under host/client authority; acceptance/rejection and replay produce one presentation; no damage from predicted ghosts; late join restores current state |
| M6 World state and NPCs | Regions/cells, ledger, scatter/splines, Recast/Detour, server NPC profiles/BT.CPP and scripted nodes | Cell retirement and crossing preserve identity; no duplicate persistent spawns; unloaded nav invalidates queries; agent creates a new Luau node and serializable behavior without a native rebuild |
| M7 VFX and audio | Diligent Effekseer adapter, trails/decals, Amplitude events/banks/routing/spatial audio, authoring and lifecycle | Real effects and sounds through accepted/rejected activations; bounded tails and resource retirement; no-device/headless operation; agent changes actual effect/event content, not only wrapper parameters |
| M8 Saves and campaign loop | SQLite coherent snapshots, host roster, checkpoints, migrations, recovery and save inspection | Claimed pickup/key/gate/altar/scatter/moved spawn survive reload; guest cannot overwrite host state; injected write/migration failure restores a complete revision; world reset differs from slot deletion |
| M9 Release slice | Four-player co-op scenario, PvP test arena, package closure, notices/prerequisites, release provenance and clean-machine validation | Game runs without source/dev PATH/editor cache, retains saves through update, rejects incompatible manifests, and passes measured multiplayer/device/streaming checks |

These milestones order implementation rather than delaying all validation until M9. Each adapter gets its focused acceptance spike before production systems rely on it. Four-player stress and poor-connection tests begin as soon as the relevant session/combat code exists. Terrain, UI, audio and VFX features expand only after their minimal working path is proven.

## Dependency acquisition and integration

Prompt 001 carries the full acquisition inventory. Use one vcpkg manifest graph with milestone features and approved source/SDK exceptions. Pre-download the selected graph; build the bootstrap subset first. Record transitive package versions, features, patches, licenses and bundled providers. Never acquire a dependency twice as independent runtime implementations.

DiligentCore and DiligentTools must come from compatible revisions. If an umbrella checkout is used to preserve its submodule relationship, disable DiligentFX/samples unless specifically used for an isolated reference test; cloning them does not make them engine dependencies. Do not separately compile a second Dear ImGui if DiligentTools already supplies the selected docking implementation.

Amplitude currently documents building its SDK with XMake and importing the installed SDK into CMake. Pin XMake and its package resolution for that source exception; verify the selected commit's actual instructions. Keep the engine build under CMake/Ninja and verify static-library/dynamic-CRT compatibility. The official FLAC plugin is an acquired integration candidate until its static registration/build path is demonstrated. Do not add Rust or Amplitude's separate CLI unless we actually choose that tool.

EOS is a vendor SDK acquired from Epic's official portal. Import an existing authorized download or report the exact missing Windows C SDK/version/path. Do not substitute an unofficial mirror, auto-accept terms or embed privileged credentials. EOS unavailable is a specific bootstrap blocker, not permission to claim online support or to leave public dependency acquisition unfinished.

Manifold and effekseer-ai are evaluation candidates named in the locked architecture. Acquire them as isolated candidate sources, pin them and record the gate that would make them active. They must not become production dependencies merely because the download succeeded. OpenVDB, PhysicsFS, DirectStorage, Live++, Steam Audio, DiligentFX and alternative frameworks remain deferred.

## Working rules for subsequent Codex tasks

- Scope each prompt to a milestone or a measurable subset. Make changes concrete, compile and run its relevant bounded checks, then update status/evidence.
- Reuse upstream libraries behind narrow adapters. Do not reproduce their mixer, archive/database, physics, ECS, graph executor or transport implementation.
- Keep public SDK headers and vendor types private to adapters wherever practical. No renderer dependency in Foundation or Headless.
- Keep source assets, profiles and ordinary behavior authoring agent-editable. A node descriptor plus Luau implementation must not require a new C++ plugin.
- Give schema fields stable IDs and versions from their first real use. Store no pointers/native entity handles in assets, network messages or saves.
- Do not invent millisecond budgets or mark target hardware performance verified from compilation. Track initial candidate values separately from measured results.
- Use compile-only checks for ordinary edits where appropriate; run actual bounded behavior/device/network tests when that milestone's gate requires them.
- Preserve upstream code; centralize source exceptions and patches. Update dependencies intentionally and re-run affected gates.

## First prompt completion and next prompt

Prompt 001 finishes after project/bootstrap/dependency acquisition and the M0 checks. It does not deliver an editor viewport, combat, an online lobby or a production save system. The result should include the absolute project path, actual tool/pin inventory, acquired/build-tested/missing packages, reproducible commands, test logs, stage inventory and remaining blockers.

The next prompt implements M1: foundation metadata, world domains, explicit phases and a minimal declared Luau object behavior. Select a native JSON parser then through a small recorded implementation choice that meets the locked schema/precision/error requirements; no native JSON library was locked by DAE-001. Do not silently introduce a second reflection or persistence model.

## Bootstrap research references

- [vcpkg install and best-effort pre-download](https://learn.microsoft.com/en-us/vcpkg/commands/install)
- [DiligentCore](https://github.com/DiligentGraphics/DiligentCore) and [DiligentTools](https://github.com/DiligentGraphics/DiligentTools)
- [Luau embedding and build](https://github.com/luau-lang/luau)
- [GNS build requirements](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/BUILDING.md)
- [Amplitude source installation](https://docs.amplitudeaudiosdk.com/nightly/getting-started/installation/) and [CMake import](https://docs.amplitudeaudiosdk.com/nightly/integration/cmake-setup/)
- [EOS SDK download reference](https://dev.epicgames.com/docs/epic-online-services/eos-get-started/eos-get-started-reference)
- [DXC official releases](https://github.com/microsoft/DirectXShaderCompiler/releases)

Pins are intentionally resolved on the actual development machine during M0, then committed with validation results. These references do not assert that a particular dependency version combination has already been tested.
