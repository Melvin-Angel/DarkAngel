# M8: world state, inventory, scene transitions and co-op progression

Rewritten 13 October 2026 from the user's M8 brief ([M8_BRIEF.md](M8_BRIEF.md), verbatim). This is the plan that opens M8. Nothing in it is built and nothing is Verified. **Do not start building until the M7 session that was running on 12–13 October has finished and committed**; then begin with stage 1 below.

The first version of this note (12 October, commit `1e14572`, "saves and campaign loop") asked eight questions. The brief answers most of them and widens M8; the mapping is in "What the brief settled". Its persistence-service design (decision DAE-020) is kept and moved into stage 3.

## What M8 must deliver

A small but complete co-op action RPG level, without world-cell streaming:

- a host-owned **world ledger** of persistent progression, keyed by stable authored IDs, surviving scene unloads, deaths, respawns, save and load;
- **full scene transitions** through one `SceneTransitionCoordinator`, with placeable Scene Entry Points, a loading screen lifecycle, failure without a half-transitioned session, and the whole party moving together;
- **items and inventory**: an Items tab, extensible item classifications, masks as items (definition separate from owned instance), per-player server-authoritative inventories, world pickups;
- **altars**: discovery, rest, the shared party checkpoint, restore, resets, optional fast travel and optional save;
- **downed, revive and party wipe** in a native `PartyLifecycleCoordinator`;
- **doors, gates and movable platforms** as reusable state machines with requirements;
- **persistence and reset policies** that separate persistence scope from reset triggers, per logical property;
- **editor authoring** for all of the above, and a **two-scene co-op acceptance slice**.

Not in M8: world-cell streaming or additive loading (the design must not block them), independent travel of one player to another scene, account-level persistence, bleed-out timers, revive tokens, party votes for fast travel, cloud saves, encryption, resuming mid-combat, host migration.

## What the brief settled from the first note

| First note's decision | Settled by the brief |
|---|---|
| 1. Start a narrow session ledger? | Yes, and wider: a world ledger across scenes, independent of the scene manager, ready for cells later. |
| 2. What a checkpoint is | Altars. Activating or resting can set the one shared party checkpoint; resting restores health and resources and resets eligible objects. No automatic checkpoints. |
| 3. What is saved of the player | Inventory, equipped masks and progression. Health and resources are restored by respawn rules, not saved mid-value. |
| 4. Inventory in M8? | Yes, a full item definition and inventory system with an Items tab. |
| 5. Altars and removable scatter | Altars are in. Removable scatter is not mentioned: see open question 4. |
| 6. Quests, dialog, objectives | Not in the brief: see open question 3. |
| 7. Choosing a slot | Not in the brief: see open question 2. |
| 8. The UI gate | The brief asks for a basic runtime inventory UI, a revive indicator, a loading screen and fast-travel selection: see "Runtime UI" below. |

Still in force from the first note: DAE-020 (one SQLite file per slot, versioned JSON records, host-owned `PersistenceService`, saves only at safe points, one transaction per snapshot plus head, previous snapshot kept, backup API, step-wise migration on a validated backup, the never-saved list). The old roadmap gates that the new scope can show remain gates: a claimed pickup, a key, a gate and an altar survive reload; a guest cannot write the host's save; an injected write or migration failure restores a complete revision; a world reset differs from deleting a slot.

## What exists today that M8 stands on

Read from the code and reports on 13 October; re-check at the start of stage 1, because the M7 session is still changing `character_scene.cpp` and the editor.

