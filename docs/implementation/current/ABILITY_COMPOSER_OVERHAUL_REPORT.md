# Ability/composer on-hold checkpoint — 21 October 2026

The user narrowed the final scope to compile fixes and requested committing and pushing the entire working tree before putting the project on hold. This is an implementation checkpoint, not acceptance of the complete original overhaul.

## Implemented

Composer-owned typed sphere hits, projectile motion/model/preparation, attached pulsed AoEs and resolved effect references integrate with the existing authoritative combat path. Gameplay blocks apply self-effects at start; Audio uses the existing cue lifecycle. Source/cooked decoding validates payloads and dependencies while retaining deterministic 60 Hz timeline behavior. Ability activation, costs, targeting and linear combos remain Ability-owned.

The Ability workspace preserves the Content Browser and top-level tabs, with preview above separate Timeline/Block Details docks and Ability Details on the right. Animation-to-composer creation and assignment are one Undo operation; combo buttons switch active composers while retaining drafts. Reusable property controls edit typed payloads. Isolated animated preview supports timeline playback and safe geometry/path overlays with an optional target character.

Royal District migration moved six hit payloads, five effect references and two projectiles across seven composers without migration warnings. Spear preparation/model references are included. See ABILITY_COMPOSER_MIGRATION.json and ../../architecture/ABILITY_COMPOSER_AUTHORING.md.

## Verification and remaining failures

- Final all-target native build passed: build/ability-composer-hold-build.log. The last change corrects an unsigned expected value in the timeline test; no outstanding compiler error was observed.
- Earlier complete integration receipt: artifacts/ability-composer-0ea2d58cb2/receipt.json. Build, all four production cooking checks and actual D3D12 workspace smoke capture passed; protected source hashes remained unchanged.
- Earlier full suite: 117/125 passed. Failures: M5.actor_assets (unchanged cache reuse), M5.composer_structure (signed expected value, corrected after that run), M5.character_combat_scene (flinch interruption/reconciliation), M8.party_scene, M8.scene_delay, M8.scene_transition, M6.navigation and M6.behavior_tree (missing resolved composer effect/evaluator). The complete suite has not been rerun after the assertion correction. These failures remain an explicit qualification gap, not claimed fixed.
- Native authoring's complete transaction fixture passed in the earlier focused and full runs. Its source timeout is now 1800 seconds to cover the measured approximately seven-minute run. The final focused rerun excludes automatic fixture setup and reuses that existing fixture; see build/ability-composer-hold-focused-tests.log. An initial redundant fixture rerun was stopped before verdict.
- Existing compiler warnings remain; no warning-free claim is made.

## Resume here

Resolve the full-suite failures above before release acceptance. Complete immediate draft-preview performance, richer shapes/filtering/knockback and composer movement-warp controls, presentation playback in isolated preview, and physical pointer/docking/backend qualification. Current preview is safe visualization, not authoritative combat simulation. AI hints are annotations. Legacy loading remains for external unmigrated content. No further overhaul development was performed after the user's scope reduction.

Final checkpoint results: all 8 focused native checks passed (59.13 seconds); all 3 Python migration tests passed. The previously failing timeline assertion now passes.
