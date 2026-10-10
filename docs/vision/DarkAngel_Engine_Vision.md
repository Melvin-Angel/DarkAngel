# DarkAngel final engine vision

Authoritative final-product target, revised 10 October 2026. All capabilities below are intended targets unless current implementation evidence explicitly establishes otherwise. This document is not a completion receipt.

## Purpose and gameplay

DarkAngel is a personal native C++20 engine and integrated editor for Ashen Roots: a third-person multiplayer action RPG with Soulslike and fighting-game-inspired combat. Optimize for an enjoyable content-production loop for its developer, rather than hypothetical external customers. One editor product contains all delivered tools; milestone build presets are development checkpoints, not editions.

The finished environment enables modular players and NPCs, stance-based eight-slot combat kits, readable attack/defence timing, resources and statuses, directional locomotion, targeting and camera modes, cooperative encounters, exploration, world interactions and persistent progression. Four-player co-op and a PvP test arena remain release qualification scenarios. Game-specific damage, defence, poise, equipment and progression rules belong to Ashen Roots definitions/evaluators, not hard-coded editor behavior.

## Documentation ownership

| Owner | Responsibility |
|---|---|
| This Final Vision | Intended finished capabilities and product boundaries |
| [Architecture decision log](../architecture/Decision_Log.txt) and its original DOCX | Locked DAE-001–021 contracts and rationale |
| [Actor gameplay design supplement](../architecture/Actor_Gameplay_Design.md) | Confirmed actor/state ownership, rationale, capability traceability and unresolved design questions |
| [Implementation handoff / M0–M9 roadmap](../architecture/DarkAngel_Implementation_Handoff.md) | Milestone responsibilities, dependencies and acceptance |
| [Editor workflow plan](../implementation/EDITOR_WORKFLOW_PLAN.md) | Incremental workflow delivery and focused authoring gates |
| [STATUS](../implementation/STATUS.md) | Current milestone state and links to current evidence |
| Implementation reports and evidence | Implemented boundaries, measured verification and limitations at each checkpoint |
| [CONTINUE](../implementation/CONTINUE.md), [Royal continuation](../implementation/ROYAL_CONTINUE.md), [ability authoring continuation](../implementation/ABILITY_AUTHORING_CONTINUE.md) | Immediate tasks, commands and blockers; current header supersedes older chronological entries |

Implemented means code is present; Verified means the named checks passed for their stated profile. Planned and deferred work has no implementation claim. A verified increment does not promote its enclosing milestone. Update milestone status, an implementation report and continuation instructions for significant implementation changes. Update this vision only when the intended product changes. Preserve historical receipts and label superseded checkpoint descriptions; do not maintain competing milestone status tables.

## Architecture and runtime principles

Preserve the locked decisions. Foundation owns stable IDs, reflection/versioned serialization and Flecs world domains with explicit phases, staged writes and generation-checked handles. Luau uses typed approved APIs, declared module/state dependencies, bounded execution, sandboxing and controlled reload; a rejected reload preserves the previous generation.

AssetService owns UUID assets, native source formats, import metadata, the rebuildable SQLite catalog, SHA-256 CAS, typed dependency closure and immutable frozen generations. No second asset database or parallel gameplay schema. Scene assemblies own stable local IDs, reusable compositions/patches and prepare/populate/wire/activate lifecycle. EditorService transactions use revision checks, prepare/commit and undo; preview and gameplay worlds remain isolated from authoring.

WorldSession owns server-authoritative gameplay through shared LocalLoopback, GNS and EOS contracts. Identity, epochs, bounded protocol parsing and content compatibility precede activation. Jolt provides collision/queries and the fixed 60 Hz character motor; prediction/reconciliation replays in disposable isolated query state. Camera presentation never validates hits. Native abilities/effects/tags/attributes own atomic commitment, hit validation, cleanup and replication; AI and Luau request approved operations.

