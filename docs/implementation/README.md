# Implementation docs

Start here. The folder was sorted on 14 October 2026 so the files in use are easy to find.

| Where | What it holds |
| --- | --- |
| [CONTINUE.md](CONTINUE.md), [STATUS.md](STATUS.md) | Running handoff logs. The newest checkpoint is the first paragraph; older checkpoints follow. |
| [current/](current/) | The active milestone and the refactor, plus references that still describe how to build and run the project. |
| [future/](future/) | Plans and backlogs that are not built yet. Nothing here is implemented until marked otherwise. |
| [legacy/](legacy/) | Finished work kept as history: milestone reports, per-increment reports, superseded plans and old session prompts. Accurate for when they were written, not maintained. |
| [evidence/](evidence/) | Logs, receipts and captures written by the gate scripts. Scripts write here by path, so it does not move. |

Tooling reads or writes these by path, so they stay at this level: [PACKAGING.md](PACKAGING.md) (installed by `cmake/install.cmake`), [M0_REPORT.md](M0_REPORT.md) and [STATUS.md](STATUS.md) (written by `scripts/write_m0_report.py`), `dependencies.json` and `notice-inventory.json` (`scripts/record_implementation.py`, `scripts/collect_notices.py`).

## current/

- [M8_REPORT.md](current/M8_REPORT.md): world state, inventory, scene transitions and co-op progression, the latest milestone. [M8_BRIEF.md](current/M8_BRIEF.md) is the user's scope for it.
- [REFACTOR_REPORT.md](current/REFACTOR_REPORT.md): the audit-driven refactor in progress, with step results, metrics and what remains. The task itself is [CODEX_REFACTOR_PROMPT.md](current/CODEX_REFACTOR_PROMPT.md), the findings it acts on are [CODEBASE_AUDIT.md](current/CODEBASE_AUDIT.md) and [BUILD_PERFORMANCE_REPORT.md](current/BUILD_PERFORMANCE_REPORT.md).
- [EXTENSIBILITY_AUDIT.md](current/EXTENSIBILITY_AUDIT.md): how hard a second game (for example a third-person shooter) would be on this engine, and what to change first.
- [BOOTSTRAP.md](current/BOOTSTRAP.md) and [LATER_DEPENDENCY_RECIPES.md](current/LATER_DEPENDENCY_RECIPES.md): how the toolchain and dependencies are reproduced.
- [ROYAL_SCENE.md](current/ROYAL_SCENE.md): the Royal District scene target the gates and hand tests use.

## future/

- [AUTHORING_NEXT_STEPS_2026-10-15.md](future/AUTHORING_NEXT_STEPS_2026-10-15.md): the current design-only handoff for prefab placement/swapping, gameplay configuration, UI authoring and the rendering/Assets workflows requested on 15 October. Links the detailed designs and their implementation gates.
- [ENVIRONMENT_LIGHTING_POST_PROCESSING_PLAN.md](future/ENVIRONMENT_LIGHTING_POST_PROCESSING_PLAN.md): cubemap/spheremap and procedural skies, Lighting/fog/baking tool, post-processing profiles and researched renderer prerequisites.
- [ASSETS_MATERIAL_EDITING_PLAN.md](future/ASSETS_MATERIAL_EDITING_PLAN.md): imported model material/texture editing, reimport-safe ownership and Assets > Edit Prefab routing.
- [PREFABS_AND_COMPONENTS_TODO.md](future/PREFABS_AND_COMPONENTS_TODO.md): high-priority prefab workflow and Add component.
- [EDITOR_UX_BACKLOG.md](future/EDITOR_UX_BACKLOG.md): the editor UX pass scheduled after M8 and before M9.
- [UI_PLAN.md](future/UI_PLAN.md): the older runtime-UI plan and 12 October deferral; the 15 October handoff above revives authoring and updates its stale implementation inventory.
- `PREFAB_WORKFLOW_PLAN.md` (this level, untracked) is a newer prefab proposal; it was left where it was written.

## legacy/

- [milestones/](legacy/milestones/): M1–M7 reports and M0/M1/M3 decision records. The systems they describe still exist; later milestones changed parts of them.
- [increments/](legacy/increments/): one report per feature increment (abilities, effects, Composer, graphs, authoring, targeting, senses, encounters, masks, HUD and so on), mostly 9–12 October 2026.
- [plans/](legacy/plans/): plans that were carried out or replaced (editor workflow, combat sandbox, M5 reaction presentation, design revision).
- [prompts/](legacy/prompts/): earlier continuation prompts and usage handoffs for Codex and Claude Code sessions.

Product vision and roadmap live outside this folder: [vision](../vision/DarkAngel_Engine_Vision.md), [architecture and roadmap](../architecture/DarkAngel_Implementation_Handoff.md), [actor gameplay design](../architecture/Actor_Gameplay_Design.md).
