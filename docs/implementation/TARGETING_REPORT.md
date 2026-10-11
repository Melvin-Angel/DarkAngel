# Player targeting module

Track A item 1 of [CLAUDE_AUTONOMOUS_PROMPT.md](CLAUDE_AUTONOMOUS_PROMPT.md). Design written 11 October 2026 before any code; the slice receipts follow it.

## Design

One module, three modes, authored per Ability. Slices land in this order: melee soft lock, crosshair aim, ground-snapped AOE aim.

### Where it lives on the Ability asset

An optional `targeting` object on `.daability` (absent = the ability targets nothing, which is every ability before this work). `mode` selects the variant and the remaining fields belong to that mode; the cooker rejects unknown modes and unknown fields.

```json
"targeting": {"mode": "soft_lock", "range": 4.5, "cone_degrees": 75, "stop": 1.2, "distance": 2.0, "begin": 0, "end": 16384}
```

- `soft_lock` (slice 1, implemented): `range` and `cone_degrees` (half angle) gate which target is acceptable; `begin`/`end` are the action-clock window of the warp; `distance` is the most the actor may travel toward the target over the whole window; `stop` is the centre distance it never closes inside.
- `aim` (slice 2, designed): `range` (maximum ray length). The ability takes its direction from the aim ray.
- `ground` (slice 3, designed): `range` (maximum distance from the actor to the aim point) and `radius` (marker and effect radius).

The frozen runtime form is `AbilityDefinition::soft_lock` (`AbilitySoftLock`); `aim`/`ground` will add sibling optionals. The targeting object is part of the ability source, so the ability generation hash covers it.

### What the intent carries on the wire