Ozz and owned Animation Graph presentation share the canonical human rig; unique nonhuman rigs have explicit compatibility and no arbitrary runtime retargeting. Action Composer owns gameplay timing through the existing native action runtime. Animation/root motion feeds the existing motor-achieved ownership boundary; cosmetic cues do not cause damage. Diligent D3D12/Vulkan and HLSL/DXC own rendering. Recast/Detour and BT.CPP adapters own navigation and native NPC orchestration. Effekseer/VfxService and Amplitude/AudioService own effects and sound. RmlUi supplies runtime UI; ImGui supplies developer tooling. The host SQLite ledger/checkpoint architecture owns persistence. Headless and Shipping retain their dependency and authoring-endpoint exclusions.

## Actor gameplay and extensibility (planned target)

Character supplies reusable presentation; Player/NPC supply common loadout and controller-specific authored configuration. Players and NPCs share Character, Kit, Ability, Effect, Mask and Equipment types and resolve into the same authoritative native actor state. Four equipped masks are distinct from the selected Fire/Earth/Water/Air stance. Masks/equipment may grant owned tags/effects/modifiers, select kits/movement/locomotion and bind cues. Removing one source never removes another source's contribution.

Tags express conditions, attributes quantities, effects changes/lifetimes, typed parameters measured state, and events occurrences. A held tag does not repeatedly trigger commands. Deterministic bounded lifecycle events and existing action/effect cue identities prevent duplicate execution during prediction/replay. Native authority owns consequential state; clients consume replicated or reconciled state for presentation.

Graphs select stance locomotion and reaction states using tags and typed parameters. Composer can present flinch, stagger, stun, knockdown, death, revive, guard break and stance/equipment changes through authored mappings with priorities, interruption, queue/retrigger and termination policies. Stun duration stays in the effect; burning aura can coexist with stun. Native actions/graphs/corrections remain one runtime, and restoration uses current actor state.

Reusable native C++ systems and data-driven assets cover ordinary gameplay. Game-specific bounded Luau and registered C++ extensions cover custom evaluators, special abilities, boss decisions and quest/encounter logic through the same validated APIs. No bypass of costs, hits, effect ownership or networking. Runtime success and practical authoring completeness have separate gates.

The [design supplement and Ashen Roots traceability](../architecture/Actor_Gameplay_Design.md) records source ownership, canonical tag reconciliation, lifecycle cleanup, implementation coverage, milestone dependencies and unanswered schema/event/reaction questions. It retains universal traversal, combos/ranged/defence, cooperative lifecycle, world progression and persistence as intended capabilities even where native coverage is missing.

## Editor interaction

Select the activity and receive a useful default layout, inspired by Blender workspaces and Ableton views. Scene / Level Design is the default. Approximately fifteen primary workflow destinations are fixed initially; layouts are customizable, and new workflow types are added only if personally needed later. Effects may begin inside Ability and Materials inside Assets. Projectile/AOE and Item are typed auxiliary designers in Ability/Assets and their related workflows, rather than mandatory extra workspaces.

Every workflow offers relevant creation/selection, typed forms, compatible shared reference picking, preview, validation, Save/cook, direct links to related assets/workflows and a short route to Game testing. Routine authoring should not require hand-editing multiple JSON files or restarting the editor. Keep reusable panels embedded by default; dock/undock, resize and rearrange them, save layouts per workflow, restore factory layouts and persist editor settings. Share selection and contextual filters. Optional pinned panels preserve useful context across switches; pending edits and input cancellation are explicit. A small panel registry is sufficient; avoid an elaborate plugin/window framework.

Game alone owns Play/Stop/Pause/Step. Save/reload live in menus, Tools exposes auxiliary panels, and Settings separates Editor and Project options. Console/diagnostics are globally accessible. Exact layout, shortcuts and preview budgets are refined through actual content work.

## Primary workflows (planned final targets)

### 1. Scene / Level Design

Create playable environments with scene viewport/hierarchy, transforms/gizmos, object placement and asset dragging, prefab-like scene compositions, environment/geometry tools, lighting setup/preview, collision assignment/visualization, physics objects/triggers and navigation generation/overlay. Place encounter volumes, spawn points, doors, elevators, moving platforms, checkpoints, altars and teleport destinations. Configure transitions/streaming; validate, save, cook and play-test scenes.

