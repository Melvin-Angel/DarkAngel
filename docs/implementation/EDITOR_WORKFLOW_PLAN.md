# Ashen Roots editor workflows and delivery plan

Captured 8 October 2026 from the user's editor direction. This is planned work, not implementation evidence. M4/M5 remain In progress; M6-M9 remain Not started. M2's verified initial editor foundation does not imply these workflows are delivered.

## Product direction

DarkAngel is tailored to making third-person co-op action games, with Ashen Roots as its concrete use case. Its main systems should have native authoring workflows integrated into the engine.

Use top-level workflow tabs that change the main editor layout, following the workspace idea in Blender and the distinct working views in Ableton. Scene / Level Design is the default. Each workflow gets a focused arrangement of its browser, preview, properties and tools. Prefer embedded panels and tabs; routine authoring should not require managing a collection of floating windows. Import and selection dialogs can be focused popups.

Exact panel placement, tab names, keyboard behavior and visual design will be designed with the user. The layouts below describe intent, not an approved final mockup. Retain the established dark-gray theme, blue primary interactions and restrained orange attention states.

## Shared import workflow

Provide Import inside the editor. After choosing one or more external files, require an explicit item type before importing. Show the destination, compatible options, detected dependencies and validation results before publication. Extension-based suggestions can help, but do not replace the user's type choice.

Route the imported project-owned sources into the appropriate category automatically. The following destinations are a proposed policy, to be finalized against the existing catalog and content organization before implementation:

| Import type | Proposed destination | Relevant options / checks |
|---|---|---|
| 3D model / unskinned mesh | `content/models/` | Units, axes, materials/textures, pivots and collision references |
| Skinned mesh / character part | `content/characters/` | Canonical human compatibility, modular slot or explicit unique rig; skin/material validation |
| Animation | `content/animation/clips/` | Rig compatibility, root motion, timing and supported conversion profile |
| VFX | `content/vfx/` | Supported effect format and texture/material dependencies |
| Audio | `content/audio/` | Supported source format and event/bank authoring relationship |
| Texture | `content/textures/` | Color space, usage and supported cook settings |
| Material | `content/materials/` | Shader family, typed parameters and referenced textures |
| UI | `content/ui/` | Supported UI documents, styles, images and fonts |

These paths do not mandate moving existing content/animation, physics or Royal District assets. Preserve established AssetIDs and references. External originals remain intact: copy/adopt files into owned project storage, then organize those owned files. Never silently delete or relocate an external source. Collect dependent files with preserved references; define collision handling for duplicate names and reimports without overwriting unrelated assets.

Use the existing import metadata, UUID catalog, cook/CAS and frozen dependency closure. Imports are bounded jobs with progress, cancellation and diagnostics. Validate and prepare before publishing; failed import/reimport preserves the previous usable generation. Only expose a category as importable when its converter and runtime/cook path work; unsupported formats get a clear explanation.

Acceptance: import a supported model, skin, animation and texture from the GUI; confirm automatic destinations, dependency resolution and picker visibility. Exercise duplicate names, incompatible rigs, cancellation and failed reimport. Add real VFX/audio/UI import receipts as those integrations become available.

## Shared visual asset picker and browser

Every asset-reference field uses the same picker, filtered to compatible asset types and, when relevant, rig/socket/profile constraints. A single-reference field selects one asset; list fields support multi-select and deterministic list assignment.

- Grid of thumbnails with readable names and asset types.
- Inspectable 3D preview with rotation for models/characters and animation preview where supported; suitable image/effect/audio previews as their services become available.
- Search, tag filters and type tabs; provide a way to assign and edit asset tags.
- Preserve selection/filter context while navigating between related assets and workflows.
- Show missing, incompatible, uncooked and failed assets clearly; references use AssetIDs rather than filenames.
- Share the browser/picker across Scene, Character, Ability, Composer, Projectile/AOE, Item and VFX workflows.

The Unity Ashen Roots combat authoring picker is a design reference to inspect read-only before building this interaction. Its native implementation remains to be designed. Persist searchable asset tags through existing source/catalog contracts; keep authoring tags distinct from gameplay ability/effect tags.

Acceptance: find an animation by name/type/tag, inspect it, assign it to a composer; multi-select compatible pieces into a list; edit tags and retain them through save/reopen and catalog rebuild. Invalid assignments reject without mutating the document. Preview work is bounded and releases resources when selection changes.

## Workflow tabs