- Soft lock: nothing new. The inputs of the choice (movement input, yaw, the actor's own motor state) already travel in `MotorInput`, and the candidates are server state. The server never trusts a client-chosen melee target.
- Aim and ground (slices 2 and 3): `AbilityIntent` gains one bounded aim payload, a unit direction for `aim` or a world point for `ground`, quantised, present only when the ability's mode asks for it. This is a wire change (`ability_wire.cpp`, protocol bump) and is deliberately not part of slice 1.

### How the server re-validates

- Soft lock: the server runs the same selection against its own candidates and its own motor state. There is nothing to validate because nothing was claimed.
- Aim: the server rebuilds the ray origin from its own actor state (authored muzzle/eye offset), accepts the client direction only if it is finite, unit length and within an authored angular tolerance of the server-known facing/camera constraint, and runs its own physics query. The camera never validates a hit.
- Ground: the server clamps the point to `range` from its own actor position, re-runs the ground ray against its collision and rejects points without ground or line of sight.

### How authority, prediction and replay produce the same warp

The motor owns movement, so the motor resolves the warp. `MotionRequest` gains `MotionWarp`: a world-space point plus the gate (`range`, `cone`), the travel bound for this tick (`step`), `stop`, and `fresh` (first tick of the window). The motor resolves it against its own state at step time (`resolve_motion_warp`): if the point is inside range and the facing cone it faces the point and adds bounded root travel toward it; otherwise the request behaves as if it had no warp. Because the motor that steps is the motor that resolves, a replayed tick resolves against the replayed position with no shared history. The cone is centred on the movement input on the fresh tick (falling back to the commanded yaw) and on the current facing afterwards, so the first tick honours intent and later ticks stay on the chosen target.

The targeting module (`targeting.hpp`) only proposes the point: `select_soft_lock` scores every candidate that would pass the motor's gate (nearest wins, angle breaks ties) on the ticks where the actor's state is known, which are the authoritative tick and the live predicted tick. A replayed tick reuses the candidate its original prediction chose, with that candidate's latest confirmed position; the motor still gates it. Authority reads candidates from server state; the owner reads them from its confirmed public view (motor snapshots and public Health). In loopback both see the same pre-step state, so prediction is exact; under latency the difference is an ordinary motor misprediction that reconciliation corrects.

The per-tick travel share is `distance * overlap / (end - begin)`, the same rule the dash uses, so it is a pure function of the action clock (`ability_warp_between`). Soft lock and dash are mutually exclusive on one ability, and the motor clamps warp travel so the total root request stays inside its existing one-metre bound.

### What the HUD shows

- Soft lock: a "Locked on" line on the target panel while the owner's predicted motor is warping toward the target. Presentation only; it reads the same resolver and decides nothing.
- Aim: a screen-centre crosshair while an aim camera is live.
- Ground: the crosshair plus a ground marker of the authored radius at the snapped point, tinted when the point is out of range.

### Scope notes

Soft lock applies to the player's abilities. The scripted training attacker keeps its own `face_player` script; a shared rule for NPCs belongs with the M6 controller seam. No hard lock-on and no camera change: the lock-on camera is a later item.

## Slice 1: melee soft lock

Delivered 11 October 2026.

What exists:

- Ability source: optional `targeting` object with `mode: "soft_lock"` (cooker rejects other modes, unknown fields, a zero cone, `stop` beyond `range`, an empty window, more than 0.5 m per tick, and soft lock combined with a dash or the movement facing lock). Frozen as `AbilityDefinition::soft_lock`.
- Motor: `MotionWarp` on `MotionRequest`, resolved by `resolve_motion_warp` at step time; the root request is turned with the new facing and the travel is clamped inside the motor's existing one-metre request bound.
- Targeting module: `targeting.hpp` / `targeting.cpp`, `select_soft_lock`.
- Scene session: authority proposes from server state, the live prediction from the confirmed public view, replay reuses the predicted choice per tick. `CharacterSceneSession::soft_lock()` reports the owner's predicted lock for presentation.
- HUD: `HudViewModel::target_locked`, "Locked on" under the target bar.
- Editor: "Targeting" section on the Ability form (ordinary draft edits, so Undo/Redo, private validation and coordinated Save apply).
- Owned content: the Royal Light (`light.daability`) soft-locks with range 4.5 m, cone 75 degrees, stop 1.2 m, closing distance 2 m over its first 16 ticks (before the hit window). Heavy is unchanged.

Two details settled during implementation, both in the code comments:

- The activation tick has action clock 0, so a window that begins at 0 treats that zero-length tick as its fresh tick: the actor turns on the press tick and starts closing on the next one.
- Ticks after the fresh one only track a target already within about 20 degrees of the current facing. Without this a target the activation tick rejected (movement input pointing away) was acquired one tick later from the facing alone.

Verified:

- `MotorTests`: resolver gates (range, cone from movement input, cone from facing on later ticks, stop distance, inactive) and a stepped motor that turns, closes along the new facing and holds near the stop distance; a gated-out warp leaves the commanded yaw alone.
- `CharacterCombatSceneTests` with the cooked Royal kit and a target 2.12 m away at 45 degrees: Light turns to the target on the activation tick under authority and prediction, closes 2.12 m to 1.13 m, prediction error 0 on every tick with no resynchronisation, the indicator ends with the window, and the swing hits once (Health 75). Controls: the same swing with targeting removed misses and stays in place; movement input pointing away (outside the cone) gives no lock and no hit; a target 5.66 m away is out of range and does not pull the player. Freezing rejects a zero cone and soft lock plus dash.
- `NativeAuthoringTests`: targeting edit, Undo, Redo, private validation, five invalid candidates rejected without publication, edits fully undone; the frozen kit carries the Light soft lock.
- Fresh editor Play on D3D12, `--exercise-soft-lock` (strafe aside, push forward, press Light): `Editor soft lock tick=30 action_clock=9 yaw_error=0.00195129 distance=1.19622 predicted_error=0 pending=0 hud_lock=1`; capture `evidence/soft-lock-d3d12.png` inspected (player turned onto the target mid-swing, "Locked on" shown).
- `python scripts/verify_effect_reaction.py`: 26 gates pass. CTest: see STATUS for the run of this date.

Not verified, or not built:

- Replay under latency. In loopback the correction arrives on the same tick, so the replay branch that reuses a predicted choice has not run against a real delay. That belongs to the deferred GNS qualification.
- More than one candidate in a live session. The selector handles a list and its ordering rule is unit-level only (the scene has one target).
- Soft lock for the scripted attacker or NPCs (deliberately out of scope here).
- Vertical separation is a coarse gate (height difference within `range`); no line-of-sight check. Hits still come only from the authoritative melee query, so this affects the assist, not damage.
- Physical pointer/controller input; the exercise is scripted input through the input profile.

## Next slice: crosshair aim (plan revised 11 October 2026 after tracing the code)

Proposed change to the design above: do not add an aim payload to `AbilityIntent`. The aim direction is continuous look input, and the commanded yaw already travels in `MotorInput` on every tick, is validated by the server, kept in the owner's input history and replayed. With the Aim camera live the character already faces the camera yaw. So:

- Horizontal aim needs no wire change: the server takes the aim direction from the actor's own yaw at the hit tick.
- Vertical aim adds one bounded `pitch` to `MotorInput` (and its codec in `world_session.cpp`), which gives authority, prediction and replay the same value through the existing motor path. The client never sends a point or a target; ground-snapped AOE is the server's own ray against its own collision from that direction.
- Known limit to state when it lands: the ray starts at the server's actor (authored eye/muzzle offset), not at the camera, so a third-person crosshair has parallax at close range.

Work for the slice, as one chunk with item 2 (ranged abilities), because aim is not observable without a consumer:

1. `targeting.mode: "aim"` with `range` in the cooker, freeze validation and the Ability form.
2. A ranged hit profile: `AbilityMelee` gains a sweep length (or a sibling ray profile) and `PhysicsMeleeQuery::query` sweeps the sphere along the facing with `PhysicsWorld::sweep`; damage and effects go through the existing evaluator and bindings.
3. Owned content: a bow-shot ability from the converted Blink clip on a ranged slot of the Royal kit (check how the ranged slots are routed with the `combat.ranged` modifier before binding).
4. HUD crosshair while an aim camera is live.
5. Then `pitch` in `MotorInput` and the server ray using it.