Prioritize Ashen Roots placement and mesh-first level iteration. Retain DAE-015/016/017 terrain, spline/scatter and collision/navigation responsibilities; advanced volumetric or general terrain sculpting is not a prerequisite for the initial level-design loop.

### 2. Assets

Manage assets through searchable categories/filters, explicit typed import/reimport, create/duplicate/rename/organization, metadata and stable IDs, dependency/reference inspection, cook/generation status and validation diagnostics. Distinguish source from cooked products. Share compatible selection with all workflows, prepare safe publication and retain old generations on failure. Preserve external originals and use the existing catalog/CAS.

### 3. Character Designer

Create/duplicate shared character definitions; assemble modular body parts, select skins, inspect skeleton/rig, assign materials, equipment slots/attachments and weapon/effect sockets. Configure compatible animation resources and optional presentation defaults. Preview animated characters and validate skeletons, sockets and references. Character owns no starting attributes, Combat Kit, masks, gameplay equipment loadout, player controls or NPC decisions. Player/NPC definitions reference this reusable presentation asset; attachment slots describe physical compatibility.

### 4. Player Designer

Select shared characters and author a common Actor Loadout: starting attributes, combat kits/stances, four mask slots and equipment. Add input profiles, movement and targeting settings; preview effective source-owned grants/effects and navigate references. Authored configuration is separate from live resources, effects and cooldowns. Configure free-look, aim, lock-on and dialog cameras, transitions/offsets/limits; spawn/respawn and eventual downed/revive lifecycle; presentation/HUD references. Expose ownership/authority only where legitimately configurable within native rules. Local input/camera preferences remain separate from authoritative gameplay and saves.

### 5. NPC / AI Designer

Create NPC definitions referencing shared Characters and the same Actor Loadout structure, with starting attributes/scaling, kits, masks/equipment and faction/targeting. Preview effective grants/effects and navigate references; author native BT.CPP/JSON trees, visualize blackboards, sensing/detection, movement/preferred range and combat decisions. Assign abilities/kits and aggression/recovery/strafe/retreat settings. Test navigation and simulation; inspect decisions and idle/hit/stagger/death behavior; reference dialog/audio. Luau leaves/scorers remain bounded. Do not revive the Unity YAML runtime or merge AI decisions with authoritative ability execution.

### 6. Ability Designer

Create/duplicate/edit native abilities with activation policies, costs/cooldowns, requirements/tag conditions, Composer and clip references, hit profiles/windows, targeting and effect bindings/magnitudes. Configure activation/commit/cancellation/interruption, canonical eight-slot kit assignment, generation inspection, validation/cook and preview/testing. Melee, ranged, projectiles and areas become available with their native systems. This is an immediate authoring priority, extended from the delivered first forms rather than a replacement schema.

### 7. Gameplay Effects / Attributes

Create/duplicate reusable effects and define attributes; browse registered gameplay tags; author modifiers, duration/expiry, periods, stacks, source/target conditions, application/removal and death/interruption/cleanup. Choose registered execution evaluators and magnitudes; simulate and inspect active effects/timers. Ordinary modifiers/statuses require no custom Luau evaluator. Begin as an Ability section until a separate destination is useful. New generated tags/schema changes retain rebuilt-consumer validation; new custom formulas require approved evaluator registration.

### 8. Animation

Preview characters, skeletons and clips; playback and scrub an Action Composer timeline with blocks, cues, hit windows and commit markers. Author graphs with tag conditions and explicitly owned typed bool/int/float/enum parameters, stance blend-tree selection, states/transitions, priority/interruption, directional locomotion, layers, upper-body/joint masks and additive animation. Bind Composer reactions to committed gameplay events/conditions with queue/retrigger/end policies. Preview state changes and restore current locomotion after reactions; animation never owns stun duration or gameplay legality. Visualize root motion and animated sockets; configure motion warping when supported, events/gameplay bindings and character/target previews. Validate and cook through existing graph/action/rig assets. Begin with the minimum useful ability timing/pose preview; retain one action runtime and simulation ownership.

