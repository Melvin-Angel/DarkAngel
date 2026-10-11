# NPC sensor system

User request of 11 October 2026: one general system for every NPC that decides how it detects the player and other actors. This file holds the design, written before the code, and then the report of what was built and verified.

## Open questions for the user

Asked on 11 October 2026, not answered yet. The build below is the neutral baseline for each, chosen so that either answer is an addition and not a rewrite.

1. Is detection instant, or does it build up over time (an awareness meter)? Baseline: instant.
2. Should crouching, distance or lighting reduce how easily the player is seen? Baseline: no modifiers.
3. Do NPCs of one faction share what they detect? Baseline: no sharing.
4. What does an NPC do at a last known position: go and look, or give up? Baseline: the lost target and its last known position are remembered and shown, and no behaviour acts on them (the NPC goes idle).

## Design

### What exists today

There is no perception. `CharacterSceneSession::drive_controller` hands each controller a `ControllerView` whose `others` lists every actor in the session, and controllers pick with `nearest_hostile`, which therefore sees every live hostile anywhere, through walls. NPC ranged aim (`aim_actor`) points at the chosen target without any line of sight test.

### Definition, instance, entity

- **Definition.** Senses are part of the NPC definition (`.danpc`), an optional `senses` object next to `tuning`. They are edited in the NPC tab as ordinary draft fields, validated by the same decoder the cooker uses, saved with the coordinated Save and frozen into `npc.json`. The NPC's generation covers them, so they are part of the session fingerprint.
- **Entity.** Each spawned actor owns one `Perception` (runtime state) built from its definition's senses. A scene spawn point adds nothing: per-instance sensor overrides are not built.

### Authored shape

```json
"senses": {
  "eye_height": 1.6,
  "forget_seconds": 5.0,
  "sensors": [
    {"type": "distance", "radius": 4.0},
    {"type": "vision", "range": 18.0, "horizontal": 60.0, "vertical": 35.0},
    {"type": "line_of_sight", "range": 30.0}
  ]
}
```

or the explicit setting `"senses": {"everything": true}`.

- `distance`: detects an actor whose position is within `radius` metres of the NPC's position, in any direction, through walls (hearing a step, feeling a presence).
- `vision`: a cone from the eye along the actor's facing (its yaw; the cone turns with the actor). It detects an actor whose body point is within `range`, within `horizontal` degrees of the facing to either side, within `vertical` degrees above or below level, and to which the eye has line of sight.
- `line_of_sight`: detects an actor within `range` in any direction to which the eye has line of sight.
- Line of sight is one rule used by both: a ray against static collision from the NPC's eye (its position raised by `eye_height`) to the target's body point (its position raised by 1 m, the same point NPC aim uses). Other characters do not block it.
- An actor is detected when any sensor detects it. Zero to eight sensors per NPC.
- `eye_height` is one value per NPC, not per cone, so that an NPC casts at most one ray per other actor per tick whatever its sensors are.
- `forget_seconds` (0 to 120): how long a lost actor is remembered.
- `everything`: every actor in the session is detected every tick. It exists so test controllers and simple content can keep the old behaviour by saying so. It cannot be combined with sensors.
- No `senses` at all: the NPC perceives nothing and its controller never finds a target.

