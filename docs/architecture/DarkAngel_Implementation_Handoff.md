# DarkAngel Engine Implementation Handoff

Prepared 7 October 2026 for Ashen Roots. DAE-001 through DAE-021 are locked. This handoff turns those decisions into a sequence of implementation milestones; it does not claim that integrations, benchmarks or acceptance tests have passed.

Start with `DarkAngel_Codex_Prompt_001_Bootstrap.txt`. Its first operation creates the project folder. Acquire and pin the selected dependency stack up front, then compile a small bootstrap. Complete each milestone's observable gates before building substantial systems on it.

## Source of truth

This is the authoritative M0–M9 milestone roadmap. The root handoff is a historical copy retained for provenance. [Final engine vision](../vision/DarkAngel_Engine_Vision.md) owns intended product capabilities; [Editor workflow plan](../implementation/EDITOR_WORKFLOW_PLAN.md) owns incremental authoring delivery. Neither is implementation evidence. Current STATUS/report/continuation headers supersede older checkpoint descriptions; preserve their historical receipts.

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

## Ashen Roots editor workflow workstream

Reconciled 9 October 2026. The [final vision](../vision/DarkAngel_Engine_Vision.md) defines fifteen fixed workflow targets, reusable panels and exclusions. The [editor plan](../implementation/EDITOR_WORKFLOW_PLAN.md) defines incremental delivery. One editor product contains all delivered workflows. Scene is default; Game owns Play/Stop/Pause/Step. Effects may begin inside Ability, Materials inside Assets; Projectile/AOE and Item become auxiliary typed designers. Fixed destinations have customizable saved layouts; arbitrary user workflow creation is deferred until personally needed.

The original milestone table above and locked DAE requirements retain their acceptance semantics. The following planned extensions supply content-production gates; they do not retroactively invalidate M1/M2 receipts or declare future designers delivered. Initial useful forms/preview precede production refinement and M9 end-to-end qualification.

| Milestone | Planned authoring scope and dependencies | Observable extension gate |
|---|---|---|
| M1 Foundation and script world | Stable identities/metadata, versioned scene data, ECS phases, deterministic runtime boundaries, declared Luau state/tasks and controlled reload underpin all workflows. | Existing handle/serialization/isolation/reload gates remain unchanged; later Scripting tools use those APIs. No reopening completed M1 for future UI. |
| M2 Assets and observable editor shell | Existing catalog/CAS/cook, scene hierarchy/inspector/viewport and undo form the foundation. Incremental M4/M5 shell work adds small reusable panel registration, fixed workflow navigation, saved/factory layouts, shared selection, explicit imports, source/runtime distinction and diagnostics. Typed material forms extend the renderer as supported. | Existing initial-profile gates remain unchanged. Later shell increments verify layout persistence/reset, pending-edit and selection preservation, compatible assignment and failed import/publication retention. Future workflows were not required at M2 completion. |
| M3 Session and agent foundation | Loopback/GNS/EOS, session identity/epochs, handshake/content compatibility, bounded wire validation and replication transport support Game/Network Debugger and multiplayer testing. | Keep original round-trip/late-join/Shipping/agent gates and separate real EOS qualification. Debugger reads native state; offline GNS evidence does not close external EOS blockers. |
| M4 Character simulation | Jolt collision/queries/streaming, fixed-tick movement, owner replay/reconciliation and movement diagnostics. Player movement settings are authored in M5 against this motor; future dodge/slide/traversal requires approved rules and queries. | Preserve slopes/steps/platforms/obstruction/teleport and isolated bounded replay gates, plus full clock/fault/streaming/performance qualification. Visualizing motor state is not movement qualification. |
| M5 Animation and combat / initial gameplay authoring | Character and Player configuration, canonical kits, Ability/Effect native forms/statuses/attributes/tags, Composer and animation graphs, hit detection and network-safe presentation; Save → Cook → Fresh Play and initial combat debugging. Minimal cue references use native events; real VFX/audio remain M7. Runtime Health HUD remains M5. | First authoring gate: create/edit ability/effect, pick compatible action/clip, edit hit/commit values, assign one of eight slots and effect binding, Save/cook complete scene closure and test new values in fresh Play; invalid candidates retain old package/resources. Current first loop/history receipts satisfy only their stated subset. Subsequent visual Composer/character preview, structural blocks/graphs/layers/masks and Player/Character tools require focused receipts. Original attack/dodge/stagger/damage/death, rejection/replay/ghost/late-join and full integration gates remain required. |
| M6 World state and NPCs | Preserve cells/regions/ledger, mesh-first terrain/splines/scatter, Recast/Detour and native BT.CPP/JSON with bounded Luau leaves. Add NPC profile/tree/blackboard/sensing/range/combat-kit tools, navigation preview, encounter/spawn and interaction/world-event foundations. NPC dialog references depend on later approved schemas; sound depends on M7. | Preserve identity/cell retirement/persistent spawn/nav invalidation and agent-authored Luau-node gates. Additionally create an NPC using a shared character/kit, configure/debug native decisions, place a cooked encounter and run it; validate terrain collision/nav products and region cycles. Never import Unity YAML execution. |
| M7 VFX and audio | Preserve Effekseer adapter/trails/decals and Amplitude events/banks/buses/RTPC/spatial lifecycle. Add real effect/event creation, preview/import, ambient/music/combat/footstep cues and presentation diagnostics; typed material/shader improvements stay within Diligent ownership. | Existing accepted/rejected cue, bounded tails/retirement, no-device/headless and actual-content editing gates remain. Author a real effect and sound, bind native action/effect events and inspect accepted/rejected lifetime and dependencies; wrapper-only parameter editing is insufficient. |
| M8 Saves and campaign loop | Preserve coherent host SQLite snapshots, roster/checkpoints/migrations/recovery. Extend with approved quest/dialog/objective/world-event definitions, inventory/equipment interfaces, campaign/scene integration and world-state inspection. Broader RmlUi HUD/menu/data-binding authoring extends M5 UI; source/preview first, optional visual designer later. M6 interactions and M7 audio supply dependencies. | Preserve pickup/key/gate/altar/scatter/moved-spawn persistence, host ownership, failure recovery and reset-versus-deletion gates. Author and test a checkpoint/progression interaction and its UI binding through reload; typed authoring cannot become another world-event executor or allow guest save writes. |
| M9 Release slice / integrated game workflow | Preserve four-player co-op, PvP arena, package closure/notices/provenance/prerequisites, clean machine and measured device/multiplayer/streaming qualification. Consolidate development layouts, validation/build/export, performance/regression/stability and authoring usability. | In addition to original release gates, produce an Ashen Roots encounter through import → character/player/NPC → ability/effect/Composer → scene/cues/UI/progression → validate/cook → fresh multiplayer test → packaged run. Reopen the project with saved layouts on clean tools installation; inspect diagnostics and failed publication recovery. Isolated subsystem tests alone do not qualify production capability. |

