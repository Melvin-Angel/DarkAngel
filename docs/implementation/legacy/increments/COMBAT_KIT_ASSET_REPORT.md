# Frozen CombatKit asset increment — 9 October 2026

Native `.dakit` schema 1 now composes the existing CombatKit/WorldSession implementation with frozen input, attributes, abilities and a real compiled locomotion stance. M4/M5 remain **In progress**.

The canonical slot order is light, heavy, ranged_light, ranged_heavy, spell1, spell2, block, parry. Every slot references a distinct declared InputManager button action; ability UUIDs may be null, including an entirely unassigned kit. Abilities retain their own Pressed/Hold/Released/Tapped semantics. The kit references one input profile, attribute schema and graph stance. Assigned abilities must use that attribute schema. Source locators are typed and mount-relative; they are stripped from the runtime product. No equipment/mask semantics or final game damage formulas are added.

Composite inspection records the exact deduplicated transitive identities and all source/sidecar hashes. Conversion freezes the complete input/schema/action/ability/clip/rig/graph closure, rejects conflicting shared products and publishes through the existing atomic catalog transaction. Parent generations include frozen dependency hashes. Warm cooks skip conversion; rejected source/dependency edits preserve the prior head. Runtime loading only uses registry/CAS products, validates dependency generation fences and returns owned input/schema, immutable grants and compiled stance for the existing configure/equip boundary. The engine grant/operation/motor worlds are unchanged.

## Verification

Reproduce: `python scripts/verify_combat_kit_assets.py --build`. Exact gates and source/binary hashes: `evidence/combat-kit-assets.json`.

- 9/9 affected native checks: kit assets, graph assets/runtime, ability assets/commitment, combat-kit routing, attributes, input and asset pipeline.
- Primary full-editor build and kit fixture.
- Feature-disabled ability asset fixture and 4/4 ordinary Headless checks.
- The kit fixture checks embedded identity, optional/all-unassigned slots, input/schema validation, warm/fresh cook equality, source-free loading, failed-head preservation, nested-generation invalidation, mixed-package rejection and immutable leases. Cooked grants activate through WorldSession. Swapping to an unassigned cooked kit cancels the active grant, retains spent resources, changes the grant generation, rejects the stale input grant and selects the real frozen stance.

## Exact remaining gates

Next: freeze Action Composer clip/rig bindings and root policy with the timeline, then integrate the cooked kit and protocol-3 input/correction path into Royal's existing CharacterSceneSession. Royal still has locomotion only: no playable attack, target damage/Health presentation or kit/character authoring UI. The focused fixture is a real native API path but does not constitute playable editor integration.

Typed graph bool/int/float/enum parameters, character states/events, transitions, layer/additive/mask/action authoring, canonical animated socket sweeps, effects/tags and game-owned Luau callbacks/formulas/cleanup/credit remain open. Pending owner prediction/combos, observer actions/public resource visibility and late join, deferred reservations and health UI remain open. Full M4/M5 input lead/clock offset/jitter, RTT/loss, streaming/topology/lifecycle, bandwidth and performance gates remain open. Royal environment collision remains generated box proxies. M3 live EOS remains externally blocked. No new graphics, GNS or EOS qualification is claimed by this increment.


Latest action increment (9 October): [frozen action clip binding](ACTION_CLIP_BINDING_REPORT.md) adds schema-2 Action Composer clip/rig/root-policy cooking through the Ability/CombatKit closure and a bounded timeline-local root helper for the existing motor. 16/16 affected native checks, full-editor binding fixture, feature-disabled rejection and 4/4 Headless checks pass. Real Jolt wall collision and synthetic 30/60/144 traces are checked. Royal input/attack/target integration, pending prediction/observer state, authoring and full M4/M5 gates remain open. Reproduce `python scripts/verify_action_clip_binding.py --build`.
