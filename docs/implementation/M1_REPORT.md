# M1 foundation and script-world implementation report

Implemented in `C:\DarkAngel` on 7–8 October 2026 (Europe/Copenhagen). The M0 report/status and complete DAE-001/002/004 records were read before implementation. The core handoff spike is implemented and behavior-tested; M1 remains **In progress** for the broader script integration acceptance listed below. The M2 catalog/cooker/renderer/editor slice is now implemented; see [M2 report](M2_REPORT.md).

## Delivered M1 core

- Explicit 128-bit stable object IDs, stable TypeId/PropertyId declarations and schema versions. A single owned property registration table supplies Flecs Meta offsets/types, property semantics, numeric ranges, scene serialization and generated Luau component declarations. Transform currently has yaw; Health has maximum/current; NetworkIdentity is a runtime-only exact uint64 component. Health.current is marked Replicated and enumerable by native consumers.
- Private Flecs backend behind value-based World APIs. Handles contain a unique world token, epoch and Flecs generation-qualified runtime ID. Validity also requires membership in the engine's owned-object registry, so internal Flecs metadata entities cannot be forged into object handles. Wrong world/thread, deletion and epoch reset are rejected. Authoring/server/client/preview worlds own separate data.
- Idle, Script, Commit and Publish phases with explicit transitions. Typed runtime writes and destruction are queued with capacity and authority checks; Flecs performs structural deferral at the declared merge. Queries before commit observe the old values. Authoring reflected edits validate complete final property batches before applying them.
- DarkAngel-owned canonical version-1 scenes with population and stable reference remapping. Required references, fields, versions, IDs and value invariants are validated before population. Raw ECS handles/session NetworkIdentity are omitted. See [native JSON choice](M1_JSON_CHOICE.md).
- Single-thread bounded native Jobs queue with owner/generation cancellation, stale-owner discard and bounded draining. This is a native work-unit queue; it does not preempt expensive native callbacks or provide background execution.
- One Luau VM per ScriptRuntime/world domain, sandboxed candidate environments, immutable exports, typed Context userdata, server/presentation authority enforcement and independently owned declared numeric angle state for each attachment. The real `games/AshenRoots/scripts/spin.luau` fixture stages yaw writes. Hidden callback upvalues and undeclared exports/state fields are rejected.
- Matching pinned Luau Analysis checks strict scripts against generated component/Context and declared BehaviorState definitions before compilation. Analyzer/runtime imports share one relative/alias resolver and a statically declared dependency closure. Missing/computed/undeclared imports, cycles and unused closure modules fail preparation. Immutable module exports/defaults can be shared; mutable captured assignments and runtime export/captured-table mutation are rejected. Source names carry stable script identity; runtime protected calls disable only the failing attachment.
- Idle-safe reload prepares new code/definitions and all migrated instance states before changing the active generation. Candidate errors preserve old definitions/state. Successful explicit state migration commits together. World state stays outside the script-state clone. Entity deletion cleans attachments on the next bounded tick; explicit detach and VM shutdown release ownership.
- Candidate quotas: 64 KiB source, 256 attachments, 8 MiB VM allocations and 20 ms per protected call by default. These are development safety limits, not performance results. Interrupts apply at Luau execution safepoints; garbage-collector callbacks never raise an unprotected error. Analyzer module timeout is 250 ms with its upstream complexity limits.
- Source packages compile into deterministic, checksum-protected cooked bytecode closures with matching Luau 0.729/native API fingerprints. Headless can run `--script artifact.dascript` through the cooked-only loading API. Candidate dependencies are staged together; changing a helper replaces its dependent entry. Failed dependency load retains the active closure and state.
- Host-managed tasks use generation/owner-qualified coroutines, bounded simulation-time/event waits and resume/event capacities. Delivered events remain latched for tasks skipped by the resume budget. Successful reload cancels old stacks; failed reload preserves them. Disable, detach, deletion and destruction clean task ownership. Inspection exposes wait reason/generation and aggregate per-runtime protected-call timing/failure counters.
- Declared state supports stable PropertyId-keyed records, bounded arrays, numbers, booleans, strings, canonical entity/asset references and exact uint64 decimal strings. Schema-generated BehaviorState types use the same declaration as runtime validation. Metatables, cycles, sparse arrays, functions/threads/userdata and undeclared fields fail. Schema changes require a version increment; migration uses isolated data and validates every staged result before commit. Versioned stable-field snapshots retain exact uint64 values. Task waits preserve nested table identity while refreshing data changed by update callbacks.

## Verification

Native configure/build/CTest runs use the pinned MSVC/SDK environment and checked-in `m1-debug`, `m1-relwithdebinfo`, `m1-release` presets. Reproduce with:

