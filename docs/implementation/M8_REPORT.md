# M8: world state, inventory, scene transitions and co-op progression

Rewritten 13 October 2026 from the user's M8 brief ([M8_BRIEF.md](M8_BRIEF.md), verbatim). Stage 1a is built; the first two stage-1b increments are built with headless fault tests and the existing editor regression gates passing (see the last section). Stage 1b and M8 remain In progress; the remaining stages are not built and nothing is Verified.

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

The brief's checklist (section 11) is the milestone gate. Stage 1a shows two players in one process over loopback. The subsequent stage-1b decision requires separate editor processes over the existing GNS UDP adapter on this machine; that adapter already has M3 transport tests, but scene play over it is not yet built. Two-machine play, real-world latency/loss and EOS remain unverified. The independent-client delivery-fault harness proves only its controlled one-process cases.

## Open questions for the user

These were the original proposals. The second-player question was settled as 1a then 1b in the design below; on 13 October the user approved all the remaining defaults, with improvements recorded in the stage-1b design. They no longer await answers.

1. **The second player for development.** Proposal: in-process second client over loopback, driven by a test input source (scripted or a second gamepad) in the editor's Play, so the two-player slice can be run and captured without two machines. A second editor instance over GNS can come later. Agree?
2. **Save slots.** Proposal: one slot per project for M8, chosen in the editor (Play from slot, Tools > Saves); no player-facing slot menu yet. Agree?
3. **Quests, dialog and objectives** from the old M8 extension row are not in the brief. Proposal: move them after M8 (with the editor-wide UX and UI pass, before M9), each with a schema for approval. Agree?
4. **Removable scatter and the "moved spawn" gate** from the old roadmap row. Proposal: drop them from M8's gates and keep them for when scatter has collision and identity. Agree?
5. **Runtime UI scope.** Proposal: the five small screens above as game-owned RmlUi documents like the HUD, without reviving the authored UI plan. Agree?
6. **Guest identity in the host's save.** With EOS blocked, a guest's inventory needs a key that survives restarts. Proposal: a locally generated profile ID per machine, sent at join, used as the roster key; no account system. Agree?

## After M8

Unchanged: the editor-wide UX and UI pass before M9.

## Stage 1: design (13 October 2026, written before the code)

Stage 1 is two players in one session, the world ledger with stable persistent IDs, and the scene transition foundation. This section settles the second-player question first, because the rest of the stage depends on how a second player connects.

### What the code is today

Read on 13 October from `engine/runtime/character_scene.*`, `world_session.hpp`, `transport.hpp`, `assembly.hpp`, the editor's Play path and the M3/M4 reports.

- **`CharacterSceneSession` is a host and one client fused in one object.** Its `Impl` owns the server `WorldSession`, the authoritative physics world, the host motor and every NPC. It also owns a client `WorldSession`, the prediction world, owner motor and ability prediction, the observers, effect views, cue and footstep trackers and the player's pose. The two halves are joined by an in-process loopback pair.
- **It runs in lockstep.** Each `step()` ticks the server and then pumps the loopback until the client holds *this tick's* motor state, collision frame, ability correction and public frames ("snapshot delivery bound"). Reconciliation requires the correction's tick to equal the current tick ("Atomic combat correction clock"). The client has never run on its own clock against a host whose answers arrive later.
- **The host code assumes one player.** It uses one `player` network id, one `host_motor` and one `ability_owner`. Interaction requests are resolved only for `player`. Trigger volumes and encounter enter-triggers step with the one owner position. `occupied()`, sensing, NPC soft lock and NPC aim lead all name that one player.
- **`WorldSession` is already multi-peer**: `SessionLimits::peers` is 4, `own_motor` binds a character to a connection, and packet counters are kept per connection. Its wire has separate-process GNS evidence (M3 create/move/despawn, M4 ninety motor commands, M5 ability intents and effects), all with stepped harnesses. None of that evidence covers the scene: combat, NPCs, prediction with presentation, or encounters.
- **Capacity**: `session_character_capacity` is 33, the player plus 32 actors. Every extra player takes one actor place.
- **Play** builds one `CharacterSceneSession` from the open scene's `SpawnPlan` (`prepare_character_preview`). There is no game executable; the editor is the only program that plays a scene.
- **World tags** come only from encounters, held while they run; **world events** are moments. Neither is durable by itself. Durable facts today are: an interactable's use count, a trigger's fire count, an encounter's phase, and which spawned NPCs are dead.

### The second player: separate instances (Multiplayer Play Mode) against the in-process fallback

**What a separate instance needs, in order of cost:**

