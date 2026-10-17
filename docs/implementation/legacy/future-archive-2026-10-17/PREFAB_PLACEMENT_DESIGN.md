# Prefab placement, quick finding and instance swapping

**16 October 2026: implemented for single-object prefabs (R4).** See the [R4 report](../current/PREFAB_AUTHORING_REPORT.md) for what shipped, the decisions taken and what remains for the later increment.

**Scheduling: Planned, complete R4 (C + F)** in the [authoritative roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026), after R3. Adopt the [UX standards](EDITOR_UX_OVERHAUL_PLAN.md). P0–P4 are internal increments of one usable prefab authoring/publication milestone; all are required to close R4.

15 October 2026. **Proposed design; unimplemented.** Companion to the [14 October prefab proposal](../PREFAB_WORKFLOW_PLAN.md), which supplies the detailed source ownership/publication design. Start/resume through [next authoring work](AUTHORING_NEXT_STEPS_2026-10-15.md).

## The everyday workflow

The existing tool is **Place**, in **Scene / Level Design**. Its search, category tiles, drag into viewport and double-click beside the selection already exist. Add prefab entries there rather than introducing another placer.

1. In **Prefabs**, create `Village Altar`, or later use **Create Prefab from Selection** on an existing modelled altar.
2. Add/choose its Model, Altar and compatible Interactable settings through the shared Inspector; Save the definition.
3. In **Place**, select **Prefabs**, type `altar`, and drag `Village Altar` into the scene. Surface placement uses the existing collision ray. Double-click remains the quick placement shortcut.
4. Move the linked instance with the gizmo. In the Inspector, adjust an instance setting; it receives an override marker and **Revert**.
5. Choose **Prefab: Village Altar > Swap…**, search for `Temple Altar`, inspect the compatibility result and commit the swap. The altar stays in the same place and retains its compatible scene identity/state.
6. **Open Prefab** edits reusable defaults in the fixed Prefabs workspace. **Find in Place** selects the corresponding category and search result; **Reveal in Assets** selects the source. Neither creates a second asset draft.

In **Assets**, a **Prefabs** filter/search lists the same assets. Selecting one and pressing **Edit Prefab** (or double-clicking) enters that fixed workspace with its existing draft/selection restored. This is an explicit entry point, not merely a file reveal. The [Assets/material plan](ASSETS_MATERIAL_EDITING_PLAN.md) defines shared typed routing and model material/texture editing; prefab material-slot assignment overrides must use stable slot identities and this document's scoped patch/swap rules.

Searching matches display name, source filename and folder/category; tags can follow once the inventory supports them. AssetID, not search-result index, is the drag payload/command target, so a catalogue refresh cannot change what is placed. Prefabs and NPC definitions have clear badges: placing an NPC still creates an NPC spawn point, using the existing actor spawning path.

MVP needs search, the Prefabs category, Open/Find/Reveal and the current placement gestures. Recent/favourite items and thumbnail generation are follow-up conveniences, not prerequisites. Do not build a separate prefab browser database; derive entries from the project asset inventory and invalidate on creation/rename/deletion/publication.

## Definition, placement and component ownership

| Thing | Owner | Editor location |
| --- | --- | --- |
| Reusable model/components/defaults | `content/prefabs/*.daprefab`, persistent AssetID | Fixed **Prefabs** workspace |
| Location/yaw, mount identity, label and instance patches | Scene document | Scene outliner and Inspector |
| Player/NPC gameplay definition | Existing `.daplayer`/`.danpc` and referenced profiles | Player/NPC tabs; Open reference from prefab |
| Runtime collection/discovery/door state | Host world ledger, keyed by scene/object identity | Gameplay inspection and existing save tools |

`.daprefab` is the existing assembly entity/mount/export representation with an explicit reusable asset kind. It is not a new gameplay serialization language and is excluded from scene-travel destination pickers. Play consumes the resolved frozen `SpawnPlan`; it does not interpret mutable prefab sources.

