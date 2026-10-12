# M8: saves and campaign loop

Written 12 October 2026. This is the design note that opens M8. Nothing in M8 is built, and nothing here is Verified. It is written before any code, as the milestone rule asks, and it ends with the decisions that are the user's to make.

## What M8 must deliver

From the implementation handoff (both M8 rows):

- Coherent host-owned SQLite snapshots, a host roster, checkpoints, migrations, recovery and save inspection.
- Gates: a claimed pickup, a key, a gate, an altar, a scatter instance and a moved spawn survive reload; a guest cannot overwrite host state; an injected write or migration failure restores a complete revision; a world reset differs from deleting a slot.
- Extension row: approved quest, dialog, objective and world-event definitions, inventory and equipment interfaces, campaign and scene integration, world-state inspection; and a checkpoint or progression interaction authored and tested, with its UI binding, through reload. Typed authoring must not become a second world-event executor, and nothing may let a guest write a save.

Decision DAE-020 (locked) fixes how: one SQLite file per save slot holding versioned JSON records; a native `PersistenceService` owned by the host; saves at declared safe checkpoints, not mid-combat; one transaction for a whole snapshot and its head pointer; the previous snapshot kept; rollback journal in DELETE mode with synchronous EXTRA; backups through SQLite's backup API; step-by-step migrations on a validated backup; fixed prepared statements only; and a list of what is never saved (poses, hit windows, prediction history, behaviour tree stacks, particles, voices).

## What exists today that M8 stands on

- **SQLite is already in the build** (the asset catalog uses it). The decision keeps the save database separate from the asset cache: a save is not rebuildable.
- **Stable identities exist.** Scene objects have a `StableId` that survives re-cooking; assets have an `AssetId`; network identities are per session and are never a durable key.
- **The durable facts of a session already exist, but only in memory**, all on the authority side of `CharacterSceneSession`: world tags and the events that set them, which single-use interactables were used, which once-only trigger volumes fired, each encounter's phase and wave, each actor's health, and the player's attributes, selected mask and position.
- **Every Play starts from the scene.** A session is built from the `SpawnPlan` and nothing else; there is no way to build one from saved state.
- **There is no session ledger.** DAE-020 puts the persistence service over "the authoritative session ledger" of DAE-016, and cells and the world ledger were deferred by the user on 11 October 2026 with the instruction to ask before starting them. This is the first decision below.
- **Several things the gates name do not exist as content kinds yet**: pickups, keys, altars and an inventory were never built (the user chose encounters as the first placeable kind); scattered models are drawn only and cannot be removed; nothing spawns a persistent object that moves between places.

## Proposed shape

### 1. A narrow session ledger

Not cells, not streaming: the durable overlay of one world instance, which today is one scene.

- A record is `{kind, world, persistent id, type, schema version, payload}`, with the payload a small typed JSON object. First kinds: `world_tag`, `interactable` (used), `trigger` (fired), `encounter` (phase, wave), `player` (checkpoint, declared attributes, selected mask), and `tombstone` (an authored object that is gone for good).
- The authority writes a fact into the ledger when it happens, in the same tick as the gameplay change, so a consumed key and its opened gate cannot be saved apart.
- A session can be built from a scene **and** a ledger: authored objects take their saved overlay, tombstoned ones are not created, encounters start in their saved phase. Loading reconstructs legal state; it never restores a mid-swing action.
- Each kind declares what a checkpoint does to it (keep, reset, or drop), which is the decision's Preserve / ResetAtCheckpoint / Transient.

This is deliberately the subset of DAE-016 that a single scene needs. Cells can be added under it later: a record already carries its world identity.

### 2. The persistence service

A new library, `engine/persistence`, with no gameplay knowledge: it stores and returns snapshots of ledger records.

- **Slot**: one SQLite file with a stable id, a lineage, a display name and a committed revision that only grows.
- **Schema**: slot metadata; snapshots (revision, capture tick, digest, time, reason); records keyed by snapshot, world, kind and identity; one head pointer. All of a snapshot's records and the head update commit in one transaction.
- **Requests** carry an operation id, the slot, the revision the caller believes is current, and a reason. A stale revision is refused; a repeated operation id is answered once.
- **Outcome** is reported as Captured, Writing, Committed or Failed; only Committed means saved.
- **One writer per slot** on a worker, holding the only connection. The capture itself is an immutable copy taken at a safe tick; the worker never reads live session state.
- **Open** runs SQLite's recovery, then checks the container version, the structure, each snapshot's digest and the records' meaning. A damaged head falls back to the previous snapshot, says so, and keeps the damaged file.
- **Backup and export** through SQLite's backup API against a committed revision.