1. **Host and client split.** Every option needs this. `CharacterSceneSession` becomes `SceneAuthority` (server half, N players) and `SceneClient` (client half, one owner), talking only through a `Transport`.
2. **A client that tolerates delay. This is the largest cost and the real risk.** A process on GNS cannot be stepped in lockstep with the host. It needs its own fixed clock, predicting ahead of the newest correction; inputs kept until acknowledged; reconciliation against a correction older than the current tick, replaying the newer inputs (the motor and ability prediction classes are built for this, but the scene loop has never driven them that way); observers interpolated from frames that arrive late or out of step; and recovery when a frame never comes. The owner semantic history is bounded at 30 ticks, so a client that falls 0.5 s behind must resynchronize.
3. **A client-only editor mode**: `DarkAngelEditor --join 127.0.0.1:<port> --player <n>`. It opens the same project, loads the same cooked Play package, sends the content hash in the handshake (the host refuses a mismatch), builds only a `SceneClient` plus rendering, input, camera and HUD, and never constructs authority, NPC controllers or saves. Most of the Play preparation is shared with the host. Server-only parts such as behaviour trees, navigation and encounter directors are skipped.
4. **The scene stream over GNS UDP.** The transport exists. What is new is the full scene stream through it, inside the budget of 16 packets per tick per peer.
5. **A launcher.** Play gets a "Players: 1–4" setting. The host opens a GNS listen socket and starts N−1 child editor processes, each with its own window placed beside the host's. Stop ends them. A child that exits or fails its handshake is reported and dropped from the party.
6. **Gates.** A gate starts the host hidden with `--players 2`. The host starts a hidden child with a scripted input source (`--input-script walk-to:x,z;interact`). Both write their own log and capture, and the gate script checks both. Everything runs on one machine, so it shows two processes agreeing, not behaviour under real latency.

**The in-process fallback needs:** item 1, plus a second `SceneClient` on its own loopback pair inside the editor process, pumped in lockstep like today. A second Game viewport panel shows player 2's camera and HUD. Keyboard and mouse drive the focused viewport's player, a gamepad can be assigned to a player, and gates use a scripted input source. Lockstep stays, so item 2 is not needed yet.

**Cost and value:**

| | In-process loopback | Separate instances |
|---|---|---|
| Host/client split, N-player authority | needed | needed |
| Delay-tolerant client | not needed | needed, the largest piece |
| Client-only editor mode, launcher | second viewport, input routing (small) | needed (medium) |
| What it proves | two players' gameplay rules: shared world, both seen by NPCs, both interact, transitions move both | the above plus two processes agreeing over UDP, each with its own window and input |
| Real latency or loss | no | no (one machine) |
| Rough size | the stage-1 core plus about one increment | the same plus about two to three increments |

**Recommendation.** The separate instances are feasible and are the right end state: the M8 brief's transitions with client readiness, revive under authority and host-only saves only mean something when a client can lag. But everything the fallback needs is a strict subset of what the separate instances need. So:

- **Stage 1a:** the split, N-player authority, the ledger and the transition coordinator, shown with an in-process second client over loopback (second viewport, scripted input in gates).
- **Stage 1b**, before stage 2: the delay-tolerant `SceneClient`, the `--join` client mode, the launcher and two-process gates. The editor's "Players" setting then starts real child instances. The in-process client stays for the fast headless tests.

If 1b turns out larger than estimated, its progress is reported at the end of stage 1 and the user decides whether to finish it before items and inventory or move on with in-process co-op. **This order needs the user's agreement before building** (question S1 at the end of this section).

### The host/client split

- **`SceneAuthority`** (new, `engine/runtime/scene_authority.*`): the server `WorldSession`, authoritative physics and collision history, actors, world events, interactables, triggers, encounters, spawning, sensing and controllers, and the **party members**: `{StableId identity, network id, motor, ability owner, connection, input queue}`. `join(member, connection)` creates the member's character at its place; `leave(member)` removes it. Members share the player faction (no friendly fire). Players plus actors must fit 33.
  - Everything that read the one `player` becomes per member: interactions resolve for the requesting member; trigger volumes and encounter enter-triggers fire for any living member; `occupied()`, sensing, NPC soft lock and aim lead consider every member. `ControllerView` already lists perceived actors by identity, so behaviour trees need no change.
- **`SceneClient`** (new, `engine/runtime/scene_client.*`): the client `WorldSession`, prediction world, owner motor, owner ability prediction, semantic history and soft-lock memory; observers and `ObservedCharacterPose` for every other character, the other players included; effect views, cue and footstep trackers, the owner's graph and pose, the interaction prompt. In 1a it is stepped in lockstep by its owner; in 1b it gets its own clock.
- **`CharacterSceneSession`** stays as the composition the editor and nine test files use: one authority plus one local client on loopback, with the same public API. It gains `add_local_player(identity)`, which adds a second loopback client, and accessors per player (`motor(StableId)`, `pose(StableId)` already exists, `interaction_prompt(StableId)`, HUD state per player).
- **Player definitions**: in stage 1 every member uses the Play's player definition (kit, masks, attributes). Per-player inventories and masks come in stage 2.
- **Order inside the work**: first the mechanical split with one player and *every existing test unchanged and passing*, then N players. The split moves code; it is not a rewrite of the prediction rules.

### Party placement

- A **Scene Entry Point** is a scene object with an `"entry": {"name": "..."}` component: stable ID, position, facing. `SpawnPlan` gains `entries` (StableId → name). Names are labels for pickers and need not be unique; the StableId is the destination identifier.
- The party is placed in a fixed formation around the entry point: the first member on it, the others on a ring of 1.2 m in a fixed order. Each place is snapped to the ground with a ray and moved further out, the way an encounter spawn is, when a body blocks it.
- Initial Play keeps today's rule: the scene's player object is where the party starts. A scene without entry points still plays.

