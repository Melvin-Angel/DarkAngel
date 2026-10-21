# Ability and Composer authoring

Implementation checkpoint: 21 October 2026. Development is on hold at the user's request. See `docs/implementation/current/ABILITY_COMPOSER_OVERHAUL_REPORT.md` for verification and open failures. This document describes the implemented slice; the remaining work below is part of the requested overhaul, not accepted functionality.

## Ownership and execution

An Ability retains activation/input policy, costs, cooldown identity, tag requirements, cancellation, targeting permissions and the linear base/continuation sequence. Animation graphs continue selecting ongoing poses. Composers supply discrete clips, timing and block-owned hit geometry/power/evaluator data.

Action source schemas 1 and 2 remain readable. New optional block payloads have their own `schema: 1`; unknown fields, unsupported shapes, malformed offsets and invalid motion/period bounds are rejected. Stable numeric block IDs, deterministic JSON hashing, finite loops, 32-block limits, 1024 action units per 60 Hz simulation tick and existing event ordering remain unchanged.

The `hit` payload supports a sphere with offset, socket, radius, power, evaluator and damage type. A Projectile pairs it with typed speed/gravity/range/lifetime. An AoE pairs it with a positive pulse interval and implements an attached spherical aura. Box, capsule and arc shapes are not approximated as spheres.

Freezing compiles these payloads into the existing immutable combat-query profiles. The runtime-only `composer_owned` flag makes repeated freezing idempotent; it is not serialized in Ability authoring data. A legacy Ability profile and composer payload claiming the same block are rejected as ambiguous. Up to eight combat profiles per action remain supported.

WorldSession still owns achieved motor poses, hostile/liveness filtering, authoritative collision, evaluators, damage and effects. Hitboxes use the existing sphere query. Projectiles use the existing swept flight/impact loop and launch once per block/loop. Their launched lifetime follows the existing projectile policy rather than the lifetime of the animation. AoEs query once per action-clock pulse, including an initial pulse, and deduplicate each target independently; crossed pulses survive accelerated action clocks and end timing is exclusive. Cancellation stops further aura pulses and hit requests. Existing transactional resolution/work limits still apply.

Audio intervals now use the existing CuePlayer and ActionCueTracker lifecycle, including end/cut/replay handling. Their key resolves through the registered cue library to sound presentation. VFX, Cue, movement locks, invulnerability/tag policies, Commit and Combo Window retain their existing consumers. Typed Gameplay blocks apply their resolved self-effects once at block start through authoritative resolution. Empty legacy Gameplay blocks remain inert and are labeled accordingly; arbitrary event dispatch is not implemented.

## Designer workflow

1. Open Ability and select an Ability in the existing Content Browser on the left. Its creation controls can make an independent Ability from a template.
2. Edit policy in Ability Details on the right. Select Base or a continuation button to change the active composer without closing drafts.
3. In Animation / Composer, choose an imported animation and a relative `.daaction` destination, then Create Composer From Animation. The staged composer takes its duration from the cooked clip, uses one loop and presentation-only root policy, and is assigned immediately to the active step. Existing matching animation assignments are reused; source/draft name collisions are rejected.
4. Add Hitbox, Projectile, AoE, Cue, VFX or Audio tracks/blocks from the Timeline context menu. Gameplay blocks start with zero damage. Select a block to edit timing and supported typed settings in the independent Block Details window.
5. Use the isolated preview to orbit, play, pause, step, restart and scrub. Choose a compatible preview Character. Volume/path toggles show active socket-relative spheres, attached areas and projectile trajectories. An optional selected target Character is displayed in its rest pose as a visual reference.
6. Add a continuation by assigning an existing composer; remove the last continuation or enable the existing last-to-base loop policy. Each advancing step still needs an authored Combo Window. There is no branching runtime.
7. Save gameplay changes publishes all open native drafts through the existing transactional validation/cook path. Creation and field edits use native Undo/Redo/history. Animation creation and assignment form one Undo/Redo operation, including creation into a new continuation slot.

The five dock windows are Content Browser (left), Ability Preview (upper center), Ability Details (right), Ability Timeline (lower center) and Block Details (beside Timeline). The timeline has horizontal and vertical scrolling; Block Details has its own scrolling. Dock positions use the existing editor preference/layout persistence. Top-level destinations are unchanged. Optional combat-kit composition remains available as a separate window.

Independent Ability creation now copies the base and all continuation composers, migrates their legacy hit profiles into those copies, and clears composer power for a blank creation. Shared composer assignment remains explicit; editing it affects every referencing Ability. Use independent creation when gameplay should diverge. No implicit per-Ability damage overrides are introduced.

## Content migration

`scripts/migrate_composer_hits.py` is dry-run by default. `--ability` limits selection; `--apply` publishes equivalent conversions. Conflicting shared consumers and unsupported conversions are reported rather than discarded. Publication checks source bytes and rolls back on failure. Repeated migration is idempotent.

Royal District Light, Heavy, Bow, Earth Light punch/kick and Earth Spear now own their hit data and on-hit effect references in composers. Bow and Spear own projectile motion; Spear also owns its model and preparation/release references. Ability aim permissions and combo identity remain unchanged. Matching legacy kit bindings were removed. The migration report records seven composers, six hit payloads, five effect references, two projectiles and no migration warnings. Legacy loading remains available for unmigrated external content.

Effect/model references are resolved and generation-pinned by the native cooker, with source-independent cooked dependency closure. Editing a shared composer remains an explicit shared edit; independent Ability creation copies its composers. General information and AI hints are editor annotations, not new AI runtime behavior.

## Remaining requested work

- Richer shape/filter/hit-frequency/knockback controls and composer-owned movement-warp authoring. Current geometry supports spheres, attached spherical AoEs and bounded projectile motion.
- Projectile-model/VFX/audio playback in isolated preview and richer gameplay/debug simulation. Current overlays are safe visualization and do not run authoritative combat or mutate the game world.
- Faster live draft previews: preview publication can still snapshot the full source mount and is not qualified as immediate for all edits.
- Broad pointer/docking interaction and backend qualification. The current actual D3D12 workspace capture passed, but does not establish these interaction or gameplay guarantees.
- Resolve the full-suite failures recorded in the checkpoint report before treating the overhaul as accepted or release-ready.