### 3. Checkpoints

A checkpoint is an authored interaction (the interaction foundation of M6 already asks the server and emits a world event). Using it is a gameplay transaction on the ledger; saving is a separate, acknowledged step. If the disk write fails the session has still rested, the previous revision is still on disk, and the player is told and can retry. Load resumes at the recorded checkpoint.

### 4. Migration

Container schema, snapshot contract and each record kind are versioned separately. A migration is a declared step from one version to the next, run on a validated backup; a failure leaves the original untouched. A save from a newer version without a path is refused with a clear message, never overwritten with defaults.

### 5. Host ownership

Capture and write exist only on the authority role. A client-role session has no call that produces or changes a save; the gate is a test that tries.

### 6. Reset and deletion

A world reset makes a new world-instance identity inside the same slot and keeps what the policy says survives (the roster, and whatever progression the user decides). Deleting a slot removes the slot. They are different operations with different confirmations.

### 7. Editor

- Game tab: start Play from a save slot, and a "save at checkpoint" that goes through the real path.
- Tools > Saves: slots, revisions, the records of a revision as readable JSON, verify, export. This is the "save inspection" of the milestone row and the "world-state inspection" of the extension row.

## Order of work, one verification pass each

1. The ledger and its records for what exists (tags, interactables, triggers, encounters, the player's checkpoint), and a session built from a scene plus a ledger. Test: a used lever, a fired trigger, a completed encounter and a world tag survive a rebuild of the session in memory. No disk.
2. The persistence service: commit, load, head and previous snapshot, digest, one writer, stale revision, repeated operation.
3. Failure: an injected write failure in the middle of a transaction leaves the previous head; a damaged snapshot recovers to the previous one; backup and restore.
4. Migration from version 1 to 2 of one record kind, with an injected failure.
5. The checkpoint interaction, Play from a slot, the Saves tool, and the milestone gates that the existing content can show.
6. Then the campaign extensions, each of which needs a schema the user approves first.

## What the gates can and cannot show with today's content

| Gate | State |
|---|---|
| A gate survives reload | Possible in step 1: a world tag, an interactable, a trigger volume. |
| Encounter progress survives | Possible in step 1. |
| Guest cannot overwrite host state | Possible in step 5 (a client-role session). |
| Injected write or migration failure restores a complete revision | Steps 3 and 4. |
| World reset differs from slot deletion | Step 5. |
| A claimed pickup, a key | Needs pickups and an inventory, which do not exist. |
| An altar | Needs the altar placeable kind, which does not exist. |
| A scatter instance | Scatter is drawn only; a removable instance needs scatter collision and identity first. |
| A moved spawn | Nothing moves a persistent spawned object between places yet. |
| A progression interaction with its UI binding through reload | Needs runtime UI as authored content, which the user deferred (UI_PLAN.md). |

## Decisions for the user

None of these should be guessed. The first blocks step 1; the rest block later steps.

1. **The ledger.** May M8 start with the narrow session ledger described above (one scene, no cells)? Cells and the world ledger were deferred on 11 October with "ask before starting".
2. **What a checkpoint is.** An object the player uses to rest, or automatic at certain events, or both? Does resting bring enemies and encounters back, and does it refill health and stamina?
3. **What is saved of the player today.** Checkpoint position, selected mask, and which attributes (or are attributes always refilled on load)?
4. **Inventory.** Pickups and keys are in the milestone's gates and nothing holds an item yet. Is a minimal inventory (keys and pickups, no equipment screen) part of M8's first pass, or after the save service works?
5. **Altars and removable scatter.** Both are in the gates and neither exists. Build them in M8, or mark those gates as waiting on M6 follow-ups?
6. **Quests, dialog, objectives.** Which comes first? Each needs a schema you approve before it is built.
7. **Choosing a slot.** A player-facing slot menu needs the runtime UI, which is deferred. Is picking a slot in the editor only acceptable for M8?
8. **The UI gate.** The extension row asks for a checkpoint interaction "and its UI binding through reload". With the UI plan deferred, should that gate wait, or should the UI plan come back for M8?

## Not in M8

Cells and streaming, cloud saves, encryption or signing, resuming in the middle of combat, guest-owned progression, host migration.

## After M8

The user's direction stands: an editor-wide UX and UI pass before M9. The runtime UI plan stays deferred unless decisions 7 and 8 bring it back.