- **Stable identities**: scene objects have a `StableId`; assets have an `AssetId`; network identities are per session and never durable keys. A persistent ID in M8 is `(scene AssetId, StableId)`, or a scene-independent ID for party-level facts.
- **Durable facts exist only in memory**, on the authority side of `CharacterSceneSession`: world tags and their events, used single-use interactables, fired once-only triggers, encounter phase and wave, actor health, the player's attributes, selected mask and position.
- **A session is built once from a `SpawnPlan`.** There is no scene replacement during Play and no way to build a session from saved state.
- **One player per scene session.** `CharacterSceneSession` takes a single player `StableId`. `WorldSession` already attaches several connections. Two players in one session is therefore the first prerequisite of everything co-op in the brief (stage 1).
- **Interaction foundation (M6)**: interactables with prompt, world event, radius, single use and a required world tag; the server decides every request. Doors, altars and pickups become typed kinds on this same request path; the generic interactable stays as a lever.
- **Encounters, trigger volumes and world events (M6)**, server-only.
- **Masks (M5)**: native Mask assets that own a kit and a worn visual; Players equip four slots; the Mask Editor is already one movable window opened from the Player and NPC tabs. The Items tab becomes a third opener, and no top-level Mask tab is added.
- **Moving bodies in physics**: `PhysicsWorld::set_platform` and the motor's support-relative movement exist; whether a character rides a moving platform correctly under prediction has not been proven in Play (DAE-006 requires replay against historical platform state).
- **Runtime UI**: the RmlUi HUD (Health, Stamina, target, mask name, interaction prompt) is game-owned. The authored runtime UI plan (UI_PLAN.md) is deferred.
- **SQLite** is in the build (asset catalogue); the save database stays separate from it.
- **Networking evidence is loopback only.** Two-machine and GNS/EOS qualification is not available; M8 will report it as unverified.

## Architecture

### Ownership

| State | Owner | Clients |
|---|---|---|
| World ledger | Host | Receive replicated ledger records relevant to their scene |
| Party checkpoint, discovered altars, roster | Host (party record in the ledger) | Read-only replica |
| Inventory, equipment, owned masks, progression | Host, one record per player | Owner sees own; others see equipped masks only |
| Interactions, pickups, doors, platforms | Host decides; clients request | Replicated logical state; platform transforms reconstructed |
| Downed, revive, wipe, respawn | Host (`PartyLifecycleCoordinator`) | Replicated state and revive progress |
| Scene transitions | Host (`SceneTransitionCoordinator`) | Load, report ready, wait |
| Save files | Host only (`PersistenceService`) | No call exists that writes one |

### World ledger (`engine/gameplay` or a new `engine/world`)

- A **record** is `{persistent id, kind, schema version, properties}`, where properties are small typed values (bool, int, enum, tag set, transform). Party-level records (checkpoint, discovered altars, per-player inventories) use the same store with scene-independent IDs.
- **Persistent objects** implement one interface: `capture(PersistentWriter&)` and `restore(const PersistentReader&)` over named logical properties. No per-type persistence code paths, no poses or intermediate positions.
- **Policies per property**, not one enum: a *scope* (session only, save-file persistent) and a set of *reset triggers* (scene entry, rest, party wipe, never). "One-time" is scope = save, triggers = none, plus a tombstone when consumed. Defaults come from the object kind; the Inspector overrides per property. Examples from the brief map directly: a gate's `unlocked` is save/never while its `open` is save/rest; a boss encounter's `completed` is save/never while a normal encounter's is save/rest+wipe.
- **Resets** run in a fixed order (by kind priority, then persistent ID), are idempotent, and happen only on the host; clients receive the resulting records.
- **Writes happen in the tick of the gameplay change**, so a consumed key and the gate it opened cannot be saved apart.
- The ledger outlives scenes: unloading a scene captures its objects; loading restores them before gameplay resumes. Every record carries its world/scene identity so cells can partition it later.
- Versioned serialization of the whole ledger (JSON records) is the payload DAE-020's slots store.

### Scene transitions

- `SceneTransitionCoordinator` (host) runs one transition at a time through explicit phases: **Requested → Validated** (destination scene exists, entry point exists) → **Capturing** (ledger capture of the current scene) → **Announced** (clients told destination and transition id) → **Loading** (host and every client load and build the destination; collision, navigation and gameplay entities ready) → **Restoring** (ledger restore, players placed at the entry point or altar arrival transform) → **Awaiting ready** (every connected client acknowledges) → **Resumed** (control returns).
- **Failure** before Resumed rolls back to the source scene, which is kept alive until the destination is ready; if that is impossible the party is returned to the checkpoint. The session is never left between scenes. A client that fails or times out is dropped from the party, not allowed to block it.
- **Scene Entry Point**: a placeable object with stable ID, position and facing. Altars carry their own arrival transform. A **Scene Transition Trigger** (volume or interactable) and doors can request a transition with a destination scene and entry ID.
- Inventories, masks, progression and the checkpoint live outside the scene, so they cross unchanged.
- A **loading screen** lifecycle (begin, status text, progress, end) is a game-owned RmlUi document fed by the phases.
- Built so that additive loading later is another kind of request through the same coordinator.

