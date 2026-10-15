# Editor UI/UX overhaul

15 October 2026. **In progress: source audit and shared-system/Scene/Game pilot.** This is the dedicated **R2** milestone in the [authoritative roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026), after R1 Scene navigation. It owns audit, standards and usability work; the roadmap owns project order. [UX backlog](EDITOR_UX_BACKLOG.md) retains user requests and navigation detail.

The objective is to make existing authoring workflows usable, consistent, discoverable and efficient. Measure successful tasks, avoidable clicks, context switches, input conflicts and recovery clarity. A visual restyle alone cannot close this milestone.

## User principles (15 October 2026)

- **The most relevant things in each panel are shown when you switch tabs.** A tab opens on its primary task: its editor and preview prepared and in view, not a creation form, empty state or diagnostics.
- **Previews prepare themselves and stay visible**, refreshing as drafts change; no Prepare buttons.
- **Shortcuts that route you from one tab to another are preferred over embedding one tab's functionality in an unrelated tab** (Open / Edit, Reveal, Edit in Ability and similar routes to the owner).
- **Keep UI testing proportionate.** Once a UI behaviour works, its gates (`scripts/verify_editor_ux.py` and the other editor capture gates) are run when editor UI code changes, not in the routine verification pass for gameplay work. They are not part of CTest.

## Concept direction and current implementation

The attached concept was inspected on 15 October before changes: near-black surfaces with dark-blue undertones, clear panel separation and restrained blue selection/focus. Scene prioritizes viewport with supporting Outliner/Inspector and tabbed lower tools; Game uses its own viewport-first debugging layout. Other workspaces retain task-specific authoring surfaces. Extend `editor_theme`; do not interpret scene lighting/material imagery as permission for R3-R8. See [R2 report](../current/EDITOR_UX_OVERHAUL_REPORT.md) for the audited layout map, exact tokens, findings, implemented/verified/remaining split and preference policy. The checklist below remains open until its workflow evidence is recorded.

## Scope and evidence rules

Audit every existing workspace, panel, popup and auxiliary window. Improve existing functionality and shared controls. Prefab and material surfaces are included in the audit and standards, but their missing feature implementations belong to R4/R5. Future UI and Lighting windows must adopt these standards at R6/R7; R2 does not build them. Do not fill Project Settings with new gameplay systems as UX work.

Use these finding states: **Source-confirmed**, **Observed in a recorded author session**, **Needs investigation**, **Planned change**, **Implemented, awaiting validation**, **Accepted**. Record source symbol/path and revision for source findings, or reproduction steps and capture/receipt for observed problems. Absence from one inspected function is not proof of editor-wide absence. The inventory below is a source orientation, not a completed editor-wide audit or a claim that every workflow has friction.

## Initial source inventory (inspected 15 October)

Source baseline: repository HEAD `7127d84`; source working tree was clean during the initial documentation audit. **Subsequent R1 implementation supersedes the fixed-Scene-camera finding below**: see [R1 report](../current/SCENE_NAVIGATION_REPORT.md). The inventory remains a dated audit starting point; recheck all findings against the implementation revision used for R2.