### 9. VFX Designer

Browse/create reusable Effekseer-compatible definitions; author particles/emitters through the selected integration, preview elemental effects, projectile trails, impacts and environmental effects. Bind sockets, timing/lifetime and gameplay cues; validate/cook dependencies. Use native trails/decals where appropriate. Actual effect-content editing is a gate; a universal particle node graph is unnecessary.

### 10. Audio

Import/browse audio, define Amplitude sound events, preview playback and configure volume/pitch/randomization, buses/mixing, spatial sound and ambient zones. Bind combat/footstep/action cues, music transitions and NPC/dialog references; debug event routing. Reuse middleware authoring and banks rather than build a DAW.

### 11. Quest / Dialog / World Events

Author dialog trees, quest/objective definitions and world events through graphs or equivalent typed forms. Configure conditions/outputs, interactions/triggers, doors/gates, encounters/waves, altar/checkpoint behavior, locks/keys, persistent flags, item/inventory requirements and scene transitions. Debug progression state. Schemas must be designed against existing scene/Luau/persistence boundaries; graphical forms do not introduce another execution system.

### 12. Materials / Shaders

Browse/create materials, inspect typed properties, assign textures and surface/rendering settings, preview materials and manage instances/overrides. Show shader compilation and texture/dependency diagnostics. Begin in Assets with typed forms. Advanced shader graphs and broad texture painting are optional later proposals.

### 13. UI / HUD Designer

Preview RmlUi screens/hierarchy, layout/style and reusable components with approved gameplay data bindings. Author Health/Stamina/effects, ability/cooldowns, targeting/lock-on, dialog, menus/settings and inventory/equipment screens; preview resolution/aspect and interaction. Start with supported RML/RCSS source editing, typed descriptors and preview. A visual drag-and-drop designer is an eventual optional extension under DAE-010, not a new framework or M5 prerequisite.

### 14. Scripting

Browse/view/edit Luau sources, validate diagnostics, inspect exposed APIs and asset/event references, debug runtime state/errors and controlled reload. External IDEs and Codex remain primary coding tools; a full embedded IDE is unnecessary. Preserve module packages, sandboxing, execution limits and migration/cleanup.

### 15. Game / Testing

Run game viewport with Play/Stop/Pause/Step, input monitor/console, CPU/GPU/frame-time profiler, memory/allocation diagnostics, physics/collision/hitboxes and animation/action debugging. Inspect ownership, replication, authoritative versus predicted state/corrections, active mask/loadout, ability grants/costs/cooldowns, attributes/modifier sources, active effects/tags/timers and world/encounter state. Explain tag grant sources, why an ability is blocked, and which condition/event/priority selected a graph state or interrupted a Composer; show relationships rather than raw values alone. Configure multiplayer test sessions and supported simulation faults. Always start a fresh isolated session from validated cooked assets; authoring Save never silently patches live authority.

## Reusable panels and tools (planned as needed)

| Panel/tool | Shared responsibility |
|---|---|
| Inspector | Typed properties of selected objects/assets |
| Asset Browser / reference picker | Search, compatible filtering, shared selection and bounded previews |
| Console / diagnostics | Errors, warnings and navigation to their source |
| Asset Dependency Viewer | Source/runtime references and frozen generations |
| Cook / Build Monitor | Validation, job progress, package preparation/publication |
| Gameplay Tag Browser | Registered tags and schema/generation context |
| Input Mapper | Semantic keyboard/mouse/gamepad bindings, chords and cancellation |
| Physics / Navigation Debuggers | Colliders, sweeps/rays/contacts, navmeshes and AI paths |
| Network Debugger | Ownership, RTT, replication, prediction and corrections |
| Profiler | CPU/GPU, memory/allocations and simulation timing |
| Animation Debugger | Graph states, action clocks, layers and root motion |
| Ability / Effect Debuggers | Activation/grants/costs/cooldowns; effects/tags/stacks/duration/modifiers |
| World State Debugger | Quest, encounter and checkpoint state |
| Asset / Scene Validator | Broken references and invalid assets/scenes |
| History / Undo | Reversible revisioned editor transactions |
| Scene Search | Objects and components |
| Project / Git Status | Optional lightweight read-only repository context |