### The world ledger

`engine/runtime/world_ledger.*`. It has no knowledge of scenes being loaded; it stores values.

- **`PersistentId`** = `{AssetId scene, StableId object}`. Party-level facts use a nil scene. Encounter enemies and spawn-point NPCs already have derived StableIds (`encounter_actor`, `spawned_actor`), so they are addressable without new identity.
- **Record**: `{PersistentId id, std::string kind, unsigned schema, properties}`. A **property** is `{name, value, policy}`. The value is bool, integer, real, text or transform (position and yaw). The **policy** is `{scope: Session | Save, resets: set of SceneEntry, Rest, PartyWipe}`. The policy is written with the value, so a reset can run over records of scenes that are not loaded (resting in scene B resets a gate in scene A). A property absent from the ledger means "as authored".
- **Persistent objects** implement one interface: `kind()`, `capture(LedgerWriter&)` and `restore(const LedgerReader&)`. Stage 1 implements it for what exists:

| Object | Properties (default policy) |
|---|---|
| Interactable | `used` count (single use: Save, no resets; repeatable: not recorded) |
| Trigger volume | `fired` (once: Save, no resets; repeating: not recorded) |
| Encounter | `completed` (Save, resets on Rest and PartyWipe). A running encounter is not recorded: after a reload it waits again, so a half-fought encounter restarts. |
| Spawn-point NPC | `defeated` (Save, resets on Rest and PartyWipe). A defeated NPC is not created. |

  Per-object overrides of these defaults come with the Inspector policy editor in stage 5. Stage 1 hard-codes the defaults in one table per kind, not per object.
- **Writes happen in the tick** of the gameplay change, by the authority. **Capture** of a whole scene happens when it unloads; **restore** happens after a scene is built and before its first tick (encounters and spawn points read it while being created).
- **Resets** run one trigger at a time, in a fixed order (scene id, kind, object id, property name). They remove matching properties and are idempotent: running the same reset twice changes nothing the second time. A loaded scene's objects are restored again from the result; the transition and lifecycle coordinators (stages 3 and 4) call them.
- **Serialization**: versioned JSON. The container has a version and records are sorted by id. A record of an unknown kind or a newer schema is kept as it is and reported, never dropped or overwritten with defaults. This JSON is what the DAE-020 slot stores in stage 3.
- **Authority only.** Clients get no ledger in stage 1: what they need to see (a used lever, an open door) reaches them through ordinary replicated object state. Replicating party-level records (discovered altars, the checkpoint) comes with the screens that show them in stage 3.
- **Owner**: the ledger is owned by the Play controller (in a game, the party host) and outlives every scene session. `SceneAuthority` gets a reference to it.

### Scene transitions

`engine/runtime/scene_transition.*`.

- **`SceneProvider`**, an interface implemented by the editor (and later the game), turns a scene AssetId into a **`PreparedScene`**: everything a `SceneAuthority` and `SceneClient` are built from (objects, collision, the combat configuration, navigation, entries, audio profile). It can run over several frames (`begin`, `poll` with progress and status text, `cancel`), so the loading screen moves. The editor implementation reuses Play's existing preparation: cook if needed, collision, navigation, NPC and encounter preparation.
- **`SceneTransitionCoordinator`** runs one request at a time: `{destination scene, entry StableId, reason}`. Phases:
  1. **Requested**
  2. **Preparing**: the provider loads the destination; the source scene keeps running, with player input held.
  3. **Validating**: the scene exists, the entry exists in it, the party fits its capacity.
  4. **Capturing**: the ledger takes the source scene's state.
  5. **Swapping**: the source session is destroyed and the destination session is built with the ledger and the party.
  6. **Placing**: the party is placed at the entry.
  7. **Awaiting ready**: every client holds its bootstrap and collision acknowledgement.
  8. **Resumed**
