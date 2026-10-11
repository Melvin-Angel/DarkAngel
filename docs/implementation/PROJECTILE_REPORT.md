# Travelling projectiles

Stage 3 of [COMBAT_SANDBOX_PLAN.md](COMBAT_SANDBOX_PLAN.md) (user decision 5: real travelling projectiles with visible geometry, configurable speed, gravity and lifetime, swept collision, structured for server authority). Written 11 October 2026; design first, then what was built and verified as one chunk.

## Design

- **A projectile is an option on an aimed ability, not a new kind of ability.** `targeting` mode `aim` gains an optional `projectile` record: `speed` (m/s, 1 to 200), `gravity` (m/s2 downward, 0 to 50) and `lifetime` (ticks, 1 to 600). Without it the ability keeps the instant sweep. Everything else about the ability (cost, cooldown, hit block, hit profile, power, evaluator, kit effect bindings) is unchanged, so a projectile hit is credited, evaluated and applies effects exactly as a melee hit does.
- **Launch.** When the ability's hit block opens, authority launches one projectile per hit block and loop from the hit profile's offset (turned with the actor's yaw), along the actor's yaw and confirmed aim pitch. Nothing comes from the client beyond the two angles already in its motor input.
- **Flight is authority's alone.** `WorldSession` owns the projectiles in flight. Once a tick, after motors have stepped and in launch order, each one applies gravity to its velocity and takes one swept step (a sphere of the hit profile's radius against characters, a ray against static, kinematic and dynamic collision). The first character in the path takes the hit; collision met sooner, the aim range or the lifetime ends the flight. The source is never hit. A projectile that meets an ally stops and does nothing (the no-friendly-fire rule, applied in the shared strike path).
- **One strike path.** The team check, liveness check, per-activation hit memory, the game evaluator, damage and on-hit effects are one function used by melee candidates and projectile impacts alike.
- **Replication is a public list.** The server sends the whole list of projectiles in flight (identity, position, velocity) as one unreliable state packet whenever it changes, tagged with the tick. The client keeps the latest. It is presentation state: the client never simulates a projectile and never reports a hit. The list is bounded at 16 in flight, which fits one packet; a launch beyond that is dropped.
- **Prediction.** The owner predicts its own ability activation and cost as before. It does not predict the projectile; the arrow appears with the server's first frame. In loopback that is the same tick.

## Built

- `AbilityProjectile` on `AbilityAim` (`ability.hpp`), validated when an ability is frozen and decoded by the ability cooker.
- `MeleeQuery::trace` and `PhysicsMeleeQuery::trace` (one swept step), and `melee_origin` (the launch point).
- `WorldSession`: projectiles in flight, launch and advance inside `resolve_ability_hits`, the shared strike, wire kind `Projectiles`, `projectiles()` on both roles.
- `CharacterSceneSession::projectiles()` (the client's list).
- Editor: the Ability form's aim section has a "Travelling projectile" switch with speed, gravity and lifetime; Play draws each projectile as a small solid pointing along its velocity.
- Content: the owned Bow Shot fires a projectile at 40 m/s with gravity 5 m/s2 and a lifetime of 120 ticks; its range stays 20 m.

## Verified (build of 11 October 2026)

- CharacterCombatSceneTests: an arrow loosed at a target 16 m away is one replicated object with a stable identity, moves forward every tick, leaves the target untouched while it flies, lands between 15 and 30 ticks after it appears and hits once for 20; gravity lowers it by more than 15 cm on the way; it is gone from the list after landing; the owner's prediction is undisturbed. The same ability without a projectile hits instantly and never lists anything. Freezing rejects a projectile without speed or lifetime. The earlier ranged cases (range, aim direction, static blocking, vertical aim, crosshair convergence) pass with the projectile.
- All 36 verify gates pass, including the new projectile-flight-d3d12 (a shot at a dummy 14 m away captured with one projectile in flight and the dummy still at full Health; capture looked at). CTest shows the five known unrelated failures plus one transient failure of M5.external_import (access denied renaming a folder in its own scratch directory; it passes when run alone and the change does not touch import code). After that pass the projectile's placeholder mesh was made thicker and brighter (editor-only); only its own gate was re-run.

## Not verified, and known limits

- No NPC fires a projectile; there is no NPC ranged content.
- The projectile mesh is a placeholder solid built in the editor. No arrow asset, trail, impact effect or sound, and the bow animation is not timed to the release.
- At most 16 projectiles in flight; more are dropped without a message.
- A fast projectile is one sphere sweep per tick, so it cannot tunnel, but it resolves against where characters are at the end of the tick.
- The owner does not predict the projectile, so under latency the arrow will appear late by the round trip. Not measured (GNS deferred).
- A projectile whose source leaves the session is removed; one whose source died keeps flying and still hits.

## NPC ranged attacks (11 October 2026)

User request of 11 October 2026.

- **Aim belongs to the session, not the controller.** A controller still only names a target and presses a slot. When the pressed ability is an aimed one, `CharacterSceneSession` points the actor from its muzzle at the body of the target latched with that press (the same latch the NPC soft lock uses), adds the drop a projectile will suffer on the way, and writes the yaw and pitch into the actor's motor input. From there the shot is the player's shot: confirmed pitch in the motor state, launch from the hit profile offset, authoritative flight and strike. No controller sets a pitch, so a behaviour tree needs nothing new: its `attack` leaf already accepts `ranged_light`.
- **Content.** `content/npcs/royal_archer.danpc` (Royal Character and kit, 40 Health, faction `royal_guard`) with `content/npcs/archer_ranged.dabehavior`: within 14 m and idle it faces, presses the ranged slot and recovers 120 ticks; otherwise it approaches to 14 m. It is in the Place panel's NPC category like any other NPC.

Verified: in CharacterCombatSceneTests an archer 10 m away looses exactly one arrow that hits the player for 20; with the player standing on a slab 3 m above it the arrow still hits; with a controller that names no target the shot is level and passes under the raised player. All 40 verify gates pass (the two-guard gate now accepts two or more guards, because the user's own uncommitted combat scene has three); CTest shows only the five known unrelated failures.

Not verified, and limits: the aim is a straight line with a drop correction, with no lead for a moving target and no line-of-sight check before firing, so an archer shoots at walls between it and its target. The archer uses the player's bow animation and kit.