### Items and inventory

- **Item definition** (`.daitem` native source): stable AssetId, name, description, classification, icon, optional model, stack limit, unique flag, usable/equippable, and a typed classification block. Classifications are registered kinds (Key, Consumable, Mask, Material, Quest Item, …) with their own property schema and use behaviour, all held by one inventory implementation.
- **Mask as an item**: a Mask item references the existing Mask definition. The definition keeps kit and visual; the owned instance lives in a player's inventory; equipping puts an owned mask into one of the four slots. Player definitions declare a starting inventory and starting equipped masks; NPCs keep referencing mask definitions directly.
- **Inventory** per player on the host: add, remove, stack, split by stack limit, consume, query, equip and unequip; every entry has an instance ID so later per-instance data fits. All changes are server requests with operation IDs so a replayed or duplicated request grants once.
- **Pickups**: a placeable object with item and quantity, collection policy (one-time, respawn on rest, respawn on scene entry) and an optional model from the item. The host claims a pickup once; a simultaneous second request is refused.
- **Editor**: an **Items tab** (list, search, create from a classification, form, icon and model pickers, opens the Mask Editor for Mask items) and a debug **Grant item** action in the Gameplay Inspector during Play.
- **Runtime**: a basic inventory screen (list, use, equip masks) as a game-owned RmlUi document.

### Altars, checkpoints, fast travel

- An **altar** is a placeable kind with stable ID, respawn transform, arrival transform and flags: discoverable, can rest, sets checkpoint on activation, sets checkpoint on rest, fast travel destination, save on rest.
- **Resting** restores every living party member's health and resources, then runs the ledger's *rest* reset trigger.
- The **party checkpoint** is one record: altar persistent ID (which carries its scene) or the initial party spawn when none.
- **Fast travel** lists discovered fast-travel altars; the host chooses; the party travels through the transition coordinator (or teleports within the scene). It does not change the checkpoint unless the altar is configured to.

### Party lifecycle

- `PartyLifecycleCoordinator` on the host owns each member's state: Alive, Downed, Wiped, Respawning.
- At Health 0 a player becomes **Downed** (a gameplay tag that blocks motor input, abilities and interaction; a downed pose and a teammate-visible marker) instead of dying. Solo: an immediate wipe.
- **Revive**: a living teammate in range holds Interact for an authored time; progress is host state, replicated to both players' HUD; release, leaving range, being downed or the target's state changing cancels and resets it; one revive completes once even if two players hold. Health restored on revive is authored.
- **Wipe** when every connected member is Downed (disconnected players do not count). Sequence: wipe presentation, *wipe* reset trigger, transition to the checkpoint scene if different, respawn every member at the checkpoint's respawn transform with authored health and resources.
- Settings live in one party rules asset (revive range, hold time, revive health, respawn health and resources).

### Doors, gates, platforms, requirements

- **Requirement** is one reusable type: has item (optionally consumed), world tag set, encounter completed, ledger property equals. Doors, gates, altars and transition triggers use it.
- **Door/gate** state machine: Locked → Closed ⇄ Opening ⇄ Open ⇄ Closing, with `locked` and `open` as separate logical properties with separate policies; optional transition on use.
- **Movable platform**: start and end transforms or a path of points; triggers (interaction, volume, world event); states Idle, Moving, Arrived, Returning; speed and behaviour (one-way, return after delay, loop). The host moves it as a kinematic body through `set_platform`; the ledger keeps only the logical state and target, and the transform is reconstructed. Riding it under prediction is a stage-5 gate.

### Saves (DAE-020)

`engine/persistence`, gameplay-agnostic, as designed in the first note: slot files, snapshot plus head in one transaction, previous snapshot kept, digest checks and recovery to the previous snapshot, backup and export, one writer per slot on a worker that receives an immutable capture, operation IDs and expected revision on requests, outcome Captured/Writing/Committed/Failed, step-wise migrations. A save captures the ledger and the party record; it is taken at safe points only (altar rest when configured, after a transition resumes, explicit editor save). Load builds the session from scene + ledger and places the party at the checkpoint.

### Runtime UI

The brief needs five small screens: inventory, loading screen, revive progress, altar menu (rest, travel) and fast-travel list. They are built like the existing HUD: game-owned RmlUi documents bound to native view models. This does not reopen the deferred authored-UI plan (UI_PLAN.md); open question 5 confirms that.