- **Failure.** A failure in Preparing or Validating (missing scene, missing entry, load error, no capacity) cancels: the source session never stopped, input returns, and the reason is reported. A failure in Swapping, after the source is gone, rebuilds the source scene from the ledger at the entry the party last came in by (or the scene's player object) and reports it. The session is never left with no scene. A failure to rebuild the source is fatal to Play and says so. Stage 3 adds "return to the checkpoint" as the first fallback.
- **Requests** come from authority, never from a client directly. A **Scene Transition Trigger** is a scene object with a `"transition": {"scene": AssetId, "entry": StableId, "activation": "enter" | "interact", "prompt": "..."}` component and a box like a trigger volume. With `enter`, it fires when any living member walks in; with `interact`, it goes through the ordinary interaction request. The session exposes the request after the tick, and the coordinator, which is owned next to the ledger, takes it. Doors, altars and fast travel later use the same request type. Party travel is whole-party by definition: there is no per-player request.
- **What crosses**: the party (members, identities, their player definitions) and the ledger. Nothing else is carried; inventories and the checkpoint join in stages 2 and 3, outside the scene for the same reason.
- **Loading screen**: the coordinator exposes `{phase, progress, text}`. A game-owned RmlUi document (like the HUD, under `games/AshenRoots/ui`) is shown over every player's view from Requested to Resumed. A 0.3 s minimum avoids a flash.
- **Separate instances (1b)**: the swap announces `{transition id, scene, entry, new session epoch}` to every client over a small party control channel that outlives scene sessions. Clients prepare the same scene from their own package, then acknowledge. The host waits for every acknowledgement or drops a client after a timeout. Stage 1a has in-process clients only, so the swap rebuilds them directly. The control channel is designed in 1b.
- **Additive loading later** is another request kind through the same coordinator; nothing here assumes one scene is the whole world, except that one session is one scene.

### Tests and gates

Headless (CTest):

- **`WorldLedgerTests`**: write, read and erase; JSON round trip; a newer container refused; an unknown kind kept; reset order and idempotence; a reset over an unloaded scene.
- **`CharacterSceneTests` / `CharacterCombatSceneTests`**: unchanged after the split; this proves the mechanical split.
- **New `PartySceneTests`**:
  - two players move, both predicted and corrected;
  - player 2 interacts with a lever, and player 1's client sees it used;
  - a trigger fires for player 2;
  - an encounter's enter-trigger fires for player 2;
  - an NPC detects and attacks player 2;
  - players cannot hurt each other;
  - capacity is refused at 34 characters.
- **New `SceneTransitionTests`**, with a provider that builds small scenes in code:
  - phase order;
  - both players arrive at the entry formation;
  - a used lever and a completed encounter of scene A are restored on the way back;
  - a defeated spawn NPC stays gone;
  - a missing scene and a missing entry leave the party in the source scene, still running;
  - a failure during swap rebuilds the source scene;
  - a transition requested during another is refused.

Editor gates (D3D12):

- **`two-players-d3d12`**: Play with two local players. Player 2 is driven by a script to a lever and uses it; the capture shows both viewports.
- **`transition-d3d12`**: in a fixture copy of RoyalTraining, a transition trigger and an entry point are placed through editor commands, leading to RoyalVillage (with an entry point placed there too). Both players walk in, the loading overlay is captured, and both arrive. Walking back restores the lever.

Not shown by stage 1a: any separate process, any delay. Those are 1b's gates.

### Questions for the user before building

- **S1. The second player, in this order:** 1a in-process loopback first (second viewport, scripted input), then 1b real separate instances with their own windows over GNS on localhost, before stage 2. Agree, or go straight to separate instances, or keep in-process only until later?
- **S2. Building here.** This session reaches the project through a Linux shell and cannot run MSVC, CMake, CTest or the D3D12 gates. Building and verifying need computer use of a PowerShell window on the user's PC (logs read back from here), the user running a runner script, or a Claude Code session running on the PC itself.

**User answers (13 October 2026):** S1: 1a then 1b, as recommended; 1b comes before stage 2. S2: building and verification continue in a Claude Code session running natively on the PC. Nothing of stage 1 is built; the design above is the next session's starting point.

## Stage 1a: built (13 October 2026)

Stage 1a of the design above is built: the host and client halves are separate, the host half carries a party, the world ledger exists, and the party travels between scenes through one coordinator. Everything below ran in **one process, over in-process loopback, in lockstep**. Nothing here is evidence about separate processes, delay, loss or two machines; that is stage 1b and later.

### What was built

**The split.** `CharacterSceneSession` was one object holding a host and one client. It is now three:

- `SceneAuthority` (`engine/runtime/scene_authority.*`): the server `WorldSession`, authoritative physics, the party members' characters, actors, world events, interactables, trigger volumes, encounters, sensing and controllers.
- `SceneClient` (`engine/runtime/scene_client.*`): the client `WorldSession`, the prediction world, owner motor and ability prediction, observers and poses of every other character, effect views, cue and footstep trackers, the owner's pose and the interaction prompt.
- `CharacterSceneSession`: the composition the editor and tests use, with the public API it had. It owns one authority and one client per local player, each client on its own loopback pair, and steps them in lockstep.

A client no longer reads any host state. It learns which characters exist from its replicated world, and what they are (attributes, hostile or not) from the same prepared scene content the host was built from, including the enemies an encounter can spawn (their identities are derived from the encounter, so the client can name them without being told). It keeps its own collision history from the frames it received, where it used to replay against the host's. Its own pose follows its confirmed motor state from the wire. The existing scene tests passed unchanged on this.

**N players.** Authority holds up to four party members. What named the one player is now per member: interactions are resolved for the member that asked; trigger volumes and an encounter's enter trigger fire for any living member; spawn placement, sensing, NPC soft lock and aim lead consider every member. Members share the party's faction, so they cannot hurt each other. Every member takes one of the session's 33 character places. Members are created with the session: a member cannot join a session that is already running (the party travels and enters scenes together; stage 1b's party channel sits above scene sessions).

One addition the design did not list: when a single-use interactable is spent, authority tells the other members' clients (a notice with the result `UsedUp` on the existing interaction answer path), so they stop offering it. The same notice is sent when a session is built with an interactable already spent in the ledger.

**The second local player in the editor.** The Game tab has a Players setting (1 to 4). With more than one, Play builds the party in formation around the scene's player object, shows a second Game viewport from player 2's own client (its world, its poses, its camera, its HUD values), and draws the other players in each view. A click in a viewport gives that player the keyboard and mouse. For gates, `--players N` and `--input-script "walk-to:x,z;interact;wait:n;await-scene:n"` drive player 2, and `--player-script` drives player 1 the same way.