The first prefab contains one gameplay entity with its model on that same entity. Current plans are flat: putting a mesh beside a mover in one assembly does not make that mesh move with the mover. Multi-object static composition can follow; general runtime parent/child motion is separate work. NPC prefabs wrap a spawn component/reference; they never duplicate actor construction logic.

The outliner shows an expandable linked-instance row. Select that row to manipulate the mount; select its member to edit component fields. Selection must distinguish `MountSelection` from `ObjectSelection`, because an editor container need not be a gameplay entity. Always show **Editing instance** or **Editing prefab definition**, the owning asset and dirty state.

Add/Remove Component and Model reference edits use shared public authoring commands and the existing decoder rules. They operate on scene sources, prefab sources or instance patches according to explicit scope. Source validation remains authoritative; a greyed-out menu alone does not enforce compatibility.

## Swap Prefab is a first-class command

**Swap** replaces the linked definition of an existing placement. **Place** creates another placement; **Duplicate Instance** creates a fresh mount identity. These are different commands in history.

Proposed command: `PrepareSwapPrefab(selection, replacementAsset, overridePolicy, expectedRevision)` returns a validated preview; `CommitSwapPrefab(token)` installs one undoable scene edit. These names are design notation, not existing APIs.

| Value | Default swap behaviour |
| --- | --- |
| Scene mount/instance identity | Preserve |
| Position/yaw and scene instance label | Preserve |
| Compatible persistent root identity and scene references | Preserve through explicit member mapping |
| New prefab's model, components and defaults | Adopt |
| Compatible explicit local field overrides | Preserve, listed in preview |
| Field/component no longer supported | Report; require an explicit discard choice before commit, or refuse |
| Runtime state in a running Play | No change; authoring swap is disabled during Play |
| Existing save data | Preserve only where the persistence contract is compatible; never silently reset/reassign |

The default picker offers compatible replacements. For the first single-entity workflow, compatibility means the same gameplay/persistent-property contract (altar to altar, mover to compatible mover, pickup to compatible pickup, plain prop to prop). Model appearance alone is not enough. Changing a pickup's item after it was collected must not make it collectible again; changing an altar's appearance must not erase discovery. Changing a persistent gameplay kind or field policy requires a later explicit migration/replacement workflow, so the MVP rejects it with a concrete reason.

An NPC spawn swap may choose another `.danpc` reference through a compatible spawn prefab, but must preserve the spawn identity and validate its encounter/ledger policy. This is a next-fresh-Play definition change, not a live actor morph. Never transfer a collected/discovered record to an unrelated new member.

The preview lists retained placement, mapped identity/references, carried overrides, explicitly discarded overrides, adopted defaults and any invalid dependencies. **Use replacement defaults** is an explicit override policy; it removes the listed local patches while preserving placement. A failed/stale preparation leaves all scene source and history unchanged. Undo restores the old link and its exact patches/identity mapping; Redo reapplies the validated edit against the dependency policy below.

### Identity is the nontrivial part

Current resolver identity is derived from placement/mount path **and the member's local key**. Keeping only the mount ID when swapping two prefabs with different member keys does not preserve the object's runtime identity. This must be solved before shipping Swap.

Extend the retained-identity mechanism proposed in the older document: a scene-owned, bounded map from the currently linked member key to a retained scene object ID. The first single-entity prefab exposes a declared semantic root/export; Swap explicitly maps old root to replacement root after compatibility validation and carries the old scene object ID. This mapping lives only on the scene placement, never in a shared prefab. Validate uniqueness, export/reference expansion and collision with every normally derived object ID.

For ordinary new placements, derive fresh identity normally. Duplicate Instance clears retained IDs and allocates a fresh mount, so copies cannot share persistent state. An explicit conversion from a loose object can use the same retained-identity machinery later. A future multi-member swap needs an explicit stable member/export mapping; do not guess by display name, order, mesh or position.

## Existing foundation and required extensions

Source inspection on 15 October confirms `assembly.cpp` supports bounded mounts, typed patches and exports. It visits mount assets with a new scope but no mount placement transform. `EditorDocument` currently owns/materializes a complete source map and histories of that map. External prefab propagation and scoped edits therefore need changes, not just new UI.

