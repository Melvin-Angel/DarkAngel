# Combat-kit and attribute primitives

9 October 2026, following 1f3d713. M4/M5 remain In progress; this is a native foundation increment, not playable combat or a Character editor.

## Implemented

`CombatKitDefinition` contains eight ordered slots (Light, Heavy, Ranged Light/Heavy, Spell1/2, Block, Parry), stable button-action bindings, optional ability UUIDs, a locomotion-stance UUID and immutable generation. Validation rejects invalid order, missing identity/stance, unknown/axis actions and duplicate action bindings. A kit owns no input-edge interpretation: the selected ability receives Pressed/Hold/Released/Tapped unchanged, including cancellation and duration.

`CombatKitInstance` copies definitions into immutable owner-local grants. Runtime replacement validates and stages the candidate before publishing, increments the grant generation, reports unique outgoing ability references and suppresses continued Hold/Released/Tapped until a fresh Pressed. Input batches tagged with an old grant generation are ignored. Player and NPC use the same primitive. No mask logic is included.

This router is not an executor: slot plus grant generation is required to distinguish grants of the same ability. The future AbilityState must prepare per-slot activation cancellation before kit publication; the returned ability list is not a substitute for activation handles. Cost refunds, effect/projectile lifetime and stance transition are not inferred. Input-generation tagging and native action/grant cleanup must be integrated into WorldSession before runtime gameplay swapping is claimed.

`AttributeSet` validates up to64 stable-ID resource/statistic definitions and256 owned modifiers. Statistics evaluate `(base + flat) * channel factors + post`, followed by priority/latest-sequence override and clamp. Bonuses add within a named channel and multiply across channels. Removing an owner recomputes the statistic from remaining contributions. Resources use stored deltas, reject temporary modifiers, and clamp against their derived maximum before publication. Multi-resource transactions aggregate repeated attribute deltas and publish atomically; insufficient costs or nonfinite inputs preserve prior values. Damage/healing can request clamping rather than cost underflow rejection.

Tests demonstrate Health/MaxHealth, Stamina, Essence, Poise, ElementalDefence and Block schemas. These are primitive examples, not final game balance, armour channels, immunity, stagger, block/parry or death implementation. Costs have no activation-ID deduplication/reservations yet and this primitive must not be exposed as an unvalidated client damage command.

## Verification and remaining work

Reproduce with `python scripts/verify_combat_foundation.py`. Receipt: [combat-foundation.json](evidence/combat-foundation.json). Focused tests cover optional/shared ability slots, edge preservation, independent player/NPC instances, immutable alias protection, failed replacement preservation, outgoing grant retirement, stale input suppression, modifier formula/ownership, override order, insufficient/aggregate costs, resource maximum clamping and nonfinite preservation. The full editor rebuild includes these same runtime sources. No editor layout changed or new rendering qualification was claimed.

Next: reflected native kit/ability/attribute assets and transactional cook, World-owned checked grants/activation handles, atomic action prepare plus costs/cooldowns/tags, action end/cancel cleanup, typed damage evaluator, and accepted/rejected operation integration through the existing WorldSession. Character authoring and script-driven graph parameters/events follow the [workflow plan](EDITOR_WORKFLOW_PLAN.md). Reservations, effects/tags, network fault/prediction gates, graph assets/transitions/layering/composer arbitration and motion-quality acceptance remain open. No full integrated suite, controller-hardware, GNS or EOS acceptance was refreshed.

Implementation continuation: the server-only WorldSession immediate-commit/action-lifecycle subset now extends these primitives; see [ABILITY_COMMIT_REPORT.md](ABILITY_COMMIT_REPORT.md). It does not implement predicted operations, damage/effects, composer rendering or editor authoring.


Implementation continuation (9 October): Latest asset increment (9 October): [frozen Ability/Attribute assets](ABILITY_ASSET_REPORT.md) adds versioned native sources, atomic action/schema cooking, dependency generation validation and source-free loading into the existing WorldSession grant/commit API. CombatKit/AnimationGraph cooking and reflected editor authoring remain open; Royal still has no playable hit/damage. M4/M5 remain In progress. Reproduce `python scripts/verify_ability_assets.py --build`.


Latest melee increment (9 October): [authoritative motor-relative melee](MELEE_REPORT.md) adds authored HitWindow profiles, a bound native post-physics query, registered game-owned damage evaluators, atomic Health/death/deduplication and existing Loopback baseline integration. Royal input/attack presentation, canonical socket sweeps, ability wire/prediction and the remaining M4/M5 gates are still open. Reproduce `python scripts/verify_melee.py --build`.


Latest kit increment (9 October): [frozen CombatKit assets](COMBAT_KIT_ASSET_REPORT.md) adds `.dakit` with eight optional ability slots, declared input actions and a coherent frozen input/attribute/ability/graph-stance closure. 9/9 affected native checks, the full-editor fixture and 4/4 ordinary Headless checks pass, including existing WorldSession active-grant replacement without refunds. Royal remains locomotion-only; Action Composer clip/root binding, playable combat, authoring, prediction/observer and full M4/M5 gates remain open. Reproduce `python scripts/verify_combat_kit_assets.py --build`.


Latest playable increment (9 October): [Royal native melee integration](ROYAL_COMBAT_REPORT.md) connects frozen kit/attributes/ability/action/clip assets to the existing CharacterSceneSession, serialized protocol-3 intents and atomic owner corrections, Jolt-achieved root motion, native melee damage and Game Health/Stamina display. Primary `scripts/open_royal_scene.ps1` uses the new RoyalCombat scene; other seven slots remain unassigned. 12/12 affected native checks, full-editor fixture, D3D12/Vulkan combat/active-pose captures and 4/4 Headless checks pass. M4/M5 remain In progress. Exact remaining gates are in the report. Reproduce `python scripts/verify_royal_combat.py --build`.

Usage stop (9 October): the requested threshold was reached: five-hour remaining 4%, weekly remaining 24%. Implementation stopped after this verified checkpoint. Next coherent increment is pending owner ability/cost/combination prediction with exact operation inclusion and dependent replay, followed by observer action/public state and active-state late join. Effects/tags/Luau composition, typed graph/layer/mask/socket/authoring, projectile/combo and full timing/fault/streaming/performance gates remain open. M3 live EOS remains externally blocked. No push or usage reset was performed. Obtain exact HEAD with `git log -1 --oneline`; the checkpoint tree should be clean.