| Surface / source | Confirmed current behavior | Audit implication / proposed outcome |
| --- | --- | --- |
| Scene renderer: `apps/editor/render/scene_viewport.cpp`, scene `XMMatrixLookAtRH` branch | Scene view uses eye (26,24,34), target (0,1,0); Play and preview branches select other cameras | R1 delivers navigation first. R2 checks discoverability and interaction with gizmo/picking/Place rather than duplicating navigation implementation |
| Scene Hierarchy (“World Outliner”): `apps/editor/editor_ui.cpp`, outliner block | Searchable clipped placement list, single `editor.selected`, object context menu with Delete; ID suffix shown | Assess find/select/focus/source-navigation tasks; preserve existing search and clipping. Multi-selection/group hierarchy is not assumed to exist |
| Inspector: `editor_ui.cpp`, Inspector / `draw_components` / `draw_gizmo` | Edit-destination combo, reflected numeric fields, collapsible gameplay sections, Undo/Redo, script/provenance data; scene edits disabled during Play | Check scope comprehension, field units, gesture history and disabled reasons. Add Component/Model and linked-instance actions are R4 functional work |
| Assets: `apps/editor/editor_assets.cpp`, `Shell::draw_assets` | Path/UUID search, kind filters (some path-based), generation/import/cook status, Open authoring via explicit extension allowlist | Unify reference navigation and type routing. The inspected allowlist excludes `.daitem` while `open_native_asset` supports item; investigate/repair that route and prevent drift |
| Shared native picker: `apps/editor/reference_picker.hpp`; `editor_authoring.cpp`, `picker` | Shared catalog candidates, search and compatibility diagnostics already exist; Scene NPC/audio combos also implement local inventory loops | Reuse the existing foundation. Consolidate adapters where behavior is equivalent; verify special compatibility needs before replacement |
| Console: `apps/editor/editor_workspaces.cpp`, `Shell::draw_console` | Iterates console lines and displays severity icon/text; this function supplies no search, severity toggles or source navigation | Add useful filtering/copy/navigation to this shared console surface; measure diagnosis of a real validation error |
| Game: `editor_workspaces.cpp`, `Shell::draw_game`; `apps/editor/gameplay_inspector.cpp` | Play/Stop, Pause/Resume, Step, players, input focus hints; Game Profiler, Gameplay Inspector, Input Monitor and Console | Preserve these controls. Audit state clarity, input release, debugging navigation and preparation feedback; no claim they are absent |
| Settings/Tools: `editor_workspaces.cpp`, `Shell::draw_auxiliary` | Editor/Project Settings explicitly say settings will be added; Tools routes Console, history, input, navigation, saves, script source and runtime phases | Design useful preference groups and clear empty/unsupported states. Retain actual tool functions; audit each separately |
| Layout: `apps/editor/render/render_system.cpp`, initialization; dock builders in `editor_ui.cpp` / `editor_workspaces.cpp` | Docking enabled, `IniFilename = nullptr`; Scene/Game docks are programmatically seeded | Standard ImGui automatic layout persistence is disabled. Plan explicit per-user workspace persistence/reset and verify restart; do not assume current dock state survives |
| Shared styling: `apps/editor/editor_theme.hpp` / `.cpp` | Existing DPI-aware theme and icon helpers | Extend current theme/components; an editor-wide design system is not a second UI framework |
| Workspace enum/routing: `editor_ui.hpp`, `Workspace`; `editor_workspaces.cpp`, `open_native_asset` | Scene, Assets, Character, Player, NPC, Ability, Animation, Game, Tools, Settings, VFX, Audio and Items; Mask Editor is auxiliary; no Prefabs/UI workspace | Audit delivered surfaces now. Specify integration contracts for future surfaces, labelled unimplemented |

## Phase 2.1 – Audit and shared design system

Deliver a checked inventory, evidence-backed findings register and a reviewed design specification. Before changing a surface, reproduce its primary task and record the baseline. Source inspection supplies hypotheses for interactive testing, not fabricated click counts.

### Actionable audit checklist