**The world ledger** (`engine/runtime/world_ledger.*`): values under `{scene asset, object StableId}`, a nil scene for party-level facts. A property is a bool, integer, real, text or transform with a policy written beside it: scope (session or save) and any of three reset triggers (scene entry, rest, party wipe). Objects reach it through one interface (`PersistentObject`: `kind`, `capture`, `restore`). Resets run in a fixed order (scene, kind, object, property) and are idempotent; they work on scenes that are not loaded, because the ledger never knew what was loaded. Serialization is versioned JSON sorted by identity; a newer container is refused; a record of an unknown kind or a newer schema is kept verbatim, reported, and never written, restored from or reset.

What uses it now, with the default policies of the design:

| Object | Recorded | Policy |
|---|---|---|
| Single-use interactable | `used` | save, never resets |
| Once-only trigger volume | `fired` | save, never resets |
| Encounter | `completed`, or `started` while it runs | save, resets on rest and party wipe |
| Scene actor (a spawn point's NPC, a placed dummy) | `defeated` | save, resets on rest and party wipe |

Authority writes in the tick of the change and captures the whole scene again when it is unloaded; it restores before the first tick. A defeated scene actor is not created. `started` is an addition to the design: without it, an encounter started by a single-use lever could never start again after the scene was left mid-fight (the lever stays used, the encounter waits). An encounter recorded as started starts again from its first wave when the scene is built; it is not resumed mid-wave.

**Scene travel.**

- A **Scene Entry Point** is a scene object with an `"entry": {"name"}` component; its stable identity is the destination identifier. A **Scene Transition Trigger** is an object with a `"transition"` component: destination scene asset, optional entry point, `enter` (any living member walks into its box) or `interact` (a member uses it on purpose, offered on the HUD like an interactable). Both are in the Place panel (Gameplay), have Inspector sections (the transition's destination scene and entry point are picked from the project's scenes) and viewport gizmos.
- `SceneTransitionCoordinator` (`engine/runtime/scene_transition.*`) owns the running session. Phases, one step per update: Requested, Preparing, Validating, Capturing, Swapping, Placing, Awaiting ready, Resumed. A failure before the swap cancels and the source session never stopped. A failure during the swap builds the source scene again from the ledger, the party where it last came in. If that fails too, Play ends and says so. A request during another is refused. The party arrives in formation: the first member on the point, the others on a 1.2 m ring starting behind it, each put on the ground and moved out when a body stands there.
- `SceneProvider` turns a scene asset into a `PreparedScene` over several polls. The editor's provider is Play's own preparation run for the destination scene: it opens the destination's document, packages and uploads its resources and builds its Preview world. The editor's document, resources and uploaded models then belong to the destination; the authoring document is set aside and comes back at Stop.
- **Loading screen**: a panel in the game's own HUD document (`games/AshenRoots/ui/hud.rml`), bound to three view-model fields, shown in every player's view from Requested to Resumed, at least 0.3 s. It is in the HUD document and not a document of its own because RmlUi allows one `Hud` at a time in this build; for the same reason both viewports are drawn from the one `Hud`, laid out twice a frame.

### What was tested

Headless, all pass:

- `WorldLedgerTests` (`M8.world_ledger`): write, read, erase; capture and restore through the interface with a policy per property; reset order, idempotence and a reset of an unloaded scene; JSON round trip; newer container refused; malformed input refused; unknown kind and newer schema kept verbatim and untouched by capture, write and reset.
- `PartySceneTests` (`M8.party_scene`): two players each predicted and corrected and presented by the other's client; player 2 pulls a lever, authority answers player 2 only, player 1's client stops offering it and is refused when it tries; a trigger volume and an encounter's enter trigger fire for player 2; a guard senses both and attacks the nearer; one player's swing does not damage the other; player 2's own swing hits a hostile and is paid from its own Stamina; 34 characters and a fifth player are refused.
- `SceneTransitionTests` (`M8.scene_transition`), with a provider that builds scenes in code: phase order; the loading minimum; both players at the entry formation; walk-in and used-on-purpose triggers, the second from player 2; a used lever, a completed encounter and a defeated actor restored on the way back, without their events being emitted again; a rest while the party is elsewhere resetting the encounter and the actor of the unloaded scene and not the lever; a running encounter starting again; a missing scene, a missing entry and a scene the party does not fit cancelled with the source still running; a failed swap rebuilding the source; a request during another refused; the component decoders.
- The existing scene tests (`CharacterSceneTests`, `CharacterCombatSceneTests` and the others that build a session) unchanged.

Editor gates (D3D12, hidden editor, captures looked at):

- `two-players-d3d12`: Play of the lever scene with two local players; player 2 walks round player 1 to the lever and uses it; authority accepts for player 2; both viewports and both HUDs are drawn.
- `transition-place-d3d12`: an entry point and a return trigger placed in the temple scene through the Place panel's items and a scene edit, then saved.
- `transition-d3d12`: the gate scene's entry point and walk-in trigger placed the same way; player 1 pulls the lever and walks in; the party arrives at the temple's entry point; player 2 uses the way back; the party is in the gate scene again with the lever still used and one ledger record.
- `transition-loading-d3d12`: the loading panel held open, in both views.
- `transition-missing-d3d12` and `transition-no-entry-d3d12`: a destination that is not in the project, and an entry point the destination does not have. The second is only found after the editor has prepared the destination, so the editor has to follow the party back to the scene it is in; the lever is then used to show Play still works.

Results of the full pass on the committed tree (13 October 2026): full build with only the known `RoyalCollisionTests` link failure; **CTest 98 of 100** (`M4.royal_mesh_collision`, the known link failure; `M5.attribute_authoring` hit its 90 s CTest limit and passes when run directly, in about 105 s on this machine today; it does not link the scene library, and `M5.native_authoring` took 14 minutes in the same run); **all 67 verify gates pass**. An earlier pass of this stage was spoiled by my editing `games/AshenRoots/ui/hud.rml` while it ran, and was discarded and repeated.

### What was not tested, and limits

- **No separate process, no delay, no loss.** Both clients are stepped in lockstep with the host in one process. A client that lags, a client loading a destination on its own and acknowledging, and two processes over UDP are stage 1b.
- The design's gate scene pair was RoyalTraining and RoyalVillage. The gates use two scenes on the training layout instead (each its own scene asset), because **a scene does not name its collision asset**: Play takes one from the command line, and the editor's provider uses that same asset for every scene. A destination whose placements the asset does not match fails its preparation and the party stays where it is. Travel between differently built scenes needs the scene to name its collision (see the questions).
- The second swap-failure path (the source cannot be rebuilt either, Play ends) is written and not exercised by a test.
- In the editor, interactive control of player 2 (click its viewport, then keyboard and mouse) was not tried by hand; gates drive it by script. Gamepad assignment to a player is not built. Player 2's view has no target bar and no worn mask drawn.
- The entry point and transition Inspector sections and the destination pickers were not looked at; the gates set the components through the same scene edit the Inspector makes.
- While the party travels, the editor draws no scene objects under the loading panel, because its document and resources may already be the destination's.
- The first member's identity is the scene's player object, which differs between scenes; companions keep theirs (`party_member(seat)`). Per-player inventories in stage 2 need a member identity that does not depend on the scene.
- An encounter recorded as started restarts when its scene is built; if a rest erases that record while the single-use lever that started it stays used, it waits for an event that will not come. That is an authored-policy matter the per-object policies of stage 5 address; until then such a lever should be repeatable.
- Members enter with the session. Nothing joins or leaves a running session yet.
- Scene files saved in the manifest-and-records form are found by the provider only if their manifest carries the `root` key; this was not checked against such a file.

### Questions for the user

- **Q1. Which collision does a scene use?** Proposal: a scene object names the scene's collision asset, the way one object names its audio profile today, with a "Use for this scene" control; Play and the transition provider read it, and the command-line flag stays as an override for gates. Agree?
- **Q2. Stage 1b before stage 2, as decided.** It starts with the delay-tolerant `SceneClient` (own clock, inputs kept until acknowledged, reconciliation against older corrections). No decision needed unless the order should change.

## Stage 1b: design (13 October 2026)

Authorized after the read-only orientation. Stage 1b precedes items and inventory. Its increments follow CONTINUE: independent client simulation, party control channel, client-only editor over GNS, child-process Play, then two-process gates. The existing serialized WorldSession and transport remain the gameplay path; no parallel gameplay authority is introduced.

User answer during this increment: use the proposed defaults, with discretion to improve the design and record changes here. Q1 is approved: a scene names its collision asset, with a command-line override. The earlier proposals are also approved: one editor-selected save slot per project; quests/dialog/objectives after M8; removable scatter/moved-spawn gates deferred; game-owned runtime UI screens; locally generated guest profile identity. These are approved scope decisions, not implemented features.

### Independent scene simulation

- Keep strict lockstep as the default for CharacterSceneSession and its existing tests. Add an explicit delay-tolerant mode to SceneAuthority and SceneClient. Authority consumes exactly one tick, whether or not a command arrived. Missing commands hold the last movement, yaw, crouch and pitch; jump is never repeated. Sequence remains the sequence actually received. Late ability intents retain their existing InputExpired result; the eight-tick lead bounds stay.
- SceneClient owns a 60 Hz clock. It receives independently, predicts without waiting for the host's current tick, and presents its predicted owner. A bounded lead target grows when corrections report sequence < tick, and decays slowly after sustained timely input. Do not send beyond the protocol's eight-tick lead bound relative to the latest confirmation. Retain clock debt while awaiting confirmation rather than overflowing queues or histories.
- Prepare and acknowledge collision on receipt, after geometry validation. Keep original frames by host tick. For prediction at a later tick, apply the newest frame as a held copy stamped with the prediction world's tick. For replay, build a bounded history using the newest received frame at or before each required tick, held forward. Held bodies are an approximation until new host frames arrive, not extrapolated authoritative physics.
- Reconcile a coherent motor/ability correction once, against its original tick; regenerate motion from retained semantic inputs and reconcile motor and ability candidates together. Public motor and ability streams can arrive separately: delay presentation of an unpaired public frame, keep the last coherent observer sample, and allow new actors to wait for their first coherent frame.
- Bootstrap and rebase from a coherent correction and collision frame, never from an old replicated object transform. A changed epoch/topology, host ahead of the client, missing history, or failed replay rebuilds the private prediction world and ability predictor. Pending inputs retired by a rebase are explicitly discarded; monotonic operation IDs are preserved. No live world is rewound.
- Native tests use separate authority/client instances with bounded delivery delays: older corrections with newer inputs retained, held collision, host movement/yaw fallback without repeated jump, late ability refusal, observer stream skew, late bootstrap, history exhaustion/rebase and topology replacement. Existing strict scene tests must remain unchanged.

### Party channel and separate processes

A bounded, versioned party control channel outlives scene sessions and carries roster/member identity, prepared content hash, scene/entry, transition ID and session epoch, ready/failure and leave messages. It uses the existing Transport adapters. A client cannot publish shared state. The host validates membership and content before constructing scene seats. Each scene switch creates a fresh WorldSession epoch; all clients prepare locally and acknowledge before control resumes. Failure/timeout drops the failed member or rolls the party back coherently; old-epoch messages cannot enter the new scene. Party identity must be independent of each scene's player object before inventories arrive.

Identity refinement for that next increment: keep a persistent profile/member ID in the party roster, mapped to the current scene avatar's StableId and seat. Do not rename the authored player object to a profile ID: existing scene bindings and preparation name that authored object. Inventories/equipment use the profile key; scene simulation continues to use its avatar and network identities. The mapping changes on travel while the profile stays. This resolves the stage-1a identity limitation without changing authored scene identities.

The editor's --join host:port --player n path shares asset and renderer preparation but builds only a SceneClient. Host Play listens over GNS and Players launches N-1 child editor windows; Stop closes owned children, and exits/failed handshakes remove members. Keep loopback composition for headless tests. Two-process gates exercise movement, combat, interaction, content mismatch, disconnect and travel/readiness, with each process's logs and captures. These prove same-machine UDP only; two-machine latency/loss and EOS remain unverified.

Every increment ends with the full guarded build, full CTest, then verify_effect_reaction.py, followed by current evidence in STATUS/CONTINUE/report and a local commit. No dependency installation/rebuild, no push, no edits to runtime assets or scripts during verification. Q1 (scene collision selection) is approved and not implemented yet; it does not block the first increment.

### Increment 1: independent client foundation built (13 October 2026)

SceneTiming::Independent is opt-in on SceneAuthority and SceneClient; the default composition and editor remain strict lockstep. The independent client has a bounded 60 Hz clock, retained one-shot inputs and debt, an adaptive lead of 2 to 6 ticks within the existing eight-tick wire bound, receipt-time collision preparation/ACK, held collision copies for prediction/replay, older motor/ability correction reconciliation, and wire-baseline reconstruction. Missing host movement input holds the last received movement/facing/posture, not action-modified movement, and never repeats jump. Owner presentation follows prediction. Rebase preserves monotonic operation IDs and rebuilds the owner graph clock; observer pose clocks restart on a discontinuity while received observer buffers remain.

Design refinement found by the delayed test: motor/public streams are paired only when their ticks match or the public content index confirms the held snapshot for the newer motor tick. Observers sample two ticks behind the newest reconciled host tick. A held active action is clamped to its last valid authored clock until confirmed completion, preventing an invented Active snapshot past its duration. This does not change gameplay authority.

SceneDelayTests (M8.scene_delay) passes with two independent scene clients and the real WorldSession wire on a test transport with externally clocked delivery delay. It covers older corrections with newer input retained, movement and combat, other-player poses, no routine rebases during 120 delayed ticks, missing input retaining movement/sequence/yaw without repeating jump, a client paused beyond 30 ticks, changed collision topology, InputExpired for a deliberately late ability, deterministic dropped unreliable packets, and bootstrap at tick 60 after the authored airborne position is obsolete. This is a one-process fault harness, not a network qualification.

Final validation: full guarded build with only the known RoyalCollisionTests link failure; full CTest **98 of 101**. M8.world_ledger, M8.party_scene, M8.scene_delay, M8.scene_transition and the existing scene tests pass. M4.royal_mesh_collision cannot run because the executable does not link; M5.actor_assets and M5.attribute_authoring exceeded their 90-second limits. Both pass directly against separately copied fixtures (119.3 and 149.1 seconds). NativeAuthoringTests passed in CTest in 1000.68 seconds. The unchanged verify_effect_reaction.py ran in full and **all 67 gates pass**; its total was 1612.1 seconds, including its own fixture generation. The two-players and transition-loading captures from this run were inspected: both views/HUDs and both loading panels are present. These editor gates exercise the existing lockstep composition, not independent mode.

Evidence: [receipt](evidence/m8-1b-client.json), [build](evidence/m8-1b-build.log), [CTest](evidence/m8-1b-test.log), [fault test](evidence/m8-1b-delay.log), [actor direct](evidence/m8-1b-actor-direct.log), [attribute direct](evidence/m8-1b-attribute-direct.log), [editor regression](evidence/m8-1b-editor.log). Build used the environment returned by scripts/msvc_environment.py activate(), the pinned CMake, --parallel 4 -- -k 0, VCPKG_MANIFEST_INSTALL=OFF and DILIGENT_NO_DIRECT3D12=OFF. No dependency installation/rebuild, push, protected scene edit or stash operation.

Not built by this increment: party control channel, live membership, GNS client-only editor mode, process launcher or two-process travel/readiness. Independent mode has no editor gate yet. Moving-platform prediction, multi-machine networking, real-world latency/loss, EOS and the later M8 stages remain unverified.

The next part of client integration must cover asynchronous WorldSession world-revision bootstrap and encounter actors entering/leaving, plus cue/effect presentation under delayed delivery. In particular, CatchUp can clear correction maps before Ready; independent advance currently assumes the owner bundle is available after receive. Hold advancement/presentation while that bootstrap completes, then recover from the new coherent baseline. The current fault harness keeps its party/actor roster fixed. This frontier must be closed before the mode backs editor clients; it is not covered by the successful strict encounter gates. Then proceed to the party channel, --join, launcher and two-process gates in the agreed order. Stage 2 still waits for stage 1b.

### Increment 2: asynchronous bootstrap and party control foundation built (13 October 2026)

Independent advancement now holds while WorldSession is not Ready or lacks a coherent owner/collision bundle. A world revision discards cached collision frames; the next coherent bundle rebases prediction. The delayed encounter test spawns a low-health enemy, receives its pose, kills it, receives its removal, and checks cue start/stop/release. It deliberately sends movement and an ability while an actor spawn rebuilds the wire world. WorldSession previously dropped such an already-admitted peer for a motor-command state error; it now ignores movement during CatchUp and answers those ability requests InputExpired. Initial admission, ownership and command lead checks remain enforced. Both the original delay cases and the expanded encounter case pass in the focused test.

PartyControl is a separate bounded ReliableOrdered channel on existing Transport adapters, with persistent profile IDs mapped to per-scene avatar IDs, assigned seats, scene/entry offers, fresh gameplay epochs, readiness, abort, timeout, failure, leave and roster removal. The protocol carries no gameplay. The host checks actual scene ID, handshake content and epoch before readiness; guests cannot publish host-only messages. Offers have a 4096-byte bound, queues have 16-message bounds, and JSON depth/work and integer fields are bounded. A failed transition holds control until the caller offers a rebuilt source with another fresh epoch. A readiness timeout starts after the host finishes local preparation, allowing slow host cooking without prematurely timing out guests.

Design refinement: each member keeps its party control connection while its separate gameplay connection/WorldSession is replaced on travel. This fits the existing one-peer GNS adapter without a new gameplay multiplex codec. Admission in this foundation is before scene entry; joining an already-running scene is not implemented. The application must consume member drops and remove the corresponding gameplay seat; the control protocol alone does not mutate SceneAuthority. No editor/GNS bridge or local profile persistence is built by this increment yet.

PartyControlTests (M8.party_control) focused checks pass: join, profile/avatar mapping across travel, all-member readiness barrier, stale acknowledgement, fresh epochs after abort, source-rebuild offer, content mismatch, scene identity mismatch, timeout, leave with survivor roster update, duplicate identity and message direction. Final validation: full guarded build with only the known RoyalCollisionTests link failure; CTest **99 of 102**, including all five M8 tests. M4.royal_mesh_collision could not run; M5.actor_assets and M5.attribute_authoring timed out at 90 seconds, then passed directly in 133.7 and 157.5 seconds. NativeAuthoringTests passed in CTest in 1117.51 seconds. The full unchanged verify_effect_reaction.py passed **all 67 gates** in 1627.0 seconds. The two-player and transition-loading captures were inspected. Source/binary hashes, counts and limits are in [the receipt](evidence/m8-1b-bootstrap.json); raw build, CTest, focused tests, direct authoring tests and editor output are in evidence/m8-1b-bootstrap-*.log. Stage 1b remains in progress. These tests use loopback in one process, not separate-process or multiplayer qualification. The next work after verification is --join client-only editor integration over GNS, the Players child launcher and two-process gates, before stage 2.

### Editor network integration refinements (design; not built)

CharacterSceneSession retains its strict constructor for the existing headless and editor gates. Its independent host composition will own one local SceneClient and accept remote SceneSeats on external gameplay transports; the guest composition will own only SceneClient. Guest presentation, actor lists and prepared actor descriptions come from replicated client state. Authority-only world events, controllers and save/capture controls remain host controls. Connection admission and scene readiness are polled across frames so the loading screen remains responsive.

The persistent local profile has a separate local-player slot for launched children, giving distinct identities on one machine. The network seat is an assignment for this session, not the persistent inventory key. The launcher passes the local slot explicitly, while a manually joined guest uses its ordinary local profile. Profiles continue to map to the current scene's authored/runtime avatar IDs.

The user's stage-1b decision explicitly requires simultaneous child editor windows belonging to one Play. The existing one-build/editor-at-a-time rule continues to serialize builds and unrelated editor/verification runs; one multiplayer test party necessarily contains its host and owned guests. Stop closes that party's children before another party starts. Two-process gates will be reported as same-machine UDP evidence; they do not qualify two-machine play or EOS.
