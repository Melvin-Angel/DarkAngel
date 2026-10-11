# Stagger as gameplay: effect movement lock, stun loop, default Flinch - 11 October 2026

Second item of the autonomous M5 list. Until now a stagger was a pose plus an ability gate (the State.Staggered tag). It is now also a movement restriction decided by authority and predicted by the owner, a status can loop its reaction clip (stun), death stops movement, and the Royal kit binds the owned Flinch by default. M4/M5 remain In progress.

## What an author does

Ability workspace, select an Effect:

- **Lock movement while active** (Status rules): while an unsuppressed instance of the effect is on an actor, that actor cannot walk or jump. Root motion from actions (for example a dodge dash), facing and gravity are unaffected. Needs a public finite or until-removed effect; the form says so inline and Save rejects other combinations.
- **Loop the reaction while the status lasts** (Reaction presentation, when a reaction timeline is bound): the reaction clip repeats on the effect-instance clock until the effect ends, instead of playing once.

Both are optional source fields (`lock_movement`, `reaction_loop`); effect sources without them are unchanged and keep their generations.

## Runtime behaviour

- **Authority**: each fixed tick, after effects have advanced, `CharacterSceneSession` asks the server for the actor's active effects. Any unsuppressed instance whose frozen definition locks movement, or Health 0, sets the motor's existing lock request and drops the jump for that tick. This applies to the player and to the combat target. The motor is unchanged; presentation decides nothing.
- **Owner prediction**: the owner applies the same restriction to its predicted and replayed ticks from state it has confirmed: its corrected Health and the instance clocks (start, end, suppressed) in its latest checked public effect frame. A lock therefore starts on the tick after the hit on both sides and ends on the expiry tick on both sides, with zero predicted-position error in loopback. That is why the flag requires a public effect.
- **Stun loop**: `ReactionPresentation` samples a looping reaction at the instance clock modulo the clip length; one-shot reactions are unchanged. Selection order, suppression, refresh and late-join rules are unchanged, so a late joiner sees the current phase of the loop.
- No wire, protocol or motor change. The frozen effect definition gained two flags.

## Owned Royal content

- `flinch.daeffect` now locks movement for its 30 ticks, and `player.dakit` binds it to the Light hit by default. Any actor hit by a Royal Light attack flinches, loses its current action and is held in place for half a second.
- New `stun.daeffect` + `stun.daaction`: 90 ticks, grants State.Staggered, interrupts, locks movement, loops the converted Blink `stunned` clip (priority 40, above Flinch). It is shipped unbound; bind it to a hit window in the Combat kit panel (for example Heavy) to use it.

## Verification

Every target was rebuilt through the pinned build command (`effects.hpp` and the effect cooker changed; no vcpkg activity). `python scripts/verify_effect_reaction.py` then ran twenty-two gates and all pass ([receipt](evidence/effect-reaction.json)). The whole CTest suite was run once after the runtime change: the five known unrelated failures plus `M5.observed_character_pose`, whose fixture did not load reaction clips for the kit's effects; after that test fix the affected subset (authoring, observed pose, reaction, effects, GNS effect, character and Royal heavy scenes; 13 tests) passes. The full suite was not run a second time.

- **CharacterCombatSceneTests** (cooked Royal `player.dakit`, scripted attacker, fresh sessions):
  - The frozen Royal kit carries the owned Flinch on the Light hit with its reaction, interruption and movement lock.
  - Stagger: after a Light hit the player holds a walk-and-jump input for ten ticks and does not move; authoritative and predicted positions agree, nothing is pending and no resynchronisation is added. On the tick the 30-tick status expires the player walks again, again with zero prediction error. The flinch clip (40 ticks) is longer than the status, so the lock follows the status, not the animation.
  - Stun (bound to the Heavy hit inside the test): the owner pose is the Blink stunned clip on the effect-instance clock, the player cannot move for the whole 90 ticks with prediction in agreement, and both the pose and the lock end with the status.
  - Death: a dead player holding walk and jump stays where it died on authority and in prediction.
  - The earlier attacker checks (damage, interruption, dodge invulnerability, death timeline) pass unchanged with the lock in place.
- **ReactionPresentationTests**: a movement lock is accepted on a public persistent effect and rejected on an owner-only one; a loop flag without a reaction timeline is rejected; a looping reaction samples the instance clock modulo the clip length and keeps its onset, while a one-shot at the same tick shows nothing.
- **Re-baselined for the default Flinch binding**: `RoyalHeavySceneTests` (two bindings, reaction clips loaded), `NativeAuthoringTests` (the pre-reaction package is checked for the authored reaction specifically), `ObservedCharacterPoseTests` (reaction clips loaded), `AuthoredEffectGnsTests` (finds the edited Burn binding by value instead of by position; it compiles, but it is driven by its own script and was not run here). No expected gameplay number changed.
- **Cooker**: source inspection (which has no frozen reaction action yet) first rejected the looping Stun; found by the suite and fixed before the commit. Cooking and loading `stun.daeffect` is now part of CharacterCombatSceneTests.
- **D3D12 editor Play**, capture inspected: [Light hit at tick 40](evidence/attacked-light-d3d12.png) on the authoring fixture's copy of the Royal kit with `--target-attacks light`: player Health 75, owner flinch at clip tick 14, prediction pending 0, scene unchanged. The two Heavy captures from the attacker increment were regenerated and still pass.

## Limits

- The lock is all-or-nothing: no slow, no knockback or pushback, no partial turn restriction. Poise, armour and stagger thresholds are still an open design decision; every hit that carries the status staggers.
- A status applied inside the effect-advance step of a tick (rather than by a hit) locks authority one tick before the owner can predict it; the ordinary motor correction covers that tick. Under real latency the owner learns of a lock one round trip late and is corrected the same way. Neither case was measured over GNS.
- Death stops walking and jumping but there is still no respawn, downed state, corpse collision rule or despawn.
- Stun is not bound by default and has no dedicated State.Stunned tag (adding a tag needs the generated-tag rebuild fences); it reuses State.Staggered.
- Reaction blends, queues and additive reactions are still not delivered.
- The two new Effect form checkboxes compile and edit the draft like the neighbouring fields, but no test or capture exercises them; the flags were verified through source files and the cooker.
- Not verified: Vulkan, host/client over GNS, the older `verify_royal_combat.py` D3D12 markers after the default Flinch binding.