- [ ] Inventory **all** workspaces: Scene, Assets, Character, Player, NPC, Ability/Effect, Animation/Composer/graph, VFX, Audio, Items, Game, Tools and Settings. Include Mask Editor and embedded editors; record where component/prefab/material inspection currently stops.
- [ ] Inventory every Tools destination: Console, Authoring history, Input bindings, Navigation, Saves, Script source and Runtime phases; inspect their callbacks and owning modules, not just the menu.
- [ ] Inventory import/create/duplicate/delete dialogs, reference pickers, context menus, preview controls, validation/progress/error surfaces and existing settings/project configuration entry points.
- [ ] For each surface record functionality, data/draft owner, entry/exit path, main tasks, source location, selected item, history owner, Save/cook/Play behavior and dependencies.
- [ ] Walk primary tasks with an author: select/edit an object; find/open/assign an asset; import/repair; create/duplicate/edit/save a native asset; place/move/delete/Undo; run/pause/step/debug/Stop. Capture steps/clicks, navigation and failures.
- [ ] Identify unnecessary navigation/clicks, unclear labels or edit scope, missing common controls, inconsistent selection/focus, hidden actions, unsupported-state explanations and recovery. Keep unobserved concerns as investigation items.
- [ ] Compare search/filter/picker implementations, dirty/save/history headers, preview controls and feedback. Identify shared components with equivalent semantics; retain specialized adapters where needed.
- [ ] Audit keyboard-only paths, text-entry shortcut suppression, mouse capture/release, drag thresholds, gizmo/placement/camera conflict, DPI scaling, narrow windows, long names and empty/large results.
- [ ] Test workspace switching/restart, dirty drafts/external changes, failed preparation, Play/Stop/scene changes and cancellation. Distinguish per-user preferences from authored assets/runtime state.
- [ ] Rank each evidenced finding by blocked task, frequency, recovery/data risk and implementation dependency; assign a phase, owner and acceptance scenario. Mark already-working controls for regression preservation.
- [ ] Review shared standards and pilot one existing form plus one reference picker before broad migration. Record remaining questions with owners and decisions.

Finding template:

| ID / state | Surface / task | Evidence and reproduction | User impact / baseline | Proposed change / priority | Owner / dependencies | Acceptance / result |
| --- | --- | --- | --- | --- | --- | --- |
| UX-001 / Source-confirmed | Workspace restart | `render_system.cpp`, `IniFilename = nullptr`; dock seeding above. Interactive restart check pending | Persistent workspace preference requested; no measured baseline yet | Explicit save/restore/reset, P0 | Shell/preferences; 2.1 policy then 2.2 implementation | Rearrange Scene/Game, restart, restore; reset without changing content |

### Shared design standards to finalize in 2.1

These are proposed standards, not claims about delivered controls. Record precise tokens and command mappings in the phase deliverable, then apply them consistently.

| Area | Required convention |
| --- | --- |
| Layout / hierarchy | Fixed workflow destinations; consistent document header (name, type, source/scope, dirty/validation state), primary toolbar, browser/sidebar, editing area and preview. Resize gracefully; essential actions remain reachable at supported DPI/window sizes. Preserve specialized task layouts |
| Visual hierarchy / spacing | Extend `editor_theme` with shared DPI-scaled spacing, padding, field widths and semantic colors/icons. Keep label/value alignment, readable contrast, keyboard focus and explicit text for status; severity must not rely on color alone |
| Collapsible sections | Stable component/section names and IDs, predictable defaults, remembered per-user open state. Put primary task fields first; keep diagnostics available without overwhelming normal editing |
| Search / filtering | Visible search and Clear, consistent name/path/AssetID matching where available, typed filters, result counts and distinct empty/loading/invalid states; stable selection through refresh. Do not hide missing references silently |
| Asset selection | Reuse `reference_candidates` and native inventory. Shared picker supports compatible candidates, current/missing reference, Clear where legal, Open/Edit, Reveal and shared/local destination. Selection and assignment are distinct actions |
| Context menus / drag-and-drop | Context actions agree with visible toolbar commands; right-click targets the clicked item predictably. Drag uses stable logical identity, clear valid/invalid target feedback and cancellation; command validates payload/revision. Deletion and incompatible edits explain consequences |
| Tooltips / feedback | Tooltips explain units, scope and shortcuts; disabled actions explain why. Inline validation points to field/source; long work reports operation/progress and supported cancellation. Failure retains draft and last valid result, with stale status where applicable |
| Keyboard shortcuts | One documented command map with focus/context precedence. Proposed defaults: Ctrl+S saves current owner, Ctrl+Z/Ctrl+Y route Undo/Redo to active owner; F frames Scene selection; Delete only in relevant object lists/viewports. Resolve camera/game/text-input conflicts before adoption; menus/tooltips show effective mappings |
| Undo/Redo / Save | Existing EditorDocument/native history and AssetService remain authoritative. One completed gesture is one history action; cancel makes no source edit. Show history target/label and availability. Define Save current versus Save All; never label separate file writes as atomic publication |
| Selection / navigation | Keep scene-object identity distinct from asset identity and future mount/member identity. Show active edit owner. One authoritative draft per asset; Open restores it, Reveal does not assign, switching workflows preserves drafts and appropriate selection |
| Preferences / workspace | Versioned per-user layout, split sizes, section states, filters and editor navigation speed/pivot as appropriate; explicit reset/factory layouts and safe fallback for invalid/old settings. Never store these in scene/gameplay sources or saves. Decide project-versus-global preference scope in 2.1 |
| Editing / Play | Visible Stopped/Preparing/Playing/Paused/Failed states and input owner. Frozen Play resources stay coherent; disabled editing explains Stop requirement. Browsing/debugging remains available where supported; preview cannot mutate gameplay |

