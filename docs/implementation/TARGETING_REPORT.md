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

## Slice 2: crosshair aim and the first ranged ability (11 October 2026)

Built in one chunk with navigation and verified once.

Design change from the top of this report, made after tracing the code: there is no aim payload on `AbilityIntent`. The aim direction is continuous look input, and the commanded yaw already travels in `MotorInput` on every tick, is validated by the server, kept in the owner's input history and replayed. With the Aim camera live the character already faces the camera yaw. So horizontal aim needs no wire change: the server takes the aim direction from the actor's own facing at the hit tick, and the client never sends a direction, point or target for an ability.

What exists:

- Ability source: `"targeting": {"mode": "aim", "range": 20.0}`. Frozen as `AbilityDefinition::aim`. An aim ability needs at least one hit profile, a range up to 60 m, and excludes soft lock.
- Ranged hit profile: `AbilityMelee::sweep`. For an aim ability the cooker sets every hit profile's sweep to the aim range, and `PhysicsMeleeQuery` sweeps the profile's sphere from its offset along the facing, keeps only the first actor it meets, and applies the existing line-of-sight check, so static collision blocks the shot. Damage and effects go through the existing evaluator and kit bindings. This is a hitscan shot, not a travelling projectile.
- Owned content: Bow Shot (`bow.daability`, `bow.daaction`, the converted Blink bow-shot clip) on the Royal kit's ranged-light slot: Stamina 10, power 20, range 20 m, hit window at ticks 30 to 34. It is pressed with the existing `projectile.light` chord (Mouse.Left with Alt held, or the gamepad equivalent), which is also what raises the Aim camera.
- HUD: a screen-centre crosshair while an aim camera is live (`HudViewModel::crosshair`).
- Editor: the Ability form's Targeting section is now a mode choice (None, Crosshair aim, Melee soft lock).

Verified:

- `CharacterCombatSceneTests`: Bow Shot starts through authority and prediction, charges Stamina once, and hits a target 8 m straight ahead for 20; it misses when the commanded yaw points 0.5 rad away, misses a target 23 m away, is blocked by a wall between the actors, and still hits at melee distance. Freezing rejects an aim ability whose hit profile is not swept and a range beyond the bound.
- `HudTests`: crosshair at the screen centre while aiming, absent otherwise.
- Fresh editor Play on D3D12, `--exercise-ranged`: `Editor ranged tick=60 camera=Aim range=20 distance=1.2 target_health=80 pending=0 hud_crosshair=1`; capture `evidence/ranged-d3d12.png` inspected (Aim camera, crosshair, bow pose, target at 80 / 100). The Royal scene's target stands 1.2 m away, so the long-range cases are proven in the scene test, not in this capture.

Not built, not verified:

- Vertical aim. The shot is horizontal at the profile's authored height. Pitch is the next step: one bounded `pitch` on `MotorInput` (and its codec in `world_session.cpp`), used by the server's sweep.
- Parallax: the shot starts at the actor, not at the camera, so at close range it does not pass exactly through a third-person crosshair.
- A travelling projectile with flight time, and any projectile presentation (arrow, trail, impact).
- The owner does not predict the hit; like melee, damage appears with the server's result.
- Ground-snapped AOE aim (slice 3).

## Next slices

1. Vertical aim. Not just a pitch value: in third person the camera sits above and behind the character and looks down by default, so the raw camera pitch would send a shot from the character into the ground at range. The client must first converge the crosshair: cast the camera ray into the world, take the point it meets (or a far point), and derive yaw and pitch from the actor's muzzle to that point. Those two angles then travel in `MotorInput` (pitch is the new field, with its codec in `world_session.cpp`) and the server sweep uses them. Shared wire code: one full rebuild and CTest.
2. Ground-snapped AOE aim: `mode: "ground"`, the server's own ray against its collision from the same yaw and pitch, clamped to the authored range, with a ground marker on the HUD.
3. Travelling projectiles if wanted: an authoritative projectile entity advanced by the server with the same sweep per tick.

## Combat sandbox stage 2: full 3D aim and shared soft lock (11 October 2026)

Stage 2 of [COMBAT_SANDBOX_PLAN.md](COMBAT_SANDBOX_PLAN.md) (user decisions 4 and 6). Design first, then what was built and verified in one chunk.

### Design

