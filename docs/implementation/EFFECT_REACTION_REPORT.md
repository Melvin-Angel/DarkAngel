# Authored effect reaction presentation - 10 October 2026

First bounded M5 reaction slice, implemented from [M5_REACTION_PRESENTATION_PLAN.md](M5_REACTION_PRESENTATION_PLAN.md) (its header table records each decision refined against the code). A short public status can now be authored to interrupt an actor's attack and show one prepared full-body flinch on that actor. M4/M5 remain In progress; this is not a stagger/death/reaction system, an NPC controller or a multiplayer graphical qualification.

## What an author does

Ability workspace, select an Effect. The new **Reaction presentation** section picks any native Composer timeline (`.daaction`), or **Create reaction timeline** copies an existing timeline into a presentation-only one (cue blocks only, full-body, one loop, root policy none) and binds it in the same step. **Edit reaction timeline** opens it in the Composer to choose its clip; **Clear reaction** removes the binding. The form states inline when the chosen timeline or the effect's lifetime/visibility will be rejected by Save, and when Interrupt action is off. Bind the effect to an ability hit window in the kit as before, Save, Play.

In Game, the Gameplay Inspector's Target effects tab begins with a **Reaction presentation** readout for player and target (effect instance, clip tick, priority, and whether it is waiting behind the actor's own action or suppressed at Health 0). The profiler text shows the target's reaction tick.

## Representation and closure

`.daeffect` gains an optional `reaction` action UUID with its locator at `sources.reaction`; sources without it are unchanged and keep their generations. The effect cooker cooks the referenced timeline through the existing action importer, so its clip, Ozz archive and canonical rig join the effect's products. The cooked effect stores `reaction_generation`, the runtime effect generation covers it, and a kit pins the whole closure through its existing effect catalogue. No reaction asset type, registry or runtime was added. Because the locator sits under `sources`, editing a reaction timeline revalidates every effect and kit that uses it during private validation and Save.

Rejected at cook and again by `freeze_effect_definition`: instant, owner or server-only effects; upper-body or masked timelines; more than one loop; a missing clip; motor root ownership; any hit, commit, movement-lock, combo or invulnerability block.

## Runtime behaviour

`ReactionPresentation` (engine/runtime/character_presentation) is a prepared, read-only, stateless selector used by both the owner's confirmed pose in `CharacterSceneSession` and `ObservedCharacterPose`. Input is the already checked public `EffectFrame` held by `EffectPresentation`, plus the actor's public/confirmed Health and active-action state. It reads no aggregate tag.

- Clock: clip tick = pose tick - instance `start`. Duplicate frames, keep-alive resends and late joins therefore sample the current clock and cannot replay a finished one-shot.
- Refresh: `refresh_per_source` keeps the instance handle and onset, so refreshing extends the status and does not restart the animation. A new instance is a new reaction.
- Order: highest timeline priority, then latest onset, then highest effect handle. One reaction per actor, no queue.
- Suppression: authoritatively suppressed instances never present; Health 0 suppresses; an actor's own remaining active action keeps precedence and the reaction joins at its current clock afterwards.
- Completion and removal: the one-shot ends at clip length while the effect, its tag and its gameplay gate continue. Expiry or removal ends presentation at once and leaves other effects alone.
- Fences: wrong actor/session or non-public frame rejects; another avatar epoch never presents; an unknown frozen effect generation rejects and keeps the previous pose and locomotion clock.
- Locomotion keeps advancing underneath; a full-body reaction replaces the pose with one clip sample, so the four-layer budget is untouched.

Interruption itself is unchanged authority: an effective `interrupt_action` effect cancels through `AbilityState::apply_effect` in WorldSession. Presentation executes no blocks, requests no root motion and cancels nothing. No protocol, motor, prediction or wire change.

## Verification

Reproduce with `python scripts/verify_effect_reaction.py --build`. Build plus nine gates pass; [receipt](evidence/effect-reaction.json) (the recorded final run reused the just-built binaries after two editor-only layout changes, so it lists the nine run gates and binary/source fingerprints).