## Phase 2.2 – Window-by-window improvements

P0 blocks basic authoring, obscures edit ownership/recovery or breaks repeatable navigation; P1 reduces frequent friction; P2 is optional convenience. Priorities below are planned targets grounded in the inspected behavior; the full audit may reprioritize them with evidence. Close each row with its actual task receipt, not a screenshot alone.

| Surface / priority | Actionable improvement | Acceptance / boundary |
| --- | --- | --- |
| Scene viewport / P0 | Integrate R1 controls/help/speed/focus into shared toolbar; clarify gizmo/mode, selection and placement feedback; expose existing scene validation | Author navigates, frames, drops and transforms an object from multiple views; no unintended selection/placement or gameplay input |
| Hierarchy / P0 | Preserve search/clipping; add consistent selected-object actions, focus/reveal source routes and useful labels; assess group presentation from actual model | Find a named object in a large scene, select/frame/inspect it, delete/Undo without wrong-target edits. Mount/member rows ship with R4, not invented hierarchy in R2 |
| Inspector / P0 | Clear edit scope/dirty/history header, units and consistent commit/cancel/validation; reusable collapsible component/reference controls | Author identifies what changes and whom it affects, edits/Undo/Redo/saves, rejects invalid input and recovers draft. R4 owns Add Component/Model functionality |
| Assets / P0 | Reconcile existing kind filters/editor-route coverage (including Items); unify search/selection/Open/Reveal/import/cook feedback with owning editors | Search, open correct existing editor, return and retain selection/draft; missing/failed assets remain diagnosable; no duplicate draft or accidental assignment |
| Prefab surfaces / P0 standards, R4 delivery | Audit current assembly edit scope/provenance and write header, mount/member selection, override and Apply/Swap preview conventions | R2 specification reviewed against prefab design. R4 supplies functional workspace/link/patch/publish acceptance; R2 does not claim a prefab editor exists |
| Material surfaces / P1 standards, R5 delivery | Audit existing imported material visibility; specify model-slot/shared/local ownership, texture picker and preview conventions | R2 specification points to existing scope and R5 contract. Reimport-safe overrides and material forms are R5, not an overhaul prerequisite |
| Console / P1 | Add severity/text filtering, copy and actionable source/context navigation where metadata exists; preserve useful diagnostics during refresh | Diagnose one failed import/validation/Play preparation, locate owner and repair it without losing draft; unavailable source location is explicit |
| Game / Play and runtime debugging / P0 | Consistent transport/state/input toolbar, preparation/failure feedback, debug selection and focus release; preserve profiler/input/gameplay inspector | Start/pause/step/Stop, inspect state and return to authoring without stuck input or modifying frozen resources; errors point to repair route |
| Settings / project configuration / P1 | Implement editor preferences for delivered UX features, reset/layout/shortcut discoverability; group only existing supported project settings | Preferences survive restart/reset without scene bytes changing; future gameplay/render fields clearly unavailable until their own milestone |
| Character/Player/NPC / P1 | Audit existing reference forms, preview controls, embedded loadout/mask and edit scope; adopt shared header/picker/history components | Create/open/edit/assign/save through current contracts; distinguish presentation, Player and NPC ownership. New movement data is R3 |
| Ability/Effect, Animation/Composer/graph, Mask Editor, Items / P1 | Audit actual create/duplicate/assign/preview/history gestures and error paths; consolidate common controls without changing specialized semantics | Representative current workflow per surface keeps drafts/history and frozen fresh-Play behavior; no new gameplay/animation channels |
| VFX/Audio and auxiliary Tools / P1 | Audit authoring/preview, jobs and unsupported-device/status messages; adopt common feedback/navigation as appropriate | One existing task per tool (including input, navigation bake, saves, history, scripts, runtime phases) succeeds or explains an actual dependency; preview lifecycle remains correct |
| Optional conveniences / P2 | Recent/favourites, thumbnails and additional shortcuts only after measured need | Separate follow-ups; do not hold P0/P1 completion for speculative additions |