1. **External links and ownership.** Scene-owned sources and externally linked prefab sources must be distinct. Keep legacy embedded sources embedded until explicit extraction/linking. Refresh published dependencies without overwriting unsaved scene patches. History owns the scene's edits, not copies of a shared prefab that Undo might write back.
2. **Mount transform.** Resolve local entities/patches, then compose the mount's position and yaw into object transforms, volumes, arrival offsets and mover travel. Old mounts default to identity. MVP mount scale is one; pitch/roll/scale wait for defined collider/volume/mover behaviour. Ordinary instance placement is never included in Apply to Prefab.
3. **Typed gameplay patches.** Add validated component/field operations beside existing reflected `SetProperty`, `ReplaceSlot`, `SetSpawn` and script binding operations. Use registered component/field identities, not raw JSON pointer strings or array indices.
4. **Dependency repair.** Keep unresolved patches visible with their original bytes and owner. Invalid candidates cannot Save/cook/Play; retain a last good authoring preview with a clear stale/invalid indication. Never drop patches silently.
5. **Publication.** Reuse AssetService candidate validation/CAS/recovery, extended to assembly/document sources. Current coordinated native publication does not already cover prefab/scene transactions. Atomic Apply/selection conversion requires that extension.

Frozen Play resources keep their generation until Stop/new Play. Definition saves refresh dependent authoring views and invalidate stale prepare tokens; fresh Play re-resolves, cooks and prepares the scene-derived static collision layer from the refreshed placements and per-model collision products; use the completed [scene-scale architecture](../current/SCENE_SCALE_PLAN.md), not the superseded whole-scene bake. Changed definitions can invalidate old patches: report the affected consumers before publication rather than hiding the break.

## MVP boundaries and sequence

| Step | Visible result | Required proof |
| --- | --- | --- |
| P0 | Versioned link/placement/member-identity contracts and reusable commands | Old embedded scenes unchanged; missing/cyclic/stale sources reject; identity/reference mapping deterministic |
| P1 | Add Component, Model picker and isolated Prefabs workspace | One modelled gameplay entity; Undo/Redo; Save/reopen; invalid component mix leaves draft intact |
| P2 | Linked prefab entries in Place; instance move/duplicate/delete; scalar overrides/Revert | Two independent copies in two scenes; template changes propagate; local override survives; new copy has independent ledger state |
| P3 | Compatible Swap with preview and explicit override policy | Pose/ID/references persist across different source member keys; save/load/travel retain state; incompatible swap refuses; Undo/Redo complete |
| P4 | Atomic Apply Selected and Create Prefab from Selection | Scene and definition commit/recover together; converted identities remain stable; failed publication preserves drafts |
| Later | Nested authoring, variants, multiple members, Unpack and in-context editing | Explicit transforms/member mappings and ownership; no implicit runtime hierarchy |

P0-P3 provide an intermediate place/edit/swap loop; they do not complete R4. P4 Apply Selected and identity-preserving Create Prefab from Selection are required parts of the same milestone. Nested-prefab work remains later.

## Focused verification and source map

Extend existing assembly/editor/asset test families through public commands: identity across same-kind swaps with different local keys; changed defaults plus local override; incompatible/missing reference rejection; duplicate isolation; stale writers; Undo/Redo; legacy source loading. At resolver integration, check actual model collision and rotated altar arrival/mover paths, plus the existing M8 party/travel gate for host/guest frozen-closure agreement. A D3D12 authoring gate should find an asset in Place, place it, swap it, Save and start fresh Play. Do not rerun broad device/provider matrices for every Inspector field.

Implementation owners: `engine/runtime/assembly.cpp`, `tools/editor/editor_document.cpp`, `apps/editor/editor_ui.*`, `apps/editor/editor_controller.*`, `engine/assets/asset_service.cpp`, `apps/editor/scene_collision.cpp`. Introduce focused `PrefabWorkspace`/component command modules using the completed refactor's service boundaries; do not grow `main.cpp` or make the runtime depend on editor code.