Later kinds (hearing, damage taken, an ally's alert) fit as further entries of `sensors` or as events fed into the same `Perception`: the result type does not care what detected an actor. Hearing and alerts need a stimulus (a noise event, a hit, a message), so they will add an input to `Perception::sense`, not a new result.

### Runtime module

New `engine/runtime/perception.cpp` with `darkangel/perception.hpp`, in the runtime library, with no editor or asset dependency:

- `SensorDefinition`, `SensesDefinition`, `validate_senses`.
- `PerceivedActor`: identity, session faction, the motor state when last detected, alive, `detected` (this tick), `visible` (line of sight this tick), `since` (tick of the current detection, or of the last one once lost) and `seen` (last tick it was detected).
- `Perception::sense(tick, self, alive, subjects, sight)`: one pass over the other actors in the order given. `sight` is a callable the session supplies; the module does no physics itself, which keeps it testable and keeps the ray on authoritative collision.
- Result: `Perception::actors()`, in subject order for current subjects, each lost actor kept in place with its last known state until `forget_seconds` has passed.

Work bound per NPC per tick: one pass over at most sixteen other actors, at most eight cheap sensor tests each, and at most one ray per other actor. A full scene is at most 16 x 16 rays a tick.

`visible` is computed for every detected actor, whichever sensor detected it, so ranged aim has a definite answer.

### Where it runs

Server only, inside `CharacterSceneSession::step`, once per tick, before controllers decide, for all actors in ascending network identity, from the authoritative state as it stood before the tick (the same state controllers have always been shown). The subjects are the authoritative motors and Health of the player and the other actors; nothing from the client is read. A dead NPC perceives nothing.

### What controllers see

- `ControllerView::others` is replaced by `ControllerView::perceived` (a span of `PerceivedActor`). A controller cannot learn about an actor its NPC does not perceive.
- `nearest_hostile` considers only actors that are detected this tick, alive and of another faction. Faction still decides who is hostile; sensors decide who is noticed.
- The behaviour tree's `opponent_alive`, `in_range`, `face` and `approach` read that opponent, so they read perception. A new condition `opponent_visible` succeeds when the opponent is detected with line of sight.
- `ControllerCommand::target` must name an actor that is detected this tick.
- Remembered (lost) actors are in the view with `detected` false and their last known state. No shipped controller acts on them (question 4).

### Ranged aim

An NPC's press of an aimed ability is only passed to the ability system when its chosen target is detected with line of sight this tick; otherwise the press is dropped before it becomes a request. This is enforced in the session, not left to the tree, and replaces the separate line of sight item for the archer. The owned Archer Ranged tree also gains `opponent_visible` so that an archer without a clear shot walks toward its target instead of standing.

### Owned content and tests

- Royal Guard and Royal Archer get real sensors (a vision cone and a short distance radius).
- Test controllers that are not about sensing set `everything` on their actor.

### Debug view

- `CharacterSceneSession::perception(StableId)` exposes an actor's senses and current perceived list to the host process, like `controller_status`. Presentation only.
- Scene and Game viewports draw the selected NPC's radii and cones from its senses at its current transform (a selected spawn point in the Scene tab; the actor chosen in the Gameplay Inspector in Play).
- Gameplay Inspector, Actors tab: each actor lists what it perceives (name, detected or lost and for how long, line of sight, distance).

### Tests

Scene tests in `CharacterCombatSceneTests` through the real session and authoritative collision: inside and outside a distance radius; inside and outside a cone horizontally and by height; behind a wall; losing a target and forgetting it; an NPC with no sensors staying idle; the same perception whatever order the actors are listed in; an archer that does not shoot through a wall. Then one editor gate: a guard that only reacts once the player walks into its cone, with a capture of the debug view.

## Report: what was built (11 October 2026)

Everything in the design above was built as designed, with these notes.

- **Module.** `engine/runtime/perception.cpp` and `darkangel/perception.hpp`. `Perception::sense` takes the subjects and a sight callable; the session passes a ray against `StaticCollision` in the authoritative physics world.
- **Session.** `CharacterSceneSession::step` senses for every actor, in ascending network identity, immediately before the controllers decide. `CharacterSceneActor::senses` carries the definition; `CharacterSceneSession::perception(StableId)` exposes the result to the host process.
- **Controllers.** `ControllerActor` and `ControllerView::others` are gone; `ControllerView::perceived` is a span of `PerceivedActor`. `nearest_hostile` reads only actors detected on the tick. The scripted attacker, the path follower and the behaviour tree all go through it, so none of them needed its own change.
- **Behaviour trees.** New condition `opponent_visible`. `approach` now walks until the opponent is within range and in line of sight (it used to stop on range alone), so an archer with a wall in the way walks on along the navigation mesh until it has a shot. Both are in the NPC tab's tree outline.
- **Ranged aim.** In `drive_controller`, a press of an aimed ability at a named target without line of sight is dropped before it becomes an ability request. A press with no named target is unchanged (it fires level, as before).
- **Asset.** `senses` on `.danpc`, decoded by `npc_senses` in `engine/assets/actor_cooker.cpp` for source validation and for the frozen product; `decode_npc_senses` exposes the same decoder to the editor.
- **NPC tab.** A Senses section under Behaviour: a choice of None, Sensors or Sees everything; eye height; time to forget; a list of sensors with their fields, Remove, and three Add buttons. Edits are ordinary draft edits.
- **Owned content.** Royal Guard: vision cone 18 m, 70 degrees to either side, 40 up and down, plus a 5 m distance sensor; forgets after 5 s. Royal Archer: the same with a 28 m cone. Archer Ranged tree: `opponent_visible` before the attack. The Training Dummy has no senses and no behaviour.
- **Debug view.** `Shell::debug_overlay` is drawn over the Scene and Game viewports. Scene tab: the selected spawn point's NPC senses from its draft (green cone, amber distance ring, blue line of sight ring). Play: each actor ticked "Draw senses in the viewports" in the Gameplay Inspector's Actors tab, or whose spawn point is selected; the cone turns red while the actor detects somebody, and a line runs from its eye to each perceived actor (red with line of sight, orange without, grey to a last known position). The Actors tab lists each actor's senses and what it perceives, with distance and times.

### Behaviour change to know about

The owned Royal Guard and Royal Archer no longer know where the player is. A guard facing away from a player more than 5 m off does nothing until the player comes into its cone or radius, and it stops chasing as soon as it loses the player (question 4). In the user's own `RoyalCombat.dascene` the three guards face away from the player's start about 17 m off, so they now stand still until approached. The gate that used that scene (`npc-spawn-points-d3d12`) and the Scene tab capture now use a two-guard scene derived in the fixture (`RoyalGuards.dascene`), as the user asked for gates with a fixed layout.

### Verified

- `CharacterCombatSceneTests`, new block "NPC sensing", through the real session and authoritative collision: a distance sensor detects at 3.5 m and not at 4.5 m with a 4 m radius, and through a wall; a vision cone sees ahead and not behind, respects its horizontal half angle (34 degrees off is seen, 57 is not, with 45), its range, and its vertical half angle (a player standing 3 m higher is seen with 30 degrees and not with 15); a wall blocks the cone; a line of sight sensor works facing away, is blocked by a wall and respects range; a watcher that turns its back at tick 10 keeps the player as a lost actor at its last known position, forgets it after the authored second, and detects it anew at tick 101 when it turns back; an actor without senses does not move, target or attack in 240 ticks while the same actor with senses hits the player; perception is identical whichever order the actors are listed in and is in ascending network identity; an archer shoots the player across open ground and never presses with a wall in the way.
- `BehaviorTreeTests`: a remembered but undetected opponent is not an opponent; `opponent_visible`; `approach` keeps walking in range of an opponent it cannot see.
- `NativeAuthoringTests`: senses out of bounds are rejected by validation; a senses edit undoes and redoes; the frozen NPC product carries them.
- Full pass on this build: `python scripts/verify_effect_reaction.py` (41 gates) and CTest with only the five known unrelated failures.
- Editor gate `npc-senses-d3d12`: a Royal Guard 6 m to the player's side, facing the way the player will walk. It has no target for the first 60 frames, first has one at tick 92 with the player at z 5.29 (the cone's edge is at z 5.18), then walks over and hits the player twice (Health 50 at tick 330). The capture was looked at (at frame 150, before the frame count was raised): the cone is drawn red, the distance ring amber, and a red line runs from the guard's eye to the player.
- Editor gate `spawn-points-scene-d3d12` now selects a spawn point and requires its senses to be drawn.