## Phase 2.3 – Cross-window workflows

Implement shared command/routing adapters incrementally, keeping native source/draft/history ownership. Do not use UX work to replace AssetService, EditorDocument, gameplay authority or the refactored service boundaries.

- **Asset → Inspector → owning editor → return:** selection shows correct source/status, Open enters the existing draft, reference assignment is deliberate; return restores context.
- **Assets/Place → Scene → Inspector:** use supported asset kinds and stable drag identity; invalid drops explain why; place/select/Undo/Redo affects the intended object. Extend to prefabs in R4.
- **Scene object → source asset → scene instance:** expose provenance/reveal/open where supported, preserving scene selection and dirty drafts. Apply/Swap/override semantics remain R4.
- **Context actions/shortcuts/history:** toolbar/menu/context/keyboard routes reach the same public commands and history owner; text entry, previews, modal dialogs and game input have predictable precedence.
- **Workspace persistence:** customize Scene/Game and representative authoring layout, switch/reopen/restart, reset and recover invalid preferences; content and asset identities remain unchanged.
- **Edit → save → cook → Play → Stop:** report preparation/progress/errors and frozen generation, release input and return to selection; repair invalid authoring while retaining the last good package. This qualifies UX for existing features; the full authored sandbox follows R6.2.

For each flow, record baseline and final steps/clicks/time-to-completion under comparable conditions. Set a concrete improvement target after measurement; do not invent numeric reductions now. Include an unfamiliar-author pass to test discoverability and a keyboard/focus pass. Track correctness separately from speed.

## Dependencies, implementation slices and exit criteria

1. **2.1 inventory/evidence and standards:** read current source/checkpoint, capture tasks, rank findings, choose shared tokens/command/preference policy and pilot controls. Exit: every delivered surface is inventoried; unknowns remain labelled; P0/P1 findings have owners and acceptance tasks.
2. **2.2 shared controls then surface adoption:** extend existing theme/pickers/commands/preferences, then Scene/Hierarchy/Inspector/Assets/Game, followed by the remaining tools. Preserve functional gates and narrow module ownership. Exit: prioritized existing-surface tasks pass; prefab/material specifications are ready for their later feature milestones; scope changes are recorded.
3. **2.3 integrated qualification:** complete cross-window flows and measured author pass, restart/reset/failure recovery, then update report/STATUS/CONTINUE. Exit: P0 and agreed P1 acceptance passes; each deferral names rationale/owner and cannot hide a broken core flow.

R1 is the entry dependency. Audit notes can be gathered earlier, but R2 navigation acceptance uses the delivered R1 camera. Completed refactor/scene-scale work supplies the baseline; their deferred instancing/reduced-feature/remote/EOS gates remain separate. Future R3–R8 surfaces adopt the standards when implemented, with their existing technical acceptance criteria intact.

Use focused command/history/preferences tests where behavior changes and one repeatable author task per changed surface. Broaden checks only at actual shared publication/resolver/input/render boundaries. Include draft retention, stale revisions, Undo/Redo grouping, invalid preference recovery, large-scene responsiveness and focus/Play isolation. Preserve measured scene-scale results; do not rerun full network/provider/device matrices for layout adjustments. R2 is complete only with recorded workflow evidence, not the completion of this plan.