```powershell
python scripts/verify_m1.py --profile m1-debug
python scripts/verify_m1.py --profile m1-relwithdebinfo
python scripts/verify_m1.py --profile m1-release
```

All three final profiles configured, built and passed **17/17 CTest checks**.

| Profile | Configure s | Build s | CTest s | Result |
|---|---:|---:|---:|---|
| m1-debug | 4.807 | 14.662 | 3.131 | 17/17 |
| m1-relwithdebinfo | 8.375 | 51.621 | 3.764 | 17/17 |
| m1-release | 31.555 | 50.780 | 4.216 | 17/17 |

Final commands, exit codes, elapsed times and full logs are in `evidence/m1-*.json` and `evidence/m1-*-{configure,build,ctest}.log`. Each profile runs four retained M0 checks, twelve M1 cases (including the cooker fixture and cooked Headless invocation) and one M2 transaction case. Tests use explicit failures in optimized builds, not assertions compiled away. Measured timings describe this machine only; profiles built concurrently with the M2 build, so they are not benchmark comparisons.

| Case | Observable checks |
|---|---|
| M1.identity | Wrong domain/generation/epoch/thread and forged backend ID rejection; actual Flecs Meta presence; Replicated property and generated type enumeration |
| M1.scene | Canonical round trip; reference remapping; authoring/play isolation; exact stable IDs; transient network ID exclusion; failed edit/load leaves old/empty data; malformed/duplicate-key/future-version/reference failures |
| M1.phases | Old values before merge; visible changes/destruction after merge; phase/authority/capacity rejection; job cancellation and stale-owner discard |
| M1.scripts | Real typed spin on two independent objects; deferred writes; separate client VM cannot mutate server gameplay; deletion/detach cleanup |
| M1.reload | Syntax/type errors; migration error/invalid state; preserved prior generation/state; successful migration; safe-point restriction; hidden-state rejection |
| M1.limits | Interruptible infinite loop at preparation and update; isolated attachment disable; VM/source/instance/world capacity limits; oversized candidate allocation rejection |
| M1.imports | Shared alias/relative mapping, dependent helper replacement, failed dependency preservation, missing/cyclic/computed import rejection and timing counters |
| M1.tasks | Event/time waits, resume capacity, latched readiness, explicit cancellation, deletion/detach/reload cleanup and failed reload stack preservation |
| M1.state | Nested typed migration, stable field IDs/exact uint64, schema-version mismatch, cyclic/oversized array rejection and task nested-alias synchronization |
| M1.cooked / cook_fixture / cooked_headless | Repeat byte-identical packages, ABI/checksum rejection, cooked-only execution in a separate Headless process |
| M2.editor_transactions | Prepare without live writes; commit validated multi-property Health edit; undo/redo; discarded/stale tokens and revisions; failed batch leaves state unchanged; outside-service write rejection |

## M2 starter

`DarkAngelEditorService` is a separate static test/authoring service that uses the same World metadata/serializer. Stable object/property IDs identify commands. Preparation validates a candidate scene, commit checks revision and baseline, and undo/redo applies the same native property batch path. History/prepared queues are bounded. It does not link into Headless. The M2 window/viewport now consumes this service; the agent endpoint remains pending.

## Remaining gates

M1's core handoff checks pass, including module packages, host-managed waits, cooked Headless and richer state. Keep M1 **In progress** for the broader locked DAE contract: one domain VM hosting several ScriptAssets/attachments with stable binding IDs, per-instance typed configuration overrides, enable/re-enable/subscription lifecycle, explicit assembly-injected reference resolution, native component migrations/unknown optional-field preservation and per-script rather than aggregate runtime profiling. Current runtime owns one entry/dependency closure per domain with one attachment per object and a speed-only configuration. State snapshots are inspectable but do not yet provide the full save/restore contract. Canonical EntityRef/AssetRef validation stores stable data; it does not prove the target is loaded or bind it to an engine service.

M2 remains **In progress**. The rebuildable asset catalog, UUID identities, static mesh/material/color texture cook, required closure/CAS, editor Inspector and D3D12/Vulkan rendering are implemented. Scene assembly resolution/lifecycle, runtime resource reload/retirement and broader document/editor/device/package gates remain. The M0 historical report/package remains an M0 receipt.

M1's new third-party dependency is deliberately pinned nlohmann/json 3.12.0#2; its lock and notice are retained. M2 builds the already frozen selected asset/renderer providers with recorded source adapter patches. Historical M0 preset names remain supported but now resolve the M1 Foundation dependency feature as well. No remote publication or moving dependency update was performed.
