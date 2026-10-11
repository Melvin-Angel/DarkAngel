# Combat sandbox plan

User decisions of 11 October 2026 for M6 and the remaining M5 content, given in answer to seven questions. They supersede the ordered lists in [CLAUDE_AUTONOMOUS_PROMPT.md](CLAUDE_AUTONOMOUS_PROMPT.md) where they differ. Goal in the user's words: a reliable, extensible combat sandbox rather than unrelated systems.

## Decisions

1. **NPC spawn points.** Reusable spawn point scene objects with position, rotation and an NPC definition reference. Designed so respawn policies and encounter ownership can be added later. The old single combat-target setting is removed once the new system works.
2. **Multiple NPCs.** Proper multi-entity combat now, on stable entity ids, not the single-target implementation extended with workarounds. Soft targeting, explicit lock-on and HUD target selection stay three separate concepts.
3. **Cells and world ledger.** Deferred until NPC and combat functionality is more mature.
4. **Vertical aiming.** Full 3D camera-based aiming: resolve a world-space aim point from the crosshair, then aim projectiles from their configured spawn position toward that point.
5. **Projectiles.** Real travelling projectiles with visible geometry, configurable speed, gravity and lifetime, and swept collision detection. Simulation and hit resolution structured for eventual server-authoritative multiplayer.
6. **Soft lock.** Light and Heavy both use shared, configurable targeting logic. NPCs use their AI-selected targets while sharing the underlying combat targeting and hit detection.
7. **Mask editor.** No top-level Mask tab. A reusable, dockable Mask Editor window opened from both the Player and NPC editors. A later Items tab (consumables, world keys, masks, other item types) opens the same window for a mask. Mask definitions stay separate from inventory instances so inventory, equipment, upgrades and persistence can follow without redesigning authoring.

## Principles

- Clean, reusable native C++ systems over temporary workarounds; replace unnecessary legacy architecture where appropriate.
- Runtime systems independent of editor UI.
- No duplicated asset editing logic across workspaces.
- A clear separation between asset definitions, scene instances and runtime entities.
- Design multiplayer authority boundaries now without building the whole networking stack.
- Functionality first; a whole-editor UI/UX overhaul comes later.

## Order

1. Multi-entity combat and NPC spawn points.
2. Full 3D targeting and shared soft lock.
3. Travelling projectile foundation.
4. Reusable Mask Editor window, integrated with the Player and NPC editors.

