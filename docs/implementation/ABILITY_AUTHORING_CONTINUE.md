# First usable ability/effect authoring loop

User priority, 9 October 2026: begin making abilities with gameplay effects synced over the network in the engine. Continue this vertical slice before broadening combat features or repeating the complete historical verification chain. M4/M5 remain In progress; this is an early authoring gate rather than M6 acceptance.

## What already works

Frozen native action/clip/ability/attribute/tag/effect/kit assets; input routing; authoritative hit validation and atomic damage/status transactions; resource costs, cooldowns and deferred commit claims; owner prediction/correction; public effect/tag/timer/credit state; persistent cue reconstruction; and real offline GNS joining an active effect state. Royal has playable light/heavy, early/late windup cancellation and Burn. Generated native/Luau tag constants use registry-generation fences. The complete checkpoint `74717c7` passes 74 native tests and the linked multi-profile/provider/backend checks.

The latest implemented increment moves on-hit composition into optional `.dakit` `effect_bindings` records: `ability`, `hit_block`, `effect`, `power`. The ability must occupy a canonical slot; the block must be one of that ability's native melee profiles; the effect must belong to the frozen kit catalogue. References, duplicate bindings, finite magnitudes and work limits are validated. Royal's game evaluator resolves the prepared records instead of selecting Burn by a special damage type. Modifier-only effects need no game execution evaluator; execution evaluator 2 currently means game-owned Health damage. New formulas still need a registered evaluator.

## Delivered first loop (9 October)

From c4072bd, the editor now has an embedded Ability workspace: source browser, Ability/Effect/action forms, compatible typed asset fields, native Create from template/Duplicate, canonical kit assignment and on-hit effect composition. Save invokes optimistic native source commands and cook. Every subsequent Play cooks the selected kit and complete scene/model/skin/collision closure, prepares fresh GPU/input/CharacterPreviewResources and an isolated native probe, then starts a new WorldSession-backed Play world. Unsaved gameplay drafts block Play; failed candidates retain the prior runtime registry/resources. Read [NATIVE_AUTHORING_REPORT.md](NATIVE_AUTHORING_REPORT.md) for the exact boundary.

Launch `powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1 -AbilityWorkflow`. Select a native ability/effect/action, create from its compatible template or duplicate, edit, assign its kit slot and bind an effect to a hit window. Save gameplay changes, switch to Game, Play and hold RMB through Heavy's windup. Stop before the next edit. External assets stay untouched; draft sources never patch a live authority world.

The combined edited fixture proves cost20, direct damage40 and Burn7/period30/duration120: Health60 and Stamina80 with Burn active at30; Health39 and expired Burn at150. Native owner/public paths converge and old prepared resources remain unchanged. Separate localhost GNS processes load the edited frozen effect and prove owner/public identity/timers, three periods Health100->79 and one cue lifetime after caster retirement. This is scoped status transport evidence, not remote graphical/Jolt melee qualification. The full-editor D3D12 scripted commands, rendered Ability/Effect/action forms, original light regression and animation-disabled create/edit safeguards are checked by `python scripts/verify_native_authoring.py --build`; receipt `evidence/native-authoring.json`.

## Next coherent increments

1. Add source-edit undo/redo and coordinated related-source transactions. Current Save validates/cooks each changed source in action/effect/ability/kit order; successful earlier commands remain if a later one fails, with an explicit diagnostic. Create is non-overwriting and rollback-safe, but this is not cross-process source/catalog atomicity. Create from template retains compatible schema/action references; duplicate a shared action before changing its timing independently.
2. Extend the initial action forms into visual Composer/character-preview authoring: structural block creation/removal, validated hit/commit editing and animation inspection using the existing graph/rig/clip services. Current forms edit existing timeline values and native clip references. Add compatible thumbnails/tags and preserve selection context through the shared asset infrastructure; do not replace existing native source formats.
3. Broaden the authoring loop to modifier/status presets, Character/Player kit selection and explicit tag-schema rebuilt-consumer handling. Existing compiled tags can be selected; new definitions still require generated constants and rebuilt native consumer validation. Additional self/on-commit/projectile/area triggers and custom formulas remain separate runtime increments.

The smallest first authoring loop is now delivered. These follow-ups should deepen that workflow without delaying it for unrelated combat features, M6 or complete M4/M5 qualification. Full required gates below remain open.

## Checks for this goal

Use focused native source-edit/rollback, frozen reference, changed effect magnitude and fresh-Play tests. For the UI increment, exercise create/edit/save/cook/Play on one backend and confirm one remote effect happy path. Run the complete milestone matrix at an actual integration/acceptance checkpoint or when a changed shared protocol/provider/physics boundary warrants it. Do not recursively run every historical verifier after an isolated form or binding edit.

## Still required for full M4/M5

Utility dodge routing and invulnerability intervals; authored Slow/Stagger-to-motor and equipment-handle policies; scalable charge, combo/projectile/area abilities; typed graph states/events/layers/masks and animated sockets; complete reflected designers and checked Luau gameplay composition; cue visual/audio and one-shot confirmation; graphical RmlUi; clock offset/jitter/input lead/redundancy; full four-owner RTT/loss/duplication/reordering/burst/streaming/lifecycle/provider/graphical qualification; bandwidth, allocation and frame-performance evidence. These should not delay a working first authoring loop. Live EOS remains externally blocked by deployment/policy and two identities. Nothing should be pushed without explicit Git-push authorization.
