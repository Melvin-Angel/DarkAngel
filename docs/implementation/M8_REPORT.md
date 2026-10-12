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