Compile and validate each stage before the next. Each stage is one chunk with one full verification pass (the user's testing rule).

## Stage 1 design: multi-entity combat and spawn points

Written before any code. Items marked "verify" are assumptions to check against the code first.

### What is single-target today

`CharacterSceneSession` (engine/runtime/character_scene.cpp) is built around exactly two actors. It holds `host_motor`, `owner_motor` and one `target_motor`; `ability_owner` and one `target_owner`; one `controller`; one `attacker_receipt`; `target_pose`. `CharacterSceneCombat` carries one `target` identity, one optional `npc`, one `controller`, `target_attributes` and the scripted `attacker`. Observers and effect views are already keyed by network id in maps, and `WorldSession` itself (ability state, motors, effects, public snapshots) is per-actor and has no two-actor limit. So the limit is in the scene composition and in everything that consumes it: Play preparation (`load_character_preview`, `prepare_character_preview`), the editor's HUD feed and Gameplay Inspector, the soft-lock candidate list, and the tests.

### Runtime shape

- **`CharacterSceneActor`** replaces the target fields: stable scene identity (`StableId`), kit, input semantics, abilities, resolved attributes, display name, faction, generation, an optional controller, and the spawn transform. `CharacterSceneCombat::actors` is a bounded list (start with 16; built as 16 once the session capacity was lifted, see TARGETING_REPORT.md). The player stays a distinct role because it alone is predicted by this client; everything else is a server-driven actor. The scripted `attacker` shorthand and `target_attributes` go away; a training dummy is an actor with no controller.
- **Session internals** become a per-actor record keyed by network id: motor, ability owner handle, controller, latest receipt, observed pose. The step loop iterates actors in a fixed order (ascending network id) for controller decisions, motor steps, publication and hit resolution, so a run is deterministic. Per-actor work keeps the existing bounds; the snapshot wait loop already scales with the public map.
- **Controller view** gains a bounded list of other actors (identity, faction, motor state, alive) instead of one `opponent`. `ControllerCommand` gains an optional chosen target identity so an NPC's AI-selected target is explicit state the shared targeting logic can read (decision 6). Existing leaves pick the nearest live hostile until sensing exists; "hostile" is a faction comparison, which needs the smallest possible faction rule: actors with different factions are hostile, the player has faction `player`.
- **Three separate target concepts (decision 2).** (a) Soft-lock candidates: the targeting module's candidate list becomes every live hostile actor, which `select_soft_lock` already handles and already tests for ordering. (b) Explicit lock-on: not built in this stage; reserve `lock.on` (already an input action) and keep it out of soft lock. (c) HUD target: a presentation choice in the client, initially the soft-locked actor if any, else the nearest live hostile in front of the camera, else none. None of the three writes gameplay state.
- **Session accessors** take an actor identity where they took the target implicitly: `attacker_receipt(StableId)`, `controller_status(StableId)`, plus `actors()` to enumerate. `pose(StableId)`, `observer(StableId)`, `reaction(StableId)`, `death(StableId)` and `effects(StableId)` already do.

### Spawn points

- **Definition, instance, entity (principle).** The NPC definition is the asset (`.danpc`). The spawn point is the scene instance. The spawned actor is the runtime entity with a network id.
- **Scene object.** A spawn point is a scene object with a transform and a `spawn` component: `{"npc": "<.danpc id>"}` plus reserved, validated-but-unused fields for later (`respawn`: `"never"` only for now; `encounter`: absent). Verify how `engine/runtime/assembly.cpp` turns a `.dascene` placement into `SpawnPlan` (it already carries `origins`, `bindings` and per-object model assets) and add spawn points the same way: `SpawnPlan::spawns` as object identity to NPC asset id. The NPC asset joins the scene's dependency closure so Save/cook pins it.
- **Stable entity ids.** A spawned actor's identity is derived from the spawn point's `StableId`, so the same spawn point yields the same actor identity every Play and across client and server. Verify what `StableId` is composed of ({9,1} style pairs in tests) and whether a derived id needs a reserved namespace.
- **Visual.** The scene must show something at a spawn point in the Scene tab; the NPC's Character skin is the obvious preview. Verify whether the scene viewport can draw a skinned model that is not a placed model object; if not, a simple marker is enough for this stage.
- **Play preparation.** `load_character_preview` loads every spawn point's NPC (kit, attributes, behaviour, tuning), unions their abilities, effects and clips as it does for one NPC today, and builds the actor list. The skin check becomes per actor; several Characters with different skins in one scene need several rigs, which the session does not support today (one `rig`, one `presentation_plan`). For this stage require all actors to share the player's rig and say so; multi-rig is its own step.
- **Removal of the legacy path.** `--combat-target`, `Controller::authoring_npc`, `--target-npc`, "Place on the combat target", `--target-attacks` and `--target-navigates` are removed once spawn points work. The Royal scene gets a spawn point for the Royal Guard. Tests and gates that used the single target move to scenes with one spawn point, then gain multi-actor cases.

### Authority boundaries (principle)

Controllers, spawning, hit resolution and target selection for NPCs are server-only and stay behind `WorldSession`'s authority checks. The client receives actors only as replicated objects, public ability snapshots and public effect frames, which is already how it sees the single target. Nothing in the client may create an actor or choose an NPC's target. The spawn list is part of the session fingerprint so a client cannot join with a different composition.

### Validation for the stage

One chunk, one pass. Focused test while implementing: `CharacterCombatSceneTests` with two and three NPCs (each with its own Health and controller; the player kills one and the others keep acting; soft lock picks the nearer of two; two NPCs attack on their own schedules; determinism of actor order). Then the full gate set, CTest, and one Play capture of the Royal scene with two Royal Guards from spawn points.

### Risks

- This touches the same file every gate depends on. Expect most existing tests and exercises to need mechanical updates (a single spawned actor instead of `target`). Do the runtime change first with the old two-actor tests still passing through a one-actor list, then remove the legacy surface.
- HUD and editor exercises read "the target"; they move to the HUD target concept above.
