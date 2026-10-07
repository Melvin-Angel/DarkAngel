# M1 foundation and script-world implementation report

Implemented in `C:\DarkAngel` on 7–8 October 2026 (Europe/Copenhagen). The M0 report/status and complete DAE-001/002/004 records were read before implementation. M1 is **Verified for its implementation-handoff acceptance gates**, including the declared script/binding profile below. The M2 catalog/cooker/renderer/editor slice is now implemented; see [M2 report](M2_REPORT.md).

## Delivered M1 core

- Explicit 128-bit stable object IDs, stable TypeId/PropertyId declarations and schema versions. A single owned property registration table supplies Flecs Meta offsets/types, property semantics, numeric ranges, scene serialization and generated Luau component declarations. Transform v2 has position, yaw/pitch/roll in radians and positive uniform scale, with a registered yaw-only v1 migration; Health has maximum/current; NetworkIdentity is a runtime-only exact uint64 component. Health.current is marked Replicated and enumerable by native consumers.
- Private Flecs backend behind value-based World APIs. Handles contain a unique world token, epoch and Flecs generation-qualified runtime ID. Validity also requires membership in the engine's owned-object registry, so internal Flecs metadata entities cannot be forged into object handles. Wrong world/thread, deletion and epoch reset are rejected. Authoring/server/client/preview worlds own separate data.
- Idle, Script, Commit and Publish phases with explicit transitions. Typed runtime writes and destruction are queued with capacity and authority checks; Flecs performs structural deferral at the declared merge. Queries before commit observe the old values. Authoring reflected edits validate complete final property batches before applying them.
- DarkAngel-owned canonical version-1 scenes with population and stable reference remapping. Required references, fields, versions, IDs and value invariants are validated before population. Raw ECS handles/session NetworkIdentity are omitted. See [native JSON choice](M1_JSON_CHOICE.md).
- Single-thread bounded native Jobs queue with owner/generation cancellation, stale-owner discard and bounded draining. This is a native work-unit queue; it does not preempt expensive native callbacks or provide background execution.
- One Luau VM per ScriptRuntime/world domain, sandboxed candidate environments, immutable exports, typed Context userdata, server/presentation authority enforcement and independently owned declared state/configuration for each stable attachment. The real `games/AshenRoots/scripts/spin.luau` fixture stages yaw writes. Hidden callback upvalues and undeclared exports/state fields are rejected.
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

The completion pass built and passed **22/22** checks in Debug and **26/26** in the optimized M2 profile, which includes every M1 case. Earlier pre-completion RelWithDebInfo/Release runs remain historical 17/17 receipts. To conserve testing, this pass used focused cases during implementation and one full Debug/optimized pass after integration. A final M3-only endpoint-lifecycle follow-up passed in the optimized profile.

| Current profile | Result | Coverage |
|---|---|---|
| m1-debug | 22/22 | Four bootstrap checks, thirteen M1, four M2, one M3 |
| m2-relwithdebinfo | 26/26 | Same foundation/script suite plus real asset and device checks |

Command/exit-code/duration receipts are `evidence/m1-debug.json` and `evidence/m2.json`; logs are retained alongside them. Optimized tests use explicit failures rather than assertions compiled away. Timings describe this machine, not hardware qualification. Source fingerprints distinguish the current completion pass from earlier profile receipts.

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
| M1.bindings | Several assets in one VM; stable attachment IDs; independent speed overrides/state; disable/re-enable, detach/recreate, duplicate ID/config rejection and per-asset profiling |
| M1.state | Nested typed migration, stable field IDs/exact uint64, schema-version mismatch, cyclic/oversized array rejection and task nested-alias synchronization |
| M1.cooked / cook_fixture / cooked_headless | Repeat byte-identical packages, ABI/checksum rejection, cooked-only execution in a separate Headless process |
| M2.editor_transactions | Prepare without live writes; commit validated multi-property Health edit; undo/redo; discarded/stale tokens and revisions; failed batch leaves state unchanged; outside-service write rejection |

## M2 starter

`DarkAngelEditorService` is a separate static test/authoring service that uses the same World metadata/serializer. Stable object/property IDs identify commands. Preparation validates a candidate scene, commit checks revision and baseline, and undo/redo applies the same native property batch path. History/prepared queues are bounded. It does not link into Headless. The M2 window/viewport now consumes this service; the agent endpoint remains pending.

## Completion scope and architecture extensions

The handoff M1 gates pass: stale/wrong-domain rejection, scene serialization/remapping, independent script instances, isolated declared spin and failed reload preservation. This completion also closes the earlier binding gaps: several ScriptAssets share one domain VM, stable attachment IDs address config/state/tasks, immutable typed per-binding configuration supports injected EntityRefs, optional `create` factories prepare fresh declared data, enable/disable cancels owned waits, conflicting enabled transform writers are rejected, and per-asset timing/diagnostics are inspectable. A failing callback rolls back its own staged world writes. Native ScriptBindings is TypeId 4 with a Flecs Meta-backed count and stable-keyed native serialization; source AssetRefs retain canonical UUID text.

M2 supplies the shared assembly resolver, source-authoritative document and prepare/populate/wire/activate session. Script initialization stays private until scene activation. Structural replacement changes the world token, retires old tasks and rejects stale handles. Current scenes support spatial Transform v2 and explicit optional authoring data preservation.

The locked DAE records remain the direction for later extensions: additional native migrations, generalized configuration/reference service APIs, fixed simulation and presentation service hooks, richer subscription/event payloads, full state restore/save integration, and measured script budgets. Stable data snapshots exist; M8 owns the coherent save/restore workflow. These extensions do not upgrade the M1 receipt into a gameplay/network/physics qualification.

M2's initial handoff profile is verified; see [M2 report](M2_REPORT.md). M3 has started with the bounded LocalLoopback transport boundary; see [M3 starter](M3_REPORT.md). M1's added dependency remains nlohmann/json 3.12.0#2 through the frozen baseline. No moving dependency update, commit or remote publication was performed.