| Workflow | User-facing authoring scope | Delivery |
|---|---|---|
| Scene / Level Design (default) | Scene viewport, hierarchy, asset placement, object/system presets, transforms and collision; later terrain, islands, splines, procedural placement and scatter | M4/M5 foundation; M6 world tools |
| Character | Canonical modular character creation and piece swapping; compatible other skinned/unskinned meshes and attachments; combat kit, stats, equipment and component assignments; later NPC behavior, dialog and loot | M5 character/combat core; M6 NPC/world links; M8 persistence links |
| Ability | Ability data, costs/cooldowns/effects and composer assignment; projectile/cast presets with fire rate, burst, spread, hold-to-charge and AOE settings | M5 |
| Animation Composer | Animation timeline and preview; hitboxes/slashes, object/VFX/projectile/AOE spawning, motion warping, invincibility/gameplay effects, parry/block windows and combo transition windows | M5 authoritative action core; M7 real VFX/audio authoring and preview |
| Projectile / AOE | Proposed shared workflow for projectile and area definitions, shapes, timing, delivery and effects; preview and ability references | M5; exact combined layout to design |
| Item | Proposed item definitions: description, image/icon, effects, equipment/kit links and other typed item data | M5 combat/equipment references; M6 loot/world links; M8 saved state; schema to design |
| VFX | Integrated effect selection, authoring, preview, dependency inspection and composer bindings | M7 |
| Texture / Material | Proposed later workflow for materials and lightweight Photoshop-style texture painting, directly on textures or selected objects | M6 terrain-material needs; broader painting deferred pending design and acceptance scope |

Character support must respect the locked canonical-human contract and explicit unique nonhuman rigs. The desire to use other meshes does not authorize arbitrary runtime retargeting. Dialog, item and loot schemas require explicit design; capture their intended editor links without claiming those runtime systems already exist.

Composer gameplay windows and ability presets compile to native typed assets and execute through the existing authoritative WorldSession/action/ability services. Previewing a slash or spawning a cosmetic VFX cannot cause damage. Motion warping, charge, burst, parry, block and invincibility need defined gameplay rules and focused host/client acceptance, not only visible timeline controls.

## Level design direction

Build a powerful native suite for fast placement of objects and game systems, terrain/floating-island sculpting, manual and procedural surface painting, splines and focused procedural tools. Optimize the tools for Ashen Roots workflows.

M6 develops the required mesh-first terrain path, coherent collision/navigation products, sculpt modifiers, surface/material/biome painting, spline generation and batched scatter under DAE-015/016/017. Stacked surfaces, caves and floating islands must retain their 3D geometry and paint scope. Measure edit/cook responsiveness and runtime cost on representative content.

Advanced volumetric sculpting and a broader texture-painting suite remain later proposals. They do not introduce runtime voxels, digging/destruction or an unqualified geometry dependency. Precise Royal terrain/hut collision remains an earlier M4/M5 task.

## Milestone integration and acceptance

This workstream extends the roadmap alongside the existing runtime gates. It does not reopen historical M2 evidence or mark later systems Verified. Each workflow must author real data used by the corresponding subsystem.

| Milestone | Editor work and observable gate |
|---|---|
| M4/M5 shared foundation | Design workspace blueprint; implement workflow-tab shell, Scene default, typed import and shared browser/picker in bounded increments. Tab switching preserves document/selection/Play state and handles pending edits explicitly. Existing undo/save/reload behavior remains passing. |
| M5 animation/combat | Deliver Character, Ability, Composer and Projectile/AOE workflows plus agreed Item core. Create a modular character, assign kit/stats/equipment, author an action/ability and test real accepted/rejected combat. Include compatible selection, timeline preview, native cook validation and shared transactional editing. |
| M6 world/NPCs | Deliver scene placement presets, terrain/sculpt/paint/spline/scatter tools, region/navigation inspection and NPC profile/behavior authoring. Extend Character/Item workflows with agreed NPC/dialog/loot references. Verify editor changes through cooked geometry, cell cycles and scripted NPC behavior. |
| M7 VFX/audio | Deliver VFX workflow and integrated audio authoring/preview, real supported imports, and Composer cue bindings. Create/edit actual effect/event content and verify activation, rejection, tails and resource retirement. |
| M8 saves/campaign | Add coherent save inspection and checkpoint/reset workflows; inspect persistent character/item/loot/world state through existing definitions. Verify persistence and failure recovery; editor inspection is not permission for guests to overwrite host state. |
| M9 release qualification | Qualify the full authoring loop and tools package: import, find, tag, assign, preview, edit, cook and playtest; validate layout usability, bounded previews and project reopen on clean tooling installs. Game Shipping still excludes authoring endpoints. |

## Implementation sequence from the current checkpoint

1. Design a reviewable workspace blueprint and choose the first supported import categories, destination policy and picker interaction. Use the existing native editor as the starting point.
2. Implement shared tab navigation, typed import and visual selection in coherent increments. Supported models/skins/animations/textures come first; other categories follow their actual adapters. Do not add empty workflows that imply working systems.
3. Continue the real M5 attack/action/ability authority path, then expose it in Composer and Ability workflows. Build Character and Projectile/AOE authoring around those same native definitions.
4. Close remaining M4 timing/fault/streaming and precise scene-collision gates alongside M5. M6 still waits for required M4/M5 gates; live EOS remains a separate M3 blocker.
5. Expand world, NPC, VFX/audio and save workflows at their owning milestones, with scoped receipts and documented remaining gates.

All authoring uses existing EditorService/EditorDocument validation, prepare/commit, revision checks, undo/redo, save/open and scoped agent commands. Keep source assets authoritative and cooked products derived. Use the locked DAE-007/008/010/011/012/013/014/015 contracts. Exact layouts, proposed schemas and later painting scope remain open design work; this document records direction and acceptance tasks, not a new parallel runtime or finished UI specification.