Build panels only when their corresponding workflow needs them; share interfaces instead of duplicating implementations.

## Content publication and iteration

Import/adopt owned sources with explicit type, stable identity and compatible dependencies. Edit/create through native revisioned commands; validate type/ID/schema, references and changed consumers. Save prepares a candidate, cooks the relevant closure, and publishes only validated source/catalog heads. Failed normal operations preserve prior sources and playable generations; visible diagnostics identify the failed stage.

Play must rebuild the complete selected scene runtime package, not just replace a kit. Validate all frozen scene/model/rig/skin/clip/action/input/ability/effect references, prepare GPU and character-preview resources and an isolated probe, then publish resources and start a new WorldSession. Old leases survive failed candidates. Current coordinated Save is not crash-atomic across files and SQLite; recovery journals/manual reconciliation remain an implementation limitation until qualified otherwise.

Target loops: Ability → Create → Configure → Assign Effect → Assign Kit Slot → Save → Cook → Game/Play → Test; NPC → Create → Character → Kit → Behavior → Place in Scene → Play; Animation → Character → Composer → Timing → Save/Cook → Preview in Game. Preview scrubbing is isolated and never activates gameplay.

## Validation and production

Validate early with focused asset/editor/runtime checks, clean frozen closure loads and accepted/rejected gameplay cases. Broader clock/fault/streaming, provider, device, bandwidth/allocation and performance matrices belong to integration checkpoints and milestone acceptance, not every form change. Measure on representative content; no arbitrary performance claims from compilation.

The finished tools package supports end-to-end Ashen Roots production and clean project reopen. Game packaging validates closure, prerequisites/licenses and provenance; Shipping excludes authoring endpoints and runs without source, dev PATH or editor cache. Preserve user saves outside installation and verify recovery/migration, multiplayer and clean-machine behavior.

## High-level milestone mapping

| Milestone | Vision responsibility |
|---|---|
| M1 | Foundation/ECS/metadata, versioned scenes, bounded Luau/state/tasks/reload; do not reopen verified foundation work |
| M2 | Asset/catalog/CAS, rendering/editor/transactions/import foundation; later fixed workflows/layouts/material forms extend it incrementally |
| M3 | Session/identity/transport/protocol compatibility and bounded replication; Game network inspection builds on it; live EOS remains a separate gate |
| M4 | Jolt motor/collision/streaming, fixed ticks/prediction/reconciliation and movement/network debugging; future traversal requires its own rules |
| M5 | Initial Character/Player/Ability/Effect/Animation, eight-slot kits, common Player loadout, masks/stances/source grants, tag/parameter graphs and Composer reactions, native combat/statuses and fresh Play/debugging; cue references precede M7 adapters |
| M6 | World cells/ledger, required mesh terrain/splines/scatter, navigation, native NPC AI/authoring, encounter/interaction foundations |
| M7 | Real VFX/audio adapters/content authoring, cues/lifetimes, materials/presentation refinements |
| M8 | Host saves/roster/checkpoints/recovery and campaign progression; inventory, quest/dialog/world-state tools and UI expansion depend on approved schemas |
| M9 | Integrated production workflow, four-player/PvP release scenarios, packaging, multiplayer/device/performance and usability qualification |

Detailed scopes and observable acceptance remain in the roadmap, not duplicated here. M6 supplies world interactions, M8 coherent persisted progression; required runtime HUD is still M5 while broader UI/content tools expand later.

## Deliberate exclusions

No marketplace, third-party plugin ecosystem, arbitrary user-created workflows, general visual scripting platform, full modeling suite, DAW, custom Git client or IDE replacement. Universal shader nodes and advanced volumetric/texture sculpting are not mandatory. Blender handles complex modeling/offline creation; IDE/Codex handles substantial C++/Luau programming. DarkAngel specializes in assembling, authoring, previewing, validating and testing gameplay content.