## Order of work

Each stage is built, compiled, tested and verified before the next; each ends with a report section, a STATUS and CONTINUE header and a commit. Gates are D3D12 editor gates in the existing verify script unless said otherwise.

1. **Two players, the ledger, the transition foundation.**
   - Two players in one `CharacterSceneSession` (host player plus one client connection in loopback; a test-only second input source in the editor).
   - Persistent IDs, the ledger, the capture/restore interface and the policy model; existing tags, interactables, triggers and encounters move onto it.
   - `SceneTransitionCoordinator` with entry points, the loading screen lifecycle and failure rollback; Scene Entry Point and Transition Trigger placeables.
   - Tests: ledger serialization round-trip and version refusal; capture, unload, reload, restore of a used lever, fired trigger, completed encounter and tag; transition phase order; a missing scene or entry point fails without leaving the session between scenes; two clients both acknowledge before Resumed.
2. **Items, inventory, masks, pickups.**
   - `.daitem`, classifications, the Items tab, Mask items opening the Mask Editor, owned-versus-defined masks, starting inventories, inventory operations, Grant item, the inventory screen, pickups with policies.
   - Tests: stacking, limits, unique items, consume, equip; a duplicate request grants once; two simultaneous pickup claims grant once; inventories cross a transition unchanged.
3. **Altars, checkpoint, fast travel, saves.**
   - Altars, rest, the party checkpoint, fast travel in and across scenes, the altar and travel screens.
   - `engine/persistence` per DAE-020, save on rest, Play from a slot, Tools > Saves inspection.
   - Tests: the first note's persistence steps (commit, head and previous, digest, stale revision, repeated operation, injected write failure, damaged head recovery, migration v1 to v2 with injected failure); a guest cannot write; fast travel leaves the checkpoint alone; a checkpoint in another scene.
4. **Downed, revive, wipe, respawn.**
   - `PartyLifecycleCoordinator`, downed tag and presentation, hold-to-revive with HUD progress, wipe, coordinated respawn across scenes, party rules asset.
   - Tests: revive completes once with two revivers; each interruption cancels and resets; solo wipe; disconnect during downed state; wipe into another scene.
5. **Doors, gates, platforms, requirements, reset policies in the Inspector.**
   - Requirement type, door/gate state machine, movable platform, per-property policy UI with defaults, viewport gizmos for transforms and paths, validation.
   - Tests: key consumption opens a gate exactly once; gate unlocked-but-closes-on-rest; platform reset on scene entry; reset order is deterministic and idempotent; a player rides a moving platform with prediction and correction.
6. **The two-scene acceptance slice and the milestone report.** Build Scene A and Scene B with the editor only (no hand-edited files), run the brief's checklist, save, quit, restart and load.

## Acceptance and what loopback cannot prove

The brief's checklist (section 11) is the milestone gate. In this environment it can be shown with two players in one process over loopback, with the second driven by a test input source. Two machines, real latency, packet loss and GNS or EOS transport are **not** available and will be reported as unverified, not claimed.

## Open questions for the user

None blocks stage 1 except the first.

1. **The second player for development.** Proposal: in-process second client over loopback, driven by a test input source (scripted or a second gamepad) in the editor's Play, so the two-player slice can be run and captured without two machines. A second editor instance over GNS can come later. Agree?
2. **Save slots.** Proposal: one slot per project for M8, chosen in the editor (Play from slot, Tools > Saves); no player-facing slot menu yet. Agree?
3. **Quests, dialog and objectives** from the old M8 extension row are not in the brief. Proposal: move them after M8 (with the editor-wide UX and UI pass, before M9), each with a schema for approval. Agree?
4. **Removable scatter and the "moved spawn" gate** from the old roadmap row. Proposal: drop them from M8's gates and keep them for when scatter has collision and identity. Agree?
5. **Runtime UI scope.** Proposal: the five small screens above as game-owned RmlUi documents like the HUD, without reviving the authored UI plan. Agree?
6. **Guest identity in the host's save.** With EOS blocked, a guest's inventory needs a key that survives restarts. Proposal: a locally generated profile ID per machine, sent at join, used as the roster key; no account system. Agree?

## After M8

Unchanged: the editor-wide UX and UI pass before M9.