- **NativeAuthoringTests** (copied disposable fixture): blank status has no reaction; create-timeline strips gameplay blocks/root ownership and leaves its template untouched; binding and creation Undo/Redo; clear; hit-block, motor-root, two-loop and owner-visibility candidates reject with no pending source published and the kit source unchanged; private validation publishes nothing; coordinated Save; complete scene package; previous package has no reaction; frozen kit carries the reaction action and clip. Fresh isolated WorldSession Play: Heavy hit at tick 18 starts the target's flinch on the effect clock, target pose equals the frozen clip sample and differs from locomotion, attacker shows no reaction and keeps its action, Burn coexists (two effects, one cue), the clock does not restart over ten ticks, at clip length (52) the pose returns to current locomotion while the status tag remains, at expiry (90) the status tag goes and Burn stays. Tick 150: target Health 39, owner Stamina 80, no active action, prediction pending 0, authoring scene unchanged. A later invalid Save leaves the published source, catalog heads and playable package unchanged.
- **ReactionPresentationTests**: frozen-policy and rig/clip/archive-generation rejection; server-only WorldSession authority with two kitted actors - a non-interrupting status leaves the attack active and is reported as waiting, the interrupting status cancels the victim's attack through the existing path while the other actor's action clock is unchanged, the status (not the animation) gates re-activation, Burn coexists, refresh keeps handle/onset and extends the end, the finished one-shot leaves the status in place and a late refresh does not replay, early removal ends presentation and keeps Burn, lethal Burn suppresses a retained status at Health 0. Selection order, expiry awaiting removal, suppression precedence, avatar/actor/session/audience/generation fences. Replicated frames through `CharacterSceneSession`: late join mid-window samples the current clock, duplicate frame is stable, rejected frames retain pose and clock, tags alone never present, late join after the window shows nothing, Health 0 suppresses, an active public action keeps precedence.
- **Retained shared suites**: ObservedCharacterPoseTests, EffectAssetTests, CombatKitAssetTests, EffectReplicationTests and CombatEffectTests pass unchanged, covering the shared effect definition/cooker/replication code that was touched.
- **Editor rebuilt; D3D12**: [Effect form](evidence/effect-reaction-form.png) shows the Reaction presentation section and the grouped asset browser. [Game at tick 30](evidence/effect-reaction-d3d12.png): Health 60, Stamina 80, pending 0, target reaction at clip tick 12 of 52 from onset 18, player without a reaction, two target effects. Both captures inspected: the target is posed by the reaction clip while the player continues its Heavy.

Not verified: physical pointer interaction with the new controls, Vulkan for this slice, remote graphical timing/loss, multi-client late join over GNS, a positive owner-side flinch in editor Play.

Also in this checkpoint (editor usability): the gameplay asset browser is grouped by type with counts, sorted names and (new)/(edited) markers; Effect forms separate Reaction presentation from Status rules.

Build environment note: the first build in this Claude Code session re-ran the pinned vcpkg manifest install and rebuilt every package from source (about 40 minutes, same versions and triplet). The ABI inputs now record PowerShell 7.6.3; a different PowerShell under the earlier tooling is the likely cause but was not confirmed. No dependency, pin or manifest was changed.

## Limits and not delivered

- One full-body reaction; no blend in/out, additive or upper-body reactions, queues, or per-binding priority separate from the timeline's.
- Reaction cue blocks are allowed but not dispatched yet.
- No flinch clip exists in owned content: the fixture reuses the attack clip, and no default Royal reaction is shipped. Default Royal content is unchanged.
- The editor Play scene has a single attacker (the player), so a positive flinch on the owner's own pose is covered by native WorldSession/selector tests, not by a Play capture.
- No stagger motor restriction, knockback, death/revive presentation, reaction profile asset, NPC/AI.
- History stays MVP; creation Undo ends at publication; publication is not crash-atomic. Eight combat slots unchanged; utility dodge ingress unresolved. Live EOS externally blocked. M6-M9 not started.
