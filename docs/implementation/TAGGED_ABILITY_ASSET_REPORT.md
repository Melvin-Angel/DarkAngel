# Authored ability tag requirements and scene preparation

9 October 2026, following actor-tag checkpoint 1f8f0bd. M4/M5 remain **In progress**.

The existing `.daability` schema now accepts an optional paired tag-registry UUID and All/Any/None requirements, with a source-only `.datags` cook locator. Untagged sources preserve their existing contract. Tag validation shares the existing decoder; complete registry generation becomes part of the native ability generation and cooked dependency closure. Unknown/contradictory requirements, missing pairs, wrong UUID/type/path and stale dependency generations reject before publication. Failed cook preserves the previous usable head, and active definitions keep their frozen lease.

CombatKit derives one coherent prepared TagAsset through its assigned abilities, validates its frozen product with the existing complete kit manifest, and rejects multiple actor registries during cooking/loading. The shared TagAsset type is separate from effect and ability assets. No duplicate tag parser, catalog, source format or networking runtime is introduced.

Native CharacterSceneCombat carries the prepared dictionary. The existing preview loader supplies it from the cooked kit; CharacterSceneSession installs it on player and target before grants, and prepares both owner prediction and public observers with the same dictionary. Wrong binding or private prediction dependencies fail before gameplay is published. General live dictionary/schema migration still requires controlled actor reinitialization.

## Verification

66/66 complete optimized native tests and native/full-editor/feature-disabled tagged ability/kit/scene, tag/effect/Royal/observer/prediction/Headless, D3D12/Vulkan and GNS compatibility pass. Run `python scripts/verify_tagged_ability_assets.py --build`; evidence/source/binary hashes are in `evidence/tagged-ability-assets.json`. Extended native/full-editor/feature-disabled ability fixtures cover authored tag closure, source-free load/application, unknown/contradictory/path rejection, dependency-only generation change and mixed-generation rejection. CombatKit fixtures verify typed tag dependency closure and source-free preparation. The real native combat scene fixture verifies that authority, owner prediction and public observers consume the same prepared dictionary while gameplay remains isolated from authoring. The complete product/regression chain is refreshed.

## Remaining gates

Reflected designers, generated C++/Luau constants and checked Luau composition remain required. Primary Royal sources retain the current playable light-attack profile; this increment enables typed authored binding, not new Royal dodge/status/combo/projectile gameplay. Action-owned invulnerability/movement tokens, effect instance/timer/credit/cue replication, effect-aware owner presentation, reservations, slow-to-motor policy, typed graph/event/layer/mask/socket/authoring and full clock/fault/streaming/performance/RmlUi gates remain open. M3 live EOS is externally blocked. M6 has not started.