### Not verified

- The Senses section of the NPC form was not clicked; its edits go through the same draft path as tuning. The `npc-form-d3d12` capture was looked at later the same day: the section shows the Royal Guard's eye height, time to forget and vision cone; three labels were cut off at the panel edge and were shortened (editor rebuilt and re-captured, no other gate re-run); the half angle labels still lose their last characters at this width, left for the UI pass.
- The Scene tab capture of the selected spawn point (`spawn-points-scene-d3d12`) was looked at later the same day: the green cone and the amber distance ring are drawn at the selected guard, facing the player.
- The Actors tab perception readout was not captured.
- Sensing was measured for correctness, not for cost; a full scene's 256 rays a tick were not profiled.
- Real latency and GNS: unchanged, sensing is server-only and adds nothing to the wire.

## User answers and the awareness meter (11 October 2026, evening)

Answers to the four questions at the top of this file: (1) build an awareness meter, configurable in the editor, set low for now; (2) no stealth modifiers; (3) no sharing between NPCs of a faction; (4) support "go and look" at a last known position, but give up for now.

### Built

- **Awareness meter.** `awareness_seconds` on an NPC's senses (0 to 30; "Awareness time (s)" in the NPC tab's Senses section). A sensed actor's meter fills over that time and drains at the same rate while no sensor senses it. The actor is detected once it is sensed with a full meter, and then stays detected for as long as it is sensed. 0 detects at once, and "sees everything" ignores the meter. The Royal Guard and Royal Archer are set to 0.3 s.
- `PerceivedActor` gained `sensed` and `awareness`. An actor that is sensed but not yet detected is in the perceived list so debug views can show the meter, but `nearest_hostile` and a controller's target still require `detected`, so nothing acts on it. A remembered (lost) actor's last known state is not refreshed by being sensed again before the meter refills.
- **Go and look.** A behaviour tree action `investigate` ("Go to last known position" in the tree outline): walk, along the navigation mesh when there is one, to where the hostile the NPC lost most recently was last detected; succeed on arrival; fail when the NPC remembers none. `last_lost_hostile` is the shared helper. No owned tree uses the node, so the Royal Guard and Archer still give up when they lose the player. Turning it on for an NPC is a tree edit, for example a third branch `sequence[investigate]` after the attack and approach branches.
- Debug: the Actors tab shows "Becoming aware of ...: n%" and the meter of a lost actor; the viewport line to an actor the NPC is becoming aware of is yellow and thickens as the meter fills.

### Verified

- `CharacterCombatSceneTests`: with half a second to fill, a watcher facing the player senses it at once but has no target at tick 20 (meter between 50% and 80%), and detects it between ticks 29 and 32; a watcher that turns away after 15 ticks never detects the player, its meter peaks below full and drains until the entry is gone.
- `BehaviorTreeTests`: `investigate` walks toward a lost hostile's last known position, stops within a metre of it, and does not move for an actor the NPC never detected.
- `NativeAuthoringTests`: the frozen NPC carries its awareness time.
- Full pass on this build: all 44 verify gates and CTest with only the five known unrelated failures. In `npc-senses-d3d12` the guard now notices the player at tick 109 (tick 92 before the meter), with the player at z 6.71.

### Not verified

- `investigate` in a real session or in the editor: it is covered by the tree's unit test only, and no owned NPC uses it.
- No capture of the meter readout or the yellow line was looked at.
