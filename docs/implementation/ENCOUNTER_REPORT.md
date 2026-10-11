# Encounters and world events

User request of 11 October 2026 (evening): encounters are the first new placeable kind. They are triggered through world events and can emit world events and tags; enemies appear at placed spawn points or at random points inside the encounter's bounds; an encounter is wave based (enemies per wave) or time based (time between spawns and a cap on enemies alive); bounds are a series of placed box volumes.

This file holds the design, written before the code, then the report of what was built and verified.

## Design

### What exists today, and what is missing

- A Play session creates every actor when it starts: `CharacterSceneSession` builds one actor per scene spawn point in its constructor (motor, ability owner, team, observers, pose). Nothing can appear during play.
- There are no world events. Tags exist per actor (`TagDictionary`, ability and effect tags); there is no session-level tag or event.
- A spawn point's `spawn` component already reserves `encounter` (must be null today) and `respawn` (must be `never`).
- A scene holds the player and at most sixteen other characters (`session_character_capacity`).

So encounters need three new things: world events and tags, an encounter definition with its volumes and spawn points in the scene, and actors that enter a running session.

### World events and world tags

`engine/runtime/world_events.cpp`, server-only, no editor or asset dependency.

- A **world event** is a name (lowercase letters, digits, `_` and `.`, up to 48 characters) emitted at a tick. Events emitted during tick T are delivered to every listener at tick T+1, in the order they were emitted. At most 64 a tick; more is an error, not a silent drop.
- A **world tag** is a name in a session-level set: set, cleared, queried. It is state, where an event is a moment.
- Built-in event: `play.started`, emitted on the first tick.
- The session exposes `emit_world_event(name)` for the host (game code, the editor's debug field), the recent event log and the tag set. Nothing is replicated to clients in this stage: events and tags decide server behaviour only. A client that needs to show them (music, a banner) gets them through a later replicated channel.
- Scripts and abilities emitting or listening to world events are not part of this stage; the seam is the two calls above.

### Encounter in the scene: definition, instance, entity

An encounter is authored in the scene, like a spawn point, because its bounds and spawn points are places. Its enemies are NPC definitions (assets). The spawned enemies are runtime actors.

- **Encounter object**: a scene object with an `encounter` component.

```json
"encounter": {
  "trigger": {"event": "play.started"},
  "mode": "waves",
  "waves": [{"npc": "<.danpc id>", "count": 3}, {"npc": "<.danpc id>", "count": 4}],
  "placement": "points",
  "emits": {"started": "gate.fight_started", "wave_cleared": "gate.wave_cleared", "completed": "gate.cleared"},
  "tag": "gate.fight_active"
}
```

or, time based:

```json
  "mode": "timed",
  "timed": {"npc": "<.danpc id>", "interval_seconds": 4.0, "max_alive": 3, "total": 10}
```

- `trigger`: `{"event": name}` starts the encounter when that world event is delivered, or `{"enter": true}` starts it when the player first stands inside its bounds. An encounter starts once.
- `waves`: one to eight waves; a wave spawns `count` enemies of one NPC definition at once; the next wave starts when every enemy of the current one is dead.
- `timed`: one enemy every `interval_seconds` while fewer than `max_alive` are alive, until `total` have been spawned; complete when all are dead.
- `placement`: `points` uses the spawn points that name this encounter, in order, round robin; `random` picks a point inside the bounds.
- `emits` (each optional): `started`, `wave_cleared` (after each wave but the last), `completed`. `tag` (optional) is a world tag held from start to completion.
- **Encounter volume**: a scene object with `"volume": {"encounter": "<encounter object>", "half": [x, y, z]}`. The box is centred on the object, turned by its yaw. An encounter's bounds are the union of its volumes. Volumes are ordinary scene objects, so they are placed, moved and deleted like any other.
- **Owned spawn point**: a spawn point whose `spawn.encounter` names an encounter. It no longer spawns at Play start; the encounter uses its position and facing. Its own `npc` is ignored while owned (the wave decides who appears).
- **Trigger volume** (user addition of 11 October 2026): a scene object with `"trigger": {"event": "gate.opened", "half": [x, y, z], "once": true}`. It emits its world event on the tick the player is first found inside its box; with `once` false it emits again on every later entry. A trigger volume is independent of encounters: its event can start an encounter (or several), and later anything else that listens to world events. A dead player does not trigger it. NPCs do not trigger it in this stage.
- Random placement: a deterministic generator seeded from the encounter's identity picks a volume (weighted by its area), a point inside it, drops it onto the static collision below, and retries (up to eight times) if there is no ground or the point is within 1 m of a living character.

### Runtime

- `engine/runtime/encounter.cpp`: `EncounterDefinition` (decoded and validated from the component, shared with the scene assembler) and `EncounterDirector`, pure logic with no physics: each tick it is given the delivered events, the player's position and how many of its enemies are alive, and it returns spawn requests (NPC, position, facing), events to emit and its tag state. This is what the unit tests drive.
- `CharacterSceneSession` owns the world events and one director per encounter, runs them once a tick on the server before sensing, and carries out spawn requests.
- **Actors that enter during play.** The per-actor setup in the session's constructor moves into one function that can also run later: create the replicated object, the authoritative motor, the ability owner with the NPC's kit and attributes, its team, its senses and controller; the client side (observer, effect view, pose) is prepared when the actor's first public frame arrives. The collision stream already carries a variable list of characters each frame, and the world's own replication already carries new objects.
- An enemy's identity is derived from the encounter's identity and its spawn index, so the same encounter yields the same actors every Play.
- **Capacity in this stage.** Dead enemies stay where they fall and keep their place in the session. So spawn-point actors plus every enemy an encounter will ever spawn must fit the sixteen actors of a scene; Play preparation refuses a scene that asks for more and says by how much. Removing the dead to free their places (and so allowing long encounters) is the next stage, with the question below.
- Authority: directors, events, tags and spawning are server-only. A client sees enemies only as replicated actors.

### Editor

- Place panel: a "Gameplay" category with Encounter and Encounter volume. A new volume belongs to the selected encounter.
- Inspector: an Encounter section (trigger, mode, waves or timed settings, placement, emitted events, tag), an Encounter volume section (half extents, owner), and an owner choice on spawn points.
- Scene viewport: the selected encounter's volumes as wire boxes, with lines to its spawn points.
- Gameplay Inspector: a World tab with the event log, the tag set, each encounter's state (waiting, wave n of m, alive, complete) and a field to emit an event by hand.

### Tests

- `EncounterTests` (new, no session): event delivery order and bounds; a wave encounter (start on event, counts, next wave only after the last death, emitted events and tag); a timed encounter (interval, cap on alive, total); the enter trigger; random points inside the union of volumes and never outside; decoding and rejection.
- `CharacterCombatSceneTests`: an encounter in a real session: enemies enter during play at spawn points and at random points, are replicated, sense and attack through the ordinary paths; the player kills a wave and the next one comes; determinism of two runs; the capacity refusal.
- Editor gate: a scene with an encounter triggered by the player entering its bounds, with a capture of the volumes in the Scene tab and of the second wave in Play.

### Order of work

1. World events, the encounter definition and director with `EncounterTests`.
2. Actors entering a running session, and encounters in the session with the scene tests.
3. Scene components, Play preparation and the editor (placing, Inspector, drawing, World tab), then the gate.

### Questions for the user (not blocking)

1. Should dead enemies disappear after a while? It decides whether an encounter can spawn more than fits the sixteen-actor scene in total. Built now: they stay.
2. Should world events and tags reach clients (for music, banners, doors that open on every screen)? Built now: server only.
3. Answered on 11 October 2026: trigger volumes the player walks into emit world events. Still open: should an NPC's death, an ability or a script emit them too, and should NPCs be able to set off a trigger volume?

## Report: what was built (11 October 2026)

The three steps of the design are built. Differences from the design are listed first.

### Differences from the design

- **Trigger volumes** were added at the user's request while the first step was being written; they are in the design above.
- **The ground query** takes the height to look down from (the top of the volume), so a volume inside a building finds its own floor and not the roof.
- **Placing an encounter places its first volume too** (5 x 2 x 5 m half extents, around it), so a new encounter has bounds to enter and to spawn in. New encounters start on "player enters its bounds" with one wave of two of the first NPC definition in the project.
- **Orphans are tolerated**: deleting an encounter leaves its volumes and owned spawn points in the scene; the Inspector says so and Play ignores them. The scene still loads.
- **Picking volumes in the viewport**: a volume is selected by a click only where no solid object smaller than it is under the cursor, so objects inside a volume stay clickable.

### Where things are

- `engine/runtime/encounter.cpp`, `darkangel/encounter.hpp`, `darkangel/world_events.hpp`: `WorldEvents`, `TriggerDefinition`/`WorldTrigger`, `EncounterDefinition`, `EncounterDirector`, their decoders.
- `engine/runtime/character_scene.cpp`: `arm` (server side of one actor), `present` (client side, once its first public frame has arrived), `spawn` (an enemy enters: replicated object, team, `arm`), `run_world` (events delivered, triggers and directors stepped, before sensing). `CharacterSceneCombat::encounters` and `triggers`; session accessors `emit_world_event`, `world_events`, `encounters`, `encounter`, `actor_encounter`, `trigger_fired`, `actor`.
- `engine/runtime/assembly.cpp`: scene components `encounter`, `volume`, `trigger` and `spawn.encounter`; `SpawnPlan::encounters`, `volumes`, `triggers`, `spawn_owners`, serialised under `gameplay` and so part of the plan fingerprint. `encounter_actor` derives an enemy's identity.
- `apps/editor/character_preview.cpp`: every NPC definition an encounter can spawn is prepared like a spawn point's NPC (`encounter_npcs`); `prepare_character_preview` builds the session's encounters and triggers from the plan and the scene's transforms.
- Editor placement (decided here, as the user asked): encounters, their volumes and trigger volumes are scene objects, so they are placed from the Scene tab's **Place panel** (a "Gameplay" category) and edited in the **Inspector** (sections Encounter, Encounter volume, Trigger volume, and an Encounter choice on spawn points). They are places in a level, not assets, so they got no tab and no Tools window. Runtime state is a **World tab in the Gameplay Inspector** (Game tab): world tags, each encounter's state and enemies, trigger counts, the event log, and a field to emit an event by hand.

### Verified

- `EncounterTests` (new, CTest `M6.encounters`): trigger volumes (entry, once and repeating, dead player), world events (next-tick delivery, order, bound, tags), decoding and rejection, waves at spawn points (trigger, next wave only when the last is dead, emitted events, tag, completion, never twice), timed spawning (interval, cap on alive, total), the enter trigger, random points inside turned volumes (on the ground, apart, repeatable per seed).
- `CharacterCombatSceneTests`, block "Encounters": a session that starts with the player alone; a world event starts a wave encounter and two enemies enter at its spawn points, replicated on the current tick with their own Health and derived identities; the player's ordinary attacks kill them and the second wave enters only then; the events arrive in order (`play.started test.go test.started test.wave_cleared test.done`) and the tag is set and cleared. A trigger volume the player walks into starts a timed encounter with random placement whose enemies, with controllers from the host's factory, sense and attack the player; it holds to two alive; two runs are identical. A scene asking for seventeen actors is refused.
- Editor gates: `encounter-scene-d3d12` (the scene's encounter selected, its volumes drawn; an encounter with its first volume, a second volume and a trigger volume placed through the Place panel's command, Undo and Redo) and `encounter-d3d12` (in Play the player walks into the trigger volume: fired once, two Royal Guards entered, events `play.started, gate.entered, gate.fight_started`). Both captures were looked at: the volumes, the trigger volume and the encounter markers are drawn in the Scene tab; in Play the volume is drawn, guards are in the scene and one has hit the player (75/100).
- Full pass: All 46 verify gates pass. CTest with only the five known unrelated failures was run on the build before two editor-only fixes (a File menu brace error from the Save All change, and a dangling reference in the editor's controller factory that crashed Play when an encounter asked for a controller); CTest was not run again after them.

### Known limits and things found

- **Each wave's entry rebuilds the owner's prediction world once** (`resynchronizations` rose by 2 for two waves in the scene test). It did not disturb prediction in the tests (no pending input, the player kept moving), but it is a full rebuild and may be a visible hitch in a large scene. Adding a character without a new prediction baseline is the fix; not done.
- Dead enemies stay, so a scene's spawn points and encounters together bring in at most sixteen actors. Long encounters need the dead removed (question 1 below).
- Enemies an encounter spawns are not in the Gameplay Inspector's Actors tab (it lists the scene's spawn points) and never become the HUD target; the World tab lists them with their Health.
- Only the player sets off trigger volumes and the enter trigger. World events and tags are not replicated.
- The Inspector's Encounter, Encounter volume and Trigger volume sections were not captured or clicked; their edits go through one function (`set_component`) that the placement gate also uses for nothing else, so the forms themselves are unverified.
- `investigate` and encounters were not combined, and no NPC death emits a world event.

### Questions for the user (not blocking)

1. Should dead enemies disappear after a while? It decides whether an encounter can spawn more than fits the sixteen-actor scene in total.
2. Should world events and tags reach clients (music, banners, doors that open on every screen)?
3. Should an NPC's death, an ability or a script emit world events too, and should NPCs be able to set off a trigger volume?
