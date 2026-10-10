Current pending creation increment: [PENDING_CREATION_REPORT.md](PENDING_CREATION_REPORT.md). Seven focused gates pass for staged native creation, shared pending references, pre-publication Undo/Redo, isolated preview and complete fresh Player Play. Published creation is retained and leaves creation history. Next: pending-draft reload/revert and selection usability. M4/M5 In progress; EOS blocked; M6–M9 not started. Older continuation entries below are historical.

Resumed after the usage reset from `af42b3c`: graph topology checkpoint `9c8062f` is delivered; current [history inspection report](AUTHORING_HISTORY_VIEW_REPORT.md) and STATUS/CONTINUE describe the next completed increment. Older usage/checkpoint observations below are historical, not the current stop instruction. Continue toward10% remaining with owned local commits.

# Current authoring usage handoff — 10 October 2026

Native feature checkpoint: `1ae1a607644e64d759f3feb69669091c13a2fea3` (`1ae1a60`, master). Observed clean tree at 06:04:51 UTC, 23 commits ahead of locally stored origin/master; no fetch or push. This documentation handoff is committed immediately after that feature checkpoint; inspect Git for its own commit and any subsequent local work. Initial session checkpoint was `5c6b52d6690cb3dfbd832fed578ef47b42309412`, clean and 11 ahead. User subsequently explicitly authorized implementation and local commits, stopping near 10% five-hour remaining. The documentation-only design revision was committed as `806010f`; implementation receipts below belong to the later authorization.

## Sources of truth

[Final vision](../vision/DarkAngel_Engine_Vision.md): finished product and fifteen fixed workflow targets. [Roadmap](../architecture/DarkAngel_Implementation_Handoff.md): milestone ownership/dependencies/acceptance. [Actor decisions and gameplay traceability](../architecture/Actor_Gameplay_Design.md): confirmed design and unresolved policies. Locked Decision_Log TXT/DOCX remain unchanged. [STATUS](STATUS.md): milestone table and current scoped receipts. [Workflow plan](EDITOR_WORKFLOW_PLAN.md): incremental editor capabilities. [Ability continuation](ABILITY_AUTHORING_CONTINUE.md): immediate authoring instructions. Historical AUTHORING_CHECKPOINT_REPORT and acceptance evidence remain untouched; older continuation paragraphs are chronological evidence, not instructions to restart delivered work.

## Completed checkpoints and focused evidence

| Checkpoint | Delivered boundary | Report |
|---|---|---|
| e0bb295 | Shared draft-aware path/UUID/reference search and resource-only cost picking | [Reference picker](REFERENCE_PICKER_REPORT.md) |
| 9564097 | Ordinary evaluator-free native effect Apply/advance/remove/expiry preview in isolated state | [Effect preview](EFFECT_PREVIEW_REPORT.md) |
| 3852040 | Presentation Character and separate Player loadout assets, frozen rig/closure validation | [Actor assets](ACTOR_ASSET_REPORT.md) |
| a57e167 | Character/Player creation, references/history and complete authored fresh Play | [Actor workflows](ACTOR_WORKFLOW_REPORT.md) |
| 82a029c | Independent authored skin/rig/clip preview, playback and read-only skeleton/socket inspection | [Character preview](CHARACTER_PREVIEW_REPORT.md) |
| 7da60d2 | Player starting attributes resolved separately from target defaults | [Player defaults](PLAYER_DEFAULTS_REPORT.md) |
| 928dea5 | Confirmed/predicted gameplay inspection and available tag/effect source relationships | [Game diagnostics](GAMEPLAY_DEBUG_REPORT.md) |
| cd99727 | Native Input Mapper/history, optional frozen Player profile and unchanged kit semantics | [Player input](PLAYER_INPUT_REPORT.md) |
| d317199 | Existing graph node/reference/parameter/point/triangle forms and frozen consumer Save/fresh Play | [Graph authoring](GRAPH_AUTHORING_REPORT.md) |
| a0eb75e | Isolated Character/graph measured-state preview through native graph/RigPose fixed ticks | [Graph preview](GRAPH_PREVIEW_REPORT.md) |
| 1ae1a60 | Contextual looping clip picker with malformed metadata diagnostics | [Loop references](LOOP_REFERENCE_REPORT.md) |