### Immediate priority and checkpoint discipline

The first native Ability/Effect/action forms, creation/duplication, kit composition and complete fresh-Play loop are delivered; coordinated Save and session draft history are delivered with the limits in [NATIVE_AUTHORING_HISTORY_REPORT.md](../implementation/NATIVE_AUTHORING_HISTORY_REPORT.md). They are not full M5 acceptance. Follow [ABILITY_AUTHORING_CONTINUE.md](../implementation/ABILITY_AUTHORING_CONTINUE.md): first a read-only tick ruler/lane view linked to numeric selection and isolated compatible clip/character scrubbing, then structural/visual editing and broader Character/Player integration. Do not restart an already delivered source-edit/form effort or postpone usable authoring for broad combat expansion.

Keep A (safe source creation/editing and revisioned history/normal-failure rollback), B (typed Ability/Effect/Composer controls, effect composition/eight-slot assignment and preview), and C (complete closure cook/package/frozen-reference validation/resource preparation/fresh isolated Play) together. Never swap only the kit while retaining stale rig/scene/animation resources. Creation/deletion history, automatic crash recovery and background/incremental preparation remain planned; current source files plus SQLite are not crash-atomic.

Use focused changed-source/consumer, failure-retention and one-backend authoring tests for isolated increments. Broaden provider/protocol/physics checks when those boundaries change; run full fault/dodge/combo/projectile/performance/milestone matrices at their integration checkpoints. M4/M5 remain In progress and M6–M9 Not started according to STATUS; live EOS remains externally blocked.

### Ownership reconciliation and deferred proposals

M6 keeps DAE-015 mesh-first terrain and DAE-016/017 streaming/navigation/AI; advanced sculpting is not an initial level-workflow prerequisite. M8 remains saves/campaign, with progression/UI authoring extensions rather than a replacement milestone. DAE-010 requires supported RmlUi source/preview first and defers drag-and-drop UI design. M5 still owns required runtime combat HUD; M7 owns real audio/VFX presentation. Shared Character definitions contain neither player camera/input behavior nor NPC trees/dialog behavior. Earlier separate Item/Projectile workspace proposals consolidate into related typed tools without changing runtime ownership. No frozen decision is changed.

Update STATUS, implementation report and continuation for significant delivered increments; update the vision only for product changes. Preserve historical acceptance evidence and avoid duplicate completion tables.

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
