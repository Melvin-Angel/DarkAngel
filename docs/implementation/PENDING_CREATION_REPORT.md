# Pending native creation and history — 10 October 2026

Base checkpoint: `272cdb3`. Evidence: [pending-creation.json](evidence/pending-creation.json); reproduce with `python scripts/verify_pending_creation.py --build`.

## Delivered boundary

Native creation/duplication now stages owned drafts with stable identities. Shared selectors include pending references without a second catalog. Character/Player creation, linked references, pre-publication Undo/Redo and isolated Character/effect/graph preparation use the existing native schemas and private AssetService candidate path. Sources and catalog remain unchanged until Save. Save validates and publishes pending sources together with edits and complete consumer closure; fresh Play rebuilds scene resources and starts isolated authoritative gameplay.

Creation Undo exists only before publication. Save retains published assets and removes their creation commands from history; ordinary edit commands remain reversible. Redo rejects externally occupied source paths. Immutable UUID/kind/schema edits are rejected. History and selectors refresh after travel.

## Verification

Seven focused gates passed: editor/native/actor build, native authoring fixture, actor asset fixture, D3D12 fresh Player Play, saved Character panel, saved Player panel and unpublished Character draft preview. Fresh Play reports Health75/Stamina90/pending0. Pending preview reports81 joints, absent source files, unchanged catalog and stopped gameplay. The final two form-refresh changes received an editor rebuild and repeated pending-preview capture; unchanged native implementation/test hashes retained their earlier passing receipts. Capture was visually inspected.

Native coverage includes linked pending references, source identity protection, occupied-path Redo rejection, publication/history pruning and existing failed-save/source-preservation checks. These are focused receipts, not broader multiplayer qualification or physical pointer automation.

## Remaining boundaries

Legacy direct AssetService creation remains immediate. Published asset deletion is not introduced. Publication retains documented crash-recovery and cross-process snapshot limitations. Full masks/equipment/reactions/NPC and production designers remain planned. M4/M5 stay In progress; live EOS blocked; M6–M9 not started.

Next: pending-draft reload/revert and selection usability, then focused creation/reference validation increments. Preserve complete Save/cook/scene closure/resource preparation/fresh Play.