Each report links or names its scoped JSON/log/capture receipts. Latest checks: loop-reference build/native/D3D12 panel, all three passing; graph preview four passing gates including exact native sampling, repeated/bounded scrub, failed parameter clock/phase retention, invalid preparation/source retention and tick0/20 rendered captures with81 joints. Graph authoring four passing gates include invalid cycle rollback, old plan retention and complete fresh Player Play. Player input six passing gates include Keyboard.K -> native light action, target Health75/Stamina90/pending0 and compatible profile preservation. Player defaults separately showed owner Health80/Stamina60 -> Stamina50 while target Health75. These are scoped results, not original milestone acceptance completion. Captures inspected; no physical popup/control interaction claim.

Current reusable fixture is `build/m5-editor-relwithdebinfo/authoring-fixture.json`, regenerated by NativeAuthoringTests. Preview verification requires its `characters/gui_character.dacharacter` fixture: run `python scripts/verify_graph_authoring.py` first if missing, then `python scripts/verify_graph_preview.py`. Do not copy fixtures into game content or assume one stale registry proves fresh authored sources. Use `python scripts/verify_loop_reference.py` for contextual picker work. Build/profile remains m5-editor-relwithdebinfo, scoped MSVC environment and normally D3D12; no new dependencies.

## Implement first after reset

Read this handoff and current status; inspect Git and Codex usage. Preserve changes. Next coherent increment: construct/edit native graph nodes, blend points and triangle topology through explicit history commands, with bounded IDs, reference/loop/rig validation and invalid-cycle/consumer preservation. Then improve asset creation/history usability. Reuse existing schemas and UI selectors rather than building another graph/action runtime. Use the smallest meaningful native fixture and one backend; rerun broader fault/combo/performance matrices only at their integration checkpoints.

Character remains reusable presentation only. Player currently owns kit, starting values and optional input profile; common loadout currently contains kit/starting values. NPC, equipment/masks, movement/camera/progression configurations remain planned. Four masks/equipped-versus-selected grants, gameplay tags/events, reaction priorities and tag/parameter graph transitions remain the confirmed product direction, with unresolved details in the actor design supplement. Do not start a monolithic mask/AI/reaction rewrite merely because it is in the final vision.

Always preserve eight canonical kit slots, WorldSession authority, native tag/attribute/effect/action contracts, generated-tag rebuild fences, stable IDs and AssetService/catalog/CAS. Save validates and cooks the complete relevant closure; fresh Play rebuilds full scene resources and creates an isolated runtime. No kit-only swap into stale scene/rig/animation resources. Creation is currently outside Undo; coordinated publication is not crash-atomic or a cross-process multi-source snapshot. Game tag provenance is available replicated/action data, not a full private grant ledger.

M0–M2 verified initial scopes are unchanged. M3 live EOS remains externally blocked; offline receipts are not live qualification. M4/M5 remain In progress with original multiplayer/fault/performance/content gates outstanding. M6–M9 remain Not started. No original acceptance receipts, locked decisions or external Unity assets were rewritten. No subagents, dependencies, push or reset consumption. Locally commit only completed owned chunks, maintain current continuation/report/status, stop near10% remaining and update the existing after-reset follow-up to the reported next reset.

## Documentation and file maintenance

Canonical vision changes only for product changes; milestone planning changes for delivery/acceptance, status for evidence and continuation for immediate work. Documentation revision files and M1–M9 changes are listed in [DESIGN_REVISION_REPORT](DESIGN_REVISION_REPORT.md). Ownership conflict corrected there: Character no longer intrinsically owns kits/stats. Air versus Unity Storm naming, loadout/schema/grant identity, kit/mask precedence, event/reaction policies and final evaluator design remain unresolved; original deliberate milestone ownership is preserved.

Implementation added actor assets/cooking, effect and gameplay-debug helper headers, focused native tests/verifiers and individual reports/receipts. It modified AssetService, editor source/history/forms/workspaces/preview/controller/launcher and the native CharacterScene owner/target default preparation. Runtime code was changed only after explicit implementation authorization. Git diff from the initial checkpoint is the exact file inventory; evidence files are listed by each verifier rather than duplicated here. No commit was pushed.

Usage stop observation: five-hour used90%, remaining10%; reset reported 10 October2026 06:11:27 UTC (08:11:27 Copenhagen). Existing `darkangel-authoring-after-usage-reset` follow-up was moved to08:13 Copenhagen, after the reset. No usage reset credit was consumed. Final documentation checks: eight authoritative/continuation documents have no missing local Markdown links; Git diff confirms locked TXT/DOCX and historical AUTHORING_CHECKPOINT_REPORT unchanged; `git diff --check` passes. No further runtime tests were needed for this documentation-only handoff.
