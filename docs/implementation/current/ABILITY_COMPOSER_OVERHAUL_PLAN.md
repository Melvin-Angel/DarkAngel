# Ability/composer overhaul implementation plan

Status: in progress (19 October 2026). No acceptance claim until integration checks.

## Inspected contracts
- Action source schema 1 is timing-only; schema 2 adds source/cooked clip closure. Blocks have stable numeric identities, 32-block limits, 1024 units per 60 Hz tick.
- Ability schema 1 owns base/continuation melee profiles and aim projectiles. Combat kits bind effects to ability + hit block. Continuations retain activation/cost/cooldown identity.
- AbilityState emits hit requests; WorldSession owns collision, deduplication, projectiles and effects. Preserve that route.
- NativeAuthoring owns multi-asset drafts, history and transactional cook/save. Isolated PosePreviews already sample draft composers and expose active block diagnostics.
- Ability currently uses a resizable table rather than the Scene/Game dockspace. Existing Content Browser and ImGui docking should be reused.
- Earth Light has punch/kick composers and looping continuation; Earth Spear has emergence/move/release references, a socket and a cooked model dependency.

## Implementation sequence
1. Add concrete, versioned composer gameplay payloads with strict validation. Resolve composer-owned hit data into the existing immutable combat request contract; reject ambiguous dual ownership. Keep legacy loading explicit while content is migrated.
2. Extend authoritative consumers through existing combat queries, with bounded work and explicit unsupported capabilities. Migrate only semantically equivalent content, preserving socket, effects and projectile preparation; report ambiguous/shared differences.
3. Give Ability its own persisted dockspace: Content Browser left, preview center, details right, timeline/block inspector independently docked below. Keep all top-level tabs.
4. Add atomic draft commands for animation-to-composer creation/assignment and base/continuation selection. Preserve drafts across selection and use existing save-all/history.
5. Use block-specific property drawers and isolated preview overlays; expose only supported semantics accurately.
6. Run focused native tests, editor/runtime build, guarded full suite, real editor smoke/capture, and document receipts/limitations.

## Risks and acceptance boundaries
- Shared actions with different profiles require explicit conflict detection or independent copies; never select one profile silently.
- Effect bindings and projectile visual dependency closure must survive migration; do not remove old fields until equivalent ownership is supported.
- Timeline presentation and authoritative outcomes remain separate. Safe preview sampling must never activate combat in the game world.
- Existing whole-suite actor-cooking closure failures must be distinguished from regressions in this change.
