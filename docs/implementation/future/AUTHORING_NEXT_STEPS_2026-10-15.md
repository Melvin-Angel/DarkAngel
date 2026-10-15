# Authoring plans and roadmap entry point

15 October 2026. **Design index; implementation evidence is linked in the current checkpoint below.** The original roadmap revision was documentation only. The [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) is the single authority for recommended development order. This document indexes technical designs and preserves A–J feature traceability; it does not set a second schedule.

## Current checkpoint and status

The refactor and agreed scene-scale implementation are complete, excluding explicit deferrals. Preserve the completed results in [REFACTOR_REPORT.md](../current/REFACTOR_REPORT.md) and [SCENE_SCALE_PLAN.md](../current/SCENE_SCALE_PLAN.md), including phases 0–5, 7 and 8 (with 3b). Scene-scale phase 6 GPU instancing, reduced-feature build verification, cross-machine testing, EOS validation and optional walkable-ground reachability warnings remain deferred in the main roadmap. Historical subsystem qualification remains open where recorded; same-machine GNS is not remote/EOS proof.

R1 Scene navigation is **Implemented and locally verified**, as recorded in the [R1 implementation report](../current/SCENE_NAVIGATION_REPORT.md). R2–R8 and the playable sandbox gate remain **Planned**. R2 editor-wide UX overhaul is the next recommended major milestone; this index does not implement it. Read [STATUS](../STATUS.md), [CONTINUE](../CONTINUE.md) and the latest owning report for evidence; older paragraphs are historical.

## Detailed plans and old feature IDs

| Legacy feature | Current owner | Design and acceptance criteria |
| --- | --- | --- |
| Scene navigation (no A–J ID) | R1, implemented / locally verified | [UX backlog](EDITOR_UX_BACKLOG.md), including orbit/fly/pan/zoom/speed/focus and picking/placement input gates |
| Editor-wide UX (no A–J ID) | R2, phases 2.1–2.3 | [UX overhaul plan](EDITOR_UX_OVERHAUL_PLAN.md): audit, shared standards, existing windows and cross-window flows |
| A – Movement configuration | R3 | [Gameplay configuration](GAMEPLAY_CONFIGURATION_DESIGN.md): shared Player/NPC profile, source/cook and authority/prediction/replay |
| C – Useful prefab loop + F – Publication completion | One complete R4 | [Prefab proposal](../PREFAB_WORKFLOW_PLAN.md), [placement and Swap](PREFAB_PLACEMENT_DESIGN.md), [original request checklist](PREFABS_AND_COMPONENTS_TODO.md) |
| G – Assets/material workflow | R5 | [Assets/material editing](ASSETS_MATERIAL_EDITING_PLAN.md): stable material assignments, texture replacement, reimport and Edit Prefab routing |
| B – UI editor MVP + D – Gameplay UI | R6.1 + R6.2 | [UI authoring MVP](UI_AUTHORING_MVP_DESIGN.md): U0/U1 foundation/editor, U2 gameplay; [older UI plan](UI_PLAN.md) kept for requirements history |
| H – Sky/Lighting + I – Post processing + J – Baking | R7.1 + R7.2 + R7.3 | [Environment/rendering plan](ENVIRONMENT_LIGHTING_POST_PROCESSING_PLAN.md): staged renderer support and acceptance; probes later |
| E – World puzzle UI | R8 | [UI authoring U3](UI_AUTHORING_MVP_DESIGN.md): UI Surface, offscreen renderer, ray input and host-approved puzzle adapter |
| Tiled navigation (no A–J ID) | Future backlog, unscheduled, low priority | [Tiled navigation plan](TILED_NAVIGATION_PLAN.md): Recast tiled baking, multi-tile Detour, incremental rebuild and streaming readiness |

The sandbox gate after R6.2 is specified in the main roadmap: third-person Player, small authored environment, reusable prefabs, basic NPC, interactables, minimal gameplay UI and edit → save → cook → Play without manual data manipulation or hardcoded demo paths. It is integration/usability validation toward historical M9, not a new large game or release qualification.

## Technical contracts retained

- Extend existing assembly, EditorDocument, AssetService, native history/publication and frozen Play resources; do not introduce parallel runtime formats, asset catalogues, gameplay authority or draft owners.
- C and F stay together as a usable end-to-end prefab milestone. Add Component/Model, external links, mount transforms, overrides/Revert, compatible Swap, Apply Selected and retained-identity conversion all need their existing acceptance gates. Nested/variant/in-context/multi-object authoring remains later.
- Movement keeps exact legacy defaults and shared deterministic parameters. UI keeps authored data separate from native/Luau runtime behavior and approved gameplay requests. Preview/Play coexistence requires shared RmlUi lifetime.
- Imported material overrides remain engine-owned and stable-keyed across reimport. Rendering/baking work exposes only real supported capabilities and preserves HDR, per-view and immutable candidate/publication boundaries.
- [Engine vision](../../vision/DarkAngel_Engine_Vision.md), [actor gameplay design](../../architecture/Actor_Gameplay_Design.md) and [DAE decisions](../../architecture/Decision_Log.txt) retain their ownership roles. No locked decision or dependency pin changes here.

## Superseded scheduling and unresolved decisions

The earlier A→B→C→D→E→F→G→H→I→J order, separate G–J stream and instruction to start A then B are superseded by the main roadmap. The older UI plan's deferral has been lifted by the renewed request; its current-state inventory and stage numbers are historical. The prefab TODO's “mostly editor” estimate is qualified by the actual external-link, transform, gameplay-patch, identity and coordinated-publication work in the newer designs.

Before implementation, resolve the owning plan's open contracts: prefab schema/link refresh/retained identity/Swap compatibility and publication recovery; movement ranges; material key repair and skinned parity; UI bundle/binding/input/Luau host; HDR stages and bake backend. The proposed `.daprefab`, fixed isolated workspace and one-entity first scope are recommendations, not locked decisions. The UX audit must settle exact controls/layout/preferences/shortcuts with evidence.

## Resume and verification discipline

Start from the main roadmap and current checkpoint, check working-tree ownership and active build/editor processes, and use the refactored narrow service boundaries. Detailed plans retain local implementation steps; those steps do not override project order. Preserve protected Royal scenes/records, existing content, pinned build/dependency state and failed receipts. Do not push without explicit authorization.

Use focused existing tests and one authoring task per visible increment. Broaden at resolver, prediction/protocol, publication or renderer integration boundaries. Record exact remaining work and qualifications when implementation starts. This planning pass checks document links, references, retained requirements and consistency only; it runs no build or editor.