- **Aim is two angles, not a ray.** The client never sends a ray, a point or a target. It converges the crosshair locally: cast the camera ray into its own view of the world, take the point it meets (or the far point at the ability's range), and work out the yaw and pitch that send a shot from the character's muzzle through that point. Yaw already travelled in `MotorInput`; `pitch` is the one new field (radians, positive up, at most 1.5 either way). The server validates the bound and sweeps its own world from its own muzzle along those angles. A wrong or malicious pitch can only aim the client's own shot somewhere else.
- **Pitch is confirmed state.** `MotorState::pitch` is set from the accepted input when the motor steps, exactly like yaw, and replicates with the state. So the hit query reads it from the same state it already checks against the physics pose, the owner's prediction and replay carry it with the rest of the input history, and observers can later present another actor's aim.
- **Convergence starts level with the owner.** The ray starts where it passes the owner, so ground or a wall between a third-person camera and the character is never the aim point. If the point ends up at or behind the muzzle the camera's own direction is used.
- **The spawn position is the hit profile's offset.** The sweep leaves from the profile's offset (right, up, forward of the actor) toward the aim point. Stage 3's travelling projectile will leave from the same place along the same two angles.
- **Soft lock is one rule for every actor.** `select_soft_lock` and the motor's warp resolver are unchanged and now serve three callers: the player on authority, the player in prediction, and NPCs on authority. What differs is only the candidate list: the player's is every live hostile actor; an NPC's is the one target its controller named when the press was accepted, latched for the length of that action. Configuration stays on the Ability (`targeting`), so Light and Heavy are tuned separately by content.

### Built

- `MotorInput::pitch` and `MotorState::pitch`, both validated, both on the wire (`session_wire.hpp` motor state, `world_session.cpp` motor command). No protocol number change: there are no deployed peers, and both ends are always the same build.
- `PhysicsMeleeQuery` sweeps a ranged profile along yaw and pitch.
- `CharacterSceneInput::pitch`; `CharacterSceneSession::aim(camera, direction, range, muzzle)` returns `AimSolution{yaw, pitch, point, hit}`. It casts against the owner's predicted collision and the other actors' confirmed public positions (upright capsules).
- Editor Play: while an aim camera is live the input yaw and pitch come from `aim` with the muzzle and range of the kit's aimed ability.
- NPC soft lock: an actor's accepted press latches `ControllerCommand::target`; its action's warp runs through the same `lock` call as the player's with that one candidate.
- Content: the owned Royal Heavy has the same soft lock as Light (range 4.5 m, cone 75 degrees, stop 1.2 m, 2 m over its first 16 ticks, before its hit window).
- Capacity correction for stage 1 (superseded the same day by the section "Session capacity and no friendly fire" below, which lifts it to 16): `character_scene_actor_limit` is 3, not 16. WorldSession (motor snapshots, public ability frames, effect frames, ability owners), the collision stream and the physics history each carry at most four characters, and the loopback queue holds 32 packets a tick, so a scene holds the player and three actors. A scene with more spawn points is refused when Play prepares, with a plain message. Lifting this needs per-tick snapshot batching and is its own piece of work.

### Verified (build of 11 October 2026)

- CharacterCombatSceneTests: a target on a slab 3 m up is missed by a level shot and hit with the elevation; the elevation is equal in authority, prediction and the public frame; aiming down or aiming up at a ground target misses; a pitch of 2 rad is rejected. `aim` on the raised actor, on the ground and on empty sky gives angles whose muzzle line passes within 2 cm of the point; a ray that lands at the owner's feet does not turn the owner around; a shot fired with the solved angles hits the actor under the crosshair. A guard 2.5 m away closes to its stop distance and hits through the shared soft lock, and misses from where it stands when the ability has no targeting. Heavy soft-locks an off-axis target with zero prediction error.
- RoyalHeavySceneTests and MotorTests pass unchanged.
- Gate script and CTest: All 35 verify gates pass; CTest shows only the five known unrelated failures, after one test-only fix (CollisionStreamTests hand-builds a raw motor command and needed the new pitch field; it was rebuilt and passes, CTest was not run a second time).

### Not verified, and known limits

- The camera-to-angles path in the editor is exercised only by the scripted `--exercise-ranged` and `--exercise-aim` runs on flat ground with the target 1.2 m away; no editor gate aims up or down at a distant target.
- NPCs have no ranged content, so no NPC sets a pitch. A controller can only ask for yaw.
- The owner does not see other actors' capsules exactly: convergence uses a fixed 0.35 m by 1.8 m capsule at the confirmed position.
- Still hitscan. Travelling projectiles are stage 3.
- Hits ignored faction in this build; fixed in the next section.
- No run under real latency (GNS deferred).

## Session capacity and no friendly fire (11 October 2026)

Two user decisions of 11 October 2026, taken after stage 2: lift the four-character limit before projectiles, and no friendly fire.

### Capacity

- `session_character_capacity` (17: one owner and sixteen others, in `character_motor.hpp`) replaces the separate literal 4s: motor snapshots, public ability frames, public ability partials, effect frames and ability owners in `WorldSession`; actors in a collision frame (`collision_source.cpp`, the physics history); and the motors one `PhysicsWorld` accepts. Owner-only state (owned motors, ability corrections) stays at 4, the peer limit.
- `character_scene_actor_limit` is `session_character_capacity - 1`, so 16, as the stage 1 plan intended.
- No wire layout changed. The existing per-tick packet budget (16 a tick, 32 queued in loopback) is not enough for 17 characters in one send, but every state stream already retries what it could not send, and the scene session already pumps until the tick's snapshots have arrived, so a full scene costs more pump rounds per tick instead of failing. That is acceptable in loopback only: over a real network the per-character snapshot packets should be batched before this capacity is relied on. Recorded as open work, not done.

### No friendly fire

- `WorldSession::assign_team(network id, team)`: server-only state, never replicated. In hit resolution a candidate that shares a nonzero team with the attacker is skipped before any damage or effect is evaluated.
- `CharacterSceneSession` assigns each character its faction number as its team, so NPCs of one faction cannot hurt each other and the player cannot hurt an ally.
- A swept shot still stops at the first character in its path, ally or not, and then does nothing to an ally.

### Verified (build of 11 October 2026)

- CharacterCombatSceneTests: a scene of sixteen actors beside the player (four controlled guards, twelve bystanders) runs with every actor replicated on the current tick, no pending prediction and no resynchronisation, and no bystander of the guards' faction is damaged; a guard's swing at the player leaves a guard of its own faction standing in it at full Health and hits a bystander of another faction; the player's swing does not damage an ally.
- 35 verify gates pass. In `npc-spawn-points-d3d12` the second guard now stays at 60/60 (it was 35/60 before) and both guards defeat the player; capture looked at. CTest shows only the five known unrelated failures.

### Not verified

- Sixteen actors in the editor: the owned combat scene has two spawn points, so the full count is proven in the scene test only. Frame cost with many skinned actors was not measured.
- Any real transport. The capacity was exercised in loopback only.

## Motor snapshot batching and the packet budget (11 October 2026)

User request of 11 October 2026: batch per-character snapshot packets so a full scene holds over a real network. What was done, and what it showed.

- **Done.** Motor states that are due are packed three to a state packet (wire kind `MotorBatch`; three is what fits the 1000-byte packet limit) instead of one packet each. The single-state kind is still accepted. `WorldSession::packets_sent` counts packets per peer.
- **Measured** (CharacterCombatSceneTests, seventeen characters, loopback): about 30 server packets per tick, down from about 41. The motor share went from 17 packets to 6.
- **What remains, and why batching is not the answer.** The session's own budget is 16 packets a tick. Most of the remaining 30 is one public ability frame per character per tick (a frame carries the motor state again plus public attributes, action state and tags, roughly half a packet each), plus effect frames and the collision stream. Packing those two to a packet would save perhaps eight more and still leave a full scene over budget, and 30 packets of up to 1000 bytes sixty times a second is far more than a real connection should carry in any case. A full scene works in loopback because every state stream retries what it could not send and the scene session pumps until the tick's state has arrived.
- **What would fix it** (not built; it changes how the scene session and observers consume state, so it should be designed on its own): send remote characters' public state at a lower rate than the simulation (the observer already buffers and interpolates), send a character's public ability frame only when something other than its clock changed, and stop repeating the motor state inside the public frame. That also means the scene session must stop requiring every character's public frame on the same tick as the owner's correction.

All 40 verify gates pass (the two-guard gate now accepts two or more guards, because the user's own uncommitted combat scene has three); CTest shows only the five known unrelated failures.

## Packet budget design: send what changed, at the rate the reader needs (11 October 2026)

User request of 11 October 2026. Design only; no code has been changed. It starts from "What would fix it" above and from reading the send paths in `engine/runtime/world_session.cpp` (`pump_abilities`, `pump_public_abilities`, `pump_effects`, `pump_collision`, the motor batch) and the reader in `CharacterSceneSession::step`.

### What the code does today

- `pump_public_abilities` already skips a character whose public snapshot `revision` is unchanged, except for a keep-alive every 30 ticks. It still sends one frame per character per tick because the scene session requires it: `public_ready` in `CharacterSceneSession::step` wants `ability.tick == tick` for the player and every actor, and the snapshot's revision moves with its clock.
- Each public frame carries the whole `MotorState` again (`encode_ability_public({id, epoch, revision, motor, snapshot})`), although every character's motor state already travels in the motor batch.
- Each frame is its own packet (chunked at `ability_chunk_bytes`), where motor states go three to a packet.
- `packets_sent` is one number per peer. The split of the measured 30 packets between motor, public ability, effect, correction and collision packets is an estimate in the section above, not a measurement.

### Step 0: measure by kind

Count packets per wire kind per peer (beside the total) and print the split in the sixteen-actor test. Every later step is judged against that table, and the estimate above is replaced by it. No behaviour change.

### Step 1: a public ability frame only when its content changed

- Split the public snapshot's identity in two: a **content revision** that moves when attributes, tags, the active ability, its action identity or the loadout change, and the **clock** (tick and action clock). The send test in `pump_public_abilities` uses the content revision; the 30-tick keep-alive stays as the repair for a lost unreliable packet.
- The reader derives the clock. For an idle character a frame is valid for every later tick. For an active action the clock advances one tick per tick from the frame's own tick, which `ObserverAbility` can do because it already samples buffered frames at a render tick. An action that ends changes content, so the end still arrives as a frame.
- Risk to check in code before building: anything on the server that changes public state on a clock without bumping the content revision (cooldown displays, periodic effects that change a public attribute). Those must bump it.

### Step 2: no motor state inside the public frame

The frame keeps only what fences it: the avatar epoch and its tick. `ObserverAbilitySample::frame.motor` is then filled by the client from the motor stream for the same character. This is a wire change to `AbilityPublic` (protocol 3), so the codec, the wire tests and the GNS tests that hand-build frames change with it: plan one full rebuild and CTest. Smaller frames then go several to a packet, the same way `MotorBatch` does.

### Step 3: remote motor states at a lower rate, by relevance

- The owner's own correction and motor state stay every tick; prediction needs them.
- A remote character within a relevance radius of the owner (proposed 12 m, a session limit) is sent every tick. That keeps melee, soft lock and aim against nearby actors exactly as they are, including the zero prediction error the soft-lock gates assert in loopback.
- A remote character beyond it is sent every third tick, staggered by network identity so the load is even. `ObserverAbility` and `ObservedCharacterPose` interpolate between the states they have, as they would have to over any real connection.
- Product decision hidden in this, to confirm with the user before building: the relevance radius, and whether far characters at 20 states a second look acceptable. It is a tuning number, not a blocker for steps 0 to 2.

### The same-tick requirement in CharacterSceneSession::step

Settled as follows, before any code:

- `step` keeps requiring, on the tick: the owner's motor snapshot, the owner's ability correction and the collision frame. Those drive reconciliation and nothing else can stand in for them.
- `public_ready` is removed from `snapshots_ready` and from the `require` after reconciliation. Public frames and remote motor states are consumed as they arrive: an observer is pushed a frame only when the client holds a newer one than it last pushed.
- What replaces the guarantee is a staleness bound, checked instead of equality: a remote character's newest public content is never older than the keep-alive interval, and its newest motor state never older than its send interval plus the retry the state stream already does. The session fails (resynchronise) only when that bound is broken, not when a frame is simply not due.
- Readers that assumed the current tick and must be changed together: `client_candidates` (soft-lock prediction reads remote motor positions and public Health; inside the relevance radius they are still current), `CharacterSceneSession::aim` (confirmed public positions; a far target can be up to three ticks old, which is acceptable because aim only shapes input and the server sweeps its own state), `actor.pose->sample(observers.at(id)->sample(tick))` (samples at the render tick, extrapolating the action clock), `effect_views` (already tolerant: they push only when a frame is found), and the Gameplay Inspector.
- Tests that assert `observer(id, tick).frame.motor.tick == motor().tick` for every actor (the sixteen-actor block of CharacterCombatSceneTests) change to the staleness bound; tests of a single nearby target keep the exact tick.

### Expected result (an estimate until step 0 gives the real split)

For the sixteen-actor test, where most actors stand still: public ability packets fall from about one per character per tick to the few characters whose content changed, motor packets from 6 to between 2 and 6 depending on how many actors are inside the relevance radius, and the rest (owner correction, effects on change, collision) stays. The target is under the 16-packet budget with every actor near the player and well under it in the usual case. Report the before and after numbers from `CharacterSceneSession::server_packets` and the per-kind split; the before number on today's build is 30.04 packets a tick.

### Order of work

Step 0, then 1 with the `step` change (they depend on each other), measured; then 2 (the wire change, one full rebuild); then 3 after the user confirms the radius. One full verification pass at the end of the chunk; steps 1 to 3 touch shared runtime, wire and prediction code.

## Packet budget, first build: 30 to 17.8 packets a tick (11 October 2026)

Steps 0 and 1 of the design above are built, with one change to the design found while building.

- **Change to the design.** Step 1 said the reader could derive an idle character's clock on its own. It cannot know on its own that nothing changed this tick, and an idle character that is walking still needs a fresh motor state in the frame observers read. So the server now sends a **public index** (wire kind `PublicIndex`, one packet a tick): each character's public ability tick and a content value (`public_ability_content`: attributes, tags, loadout and the running action with its clock, without tick and revision). A client holding a frame with the same content knows it is current. The scene session then brings that frame to the tick with the character's motor state from the motor stream (`shown` in character_scene.cpp). This is the client half of step 2; the wire frame still carries its own copy of the motor state.
- **Server.** `pump_public_abilities` sends a frame when its content changed (or as the 30-tick keep-alive), not when only its clock did. A running action is content, so an acting character is still sent every tick. `pump_public_index` sends the index.
- **Same-tick requirement.** `public_ready` in `CharacterSceneSession::step` now asks, per character: the index and the motor state are at the tick, and the held frame is either at the tick or idle with the indexed content. The owner's motor, correction and collision requirements are unchanged. It is still an exact rule, not yet the staleness bound of the design; that comes with step 3.
- **Step 0.** `WorldSession::packets_by_kind` and `CharacterSceneSession::server_packet_kinds`; the sixteen-actor test prints the split.

### Measured (CharacterCombatSceneTests, seventeen characters, loopback)

| | before | after |
|---|---|---|
| server packets per tick | 30.04 | 17.79 |

After, by kind: motor batch 6.00, collision 5.45, public ability 3.09, owner correction 2.21, public index 1.00. The before split was not measured (the counter did not exist); the public ability share before was about one packet per character per tick. The test's bound is now 19 (was 34).

Still above the 16-packet budget by about two. What is left, largest first: motor states for seventeen characters every tick (step 3, relevance and rate), the collision stream at five to six packets a tick (not looked at yet: seventeen moving character bodies), and the owner correction at two packets.

### Verified

Full pass on this build: all 41 verify gates and CTest with only the five known unrelated failures. The combat scene test's exact-tick assertions on observed actors (every actor replicated on the current tick, action phase on the render clock) pass unchanged. One capture looked at (`npc-spawn-points-d3d12`: two guards standing over the defeated player, target bar shown).

### Not verified

Packet loss: a lost public frame is repaired by the 30-tick keep-alive, and until then the scene session waits for it (its pump bound is 32). Over a real lossy connection that wait must become the staleness bound of the design. GNS tests pass but none exercises the index under loss.

## Packet budget, second build: under budget at 14.4 packets a tick (11 October 2026)

- **Cause found.** The collision stream packed a fixed four items to a packet, a count sized for its largest item. A character entry is 88 bytes and a box 176, so seventeen characters and the ground took five to six packets every tick.
- **Change.** Collision items are packed whole, as many as fit a packet (`collision_chunk_starts` in world_session.cpp). The receiver reads each chunk to its end and checks the item total instead of expecting four per chunk; the old four-per-chunk layout is still a valid input. No wire kind or field changed.

| CharacterCombatSceneTests, seventeen characters | start of day | after public index | now |
|---|---|---|---|
| server packets per tick | 30.04 | 17.79 | 14.40 |

Now, by kind: motor batch 6.00, public ability 3.00, collision 2.18, owner correction 2.18, public index 1.00. The test's bound is the budget itself, 16.

**Verified:** full pass on this build, all 41 verify gates and CTest with only the five known unrelated failures; `CollisionStreamTests` (loss, reorder, duplicates, late join) passes with a largest packet of 952 bytes. No capture was looked at for this build: nothing visible changed since the capture looked at for the first build.

**Not verified / remaining:** the margin is 1.6 packets in a scene where every actor is within reach of the player and four are acting; a scene with more actors acting at once sends more public ability frames. Step 3 of the design (remote motor states by relevance and rate, the largest remaining share) and dropping the motor state from the public frame are not built. Loopback only; packet loss on the public index was not exercised.
