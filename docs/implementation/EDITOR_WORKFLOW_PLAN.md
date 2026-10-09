# Ashen Roots editor workflows and delivery plan

Captured 8 October 2026 from the user's editor direction. This is planned work, not implementation evidence. M4/M5 remain In progress; M6-M9 remain Not started. M2's verified initial editor foundation does not imply these workflows are delivered.

Initial implementation update: Scene / Level Design and Assets layouts, a single-file typed import modal with supported model/canonical skin/normalized clip/color texture routing, and a searchable asset inventory are now implemented. See [Editor import report](EDITOR_IMPORT_REPORT.md) for exact profiles, limitations and focused receipts. The visual picker, tags, batch/background import and subsystem workspace designs below remain planned.

Ability authoring implementation update (9 October): [NATIVE_AUTHORING_REPORT.md](NATIVE_AUTHORING_REPORT.md) records the initial embedded Ability/Effect/action forms, typed reference fields, native creation/duplication, kit slot/effect composition and Save/cook/complete scene/fresh Play loop. The numeric action forms are an initial Composer surface; visual timeline/character preview, structural block creation, broad thumbnail/tag picker and the other workflow designers below remain planned. Scene stays the default workspace and all Play controls stay in Game. This does not change M4/M5 or M6 acceptance gates.

History/Save implementation update (after `3334e56`): [NATIVE_AUTHORING_HISTORY_REPORT.md](NATIVE_AUTHORING_HISTORY_REPORT.md) records session gameplay Undo/Redo, grouped form/kit-composition edits, stale-command rejection and a prepared coordinated native source/dependent kit/scene Save with normal-failure rollback. The next smallest visual increment is a read-only action lane view with isolated compatible clip/character scrubbing; full Composer remains planned.

Composer update (9 October): [COMPOSER_LANES_REPORT.md](COMPOSER_LANES_REPORT.md) records the delivered read-only action ruler/tracks/markers/scrub cursor and numeric block selection. Isolated character pose scrubbing is next; structural editing/full Composer remain planned.

Composer preview update (9 October): [COMPOSER_PREVIEW_REPORT.md](COMPOSER_PREVIEW_REPORT.md) records frozen compatible character/clip scrubbing beside the lanes. Structural blocks/typed hit/commit references are next; full Composer/graphs/layers/masks remain planned.

Structural Composer update (9 October): [COMPOSER_STRUCTURE_REPORT.md](COMPOSER_STRUCTURE_REPORT.md) records native block create/remove and typed ability hit/commit fields. Timeline gestures are next; full graphs/layers/masks and production designers remain planned.

Timeline gesture update (9 October): [COMPOSER_GESTURES_REPORT.md](COMPOSER_GESTURES_REPORT.md) records validated move/resize and grouped/cancellable timing history. Preview usability/picker refinements are next; full graph/layer/mask and broader designer work remain planned.

Preview usability update (9 October): [COMPOSER_PLAYBACK_REPORT.md](COMPOSER_PLAYBACK_REPORT.md) records presentation-only playback/camera controls and more compact native Composer panels. A focused Animation destination is next; full graph/layer/mask tools remain planned.

Animation destination update (9 October): [ANIMATION_WORKFLOW_REPORT.md](ANIMATION_WORKFLOW_REPORT.md) records the initial shared Composer/clip-preview workflow and Ability navigation. Full animation graph/state/layer/mask designers and persistent configurable layouts remain planned.

Attribute-authoring update (10 October): [ATTRIBUTE_AUTHORING_REPORT.md](ATTRIBUTE_AUTHORING_REPORT.md) records native definition forms/history and ordinary modifier use, with affected standalone effects included in coordinated Save validation. Generated-tag schema/rebuilt-consumer handling and broader designers remain planned.

## Documentation ownership

The [final engine vision](../vision/DarkAngel_Engine_Vision.md) owns the complete product target and all fifteen primary workflow descriptions. The [M0–M9 handoff](../architecture/DarkAngel_Implementation_Handoff.md) owns milestone scope/dependencies/acceptance; this plan owns incremental editor delivery. STATUS owns current state, reports own receipts and ABILITY_AUTHORING_CONTINUE owns immediate work. Historical implementation notes below are checkpoint descriptions, superseded by the current reports where later work is delivered.

## Product direction

DarkAngel is tailored to making third-person co-op action games, with Ashen Roots as its concrete use case. Its main systems should have native authoring workflows integrated into the engine.

There is one editor product/build containing all currently implemented editor features. Milestone-named build directories and presets are temporary iteration checkpoints, not separate editor versions or feature editions. Shared-library compatibility checks may disable subsystems, but they do not define a lighter editor product. Integrate every delivered workflow into the same editor; converge temporary build naming and launch paths as the iteration checkpoints are consolidated.

Use top-level workflow tabs that change the main editor layout, following the workspace idea in Blender and the distinct working views in Ableton. Scene / Level Design is the default. Each workflow gets a focused arrangement of its browser, preview, properties and tools. Prefer embedded panels and tabs; routine authoring should not require managing a collection of floating windows. Import and selection dialogs can be focused popups.

Workflows are fixed initially; reusable registered dockable panels can be undocked, resized and rearranged. Persist per-workflow layouts/settings, restore factory layouts, share selection/contextual filters and related-asset navigation, and keep console/diagnostics globally available. Pinned panels are optional when useful. Do not build a general plugin framework or arbitrary workflow creation.

Exact panel placement, tab names, keyboard behavior and visual design will be designed with the user. The layouts below describe intent, not an approved final mockup. Retain the established dark-gray theme, blue primary interactions and restrained orange attention states.

## Player, NPC, Game and input direction (9 October 2026)

The user added separate Player and NPC workflows and a dedicated Game workflow. Player/NPC authoring reuses Character definitions and shared combat kits rather than duplicating their data. These are all workflows in the one full editor.

Game owns the game viewport and docked profiler/debugging panels. Play, Stop, Pause and Step controls belong only here. Scene remains the default level-design workspace. Save Scene and resource/script reload actions live in drop-down menus, not a permanent button toolbar. A Tools menu hosts auxiliary views that do not belong to a main workflow; keep them embedded. Settings exposes general Editor and Project sections, with empty placeholders acceptable until the settings are designed.

Player authoring assigns the character, input profile and player-specific setup. It configures free-look, aim, dialog and lock-on camera modes, their transitions/offsets/limits, and the player lifecycle. Camera presentation does not determine authoritative hits; tick aim/targets still require native validation. M5 owns the initial player/input/camera authoring; dialog/interaction and persistent roster links extend through M6/M8. The existing follow camera is only an initial preview, not all four modes.

NPC authoring creates/edits behavior trees, NPC decision profiles, sensing and shared combat-kit references, plus dialog and SFX-pack assignments. M6 owns native BT.CPP/JSON topology, Luau leaves, blackboards, sensing/navigation and decision profiles; M7 owns real audio packs/events, and later dialog/persistence needs their own defined schemas. Keep tree orchestration separate from candidate scoring, ability validation and Composer timing. Do not import Unity's YAML behavior-tree runtime into the native engine.

Input authoring binds keyboard, mouse and gamepad controls to semantic actions, with Pressed, Hold, Released and Tapped events for both single controls and chords. Hold/tap timing and chord consumption are explicit settings. Support consuming none, the trigger only, or all participating controls, with deterministic arbitration and cancellation. Focus loss, pause, workspace changes, disconnect and rebinding must not produce accidental taps or stuck actions. Local binding preferences remain separate from authoritative gameplay state and campaign saves. Map semantic intent to the existing WorldSession/motor/ability APIs; never drive damage from a key callback.

The supplied Unity reference is actually under `C:/Unity Projects/AshenRootsMP/Assets/Game/Docs`. Read `Combat_System_TODO.md`, `Core_Traversal_System.md` and `NPC_Behaviour_Trees.md` as read-only gameplay references. Relevant behavior includes stance/mask slot resolution, L1-alone defense on release unless consumed by a chord, L2 ranged mode, shared player/NPC kit rules, and server-only NPC decision execution. Preserve these responsibilities while following the locked native architecture. Unity status checkmarks are not DarkAngel acceptance evidence.

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

## Fixed workflow delivery map

All fifteen destinations are final targets, not current functionality. Main tools and required capabilities are defined once in the [vision](../vision/DarkAngel_Engine_Vision.md#primary-workflows-planned-final-targets). Effects initially remain an Ability section; Materials can remain in Assets. The previous separate Projectile/AOE and Item proposals become typed auxiliary designers within Ability/Assets and related workflows. Their runtime schema/acceptance obligations are preserved.

| Workflow | Initial delivery / later refinement |
|---|---|
| Scene / Level Design | Existing scene shell; M4/M5 placement/import foundation, M6 world/terrain/nav/interactions |
| Assets | M2 catalog/cook foundation, current explicit import/inventory; M4/M5 shared compatible picker/layouts, later category adapters |
| Character Designer | M5 shared modular character/rig/kit/stat/animation references; equipment/persistence links as supported |
| Player Designer | M5 input/movement/cameras/targeting/lifecycle/HUD references; M6 interactions, M8 persistence |
| NPC / AI Designer | M6 native trees/profiles/sensing/navigation/debugging; M7 sound and M8 progression references |
| Ability Designer | M5 delivered first forms/kit bindings; extend practical timing/preview before broader targeting |
| Gameplay Effects / Attributes | M5 current effect forms, later schema/tag designers and simulator; begin inside Ability |
| Animation | M5 read-only lanes/scrub first, then visual Composer/graphs/layers/masks; M7 cue previews |
| VFX Designer | M7 Effekseer content tools, trails/decals and native cue integration |
| Audio | M7 Amplitude event/bank/mixer/spatial/ambient preview and cue tools |
| Quest / Dialog / World Events | M6 interaction foundations; M8 approved progression/dialog schemas, persistence and debugging |
| Materials / Shaders | M2-based typed material forms in Assets; M6 terrain needs, M7 presentation diagnostics/refinement |
| UI / HUD Designer | Required runtime HUD M5; supported RmlUi source/preview and wider menu/content tools expand through M8; optional drag-and-drop later |
| Scripting | M1 bounded packages/reload; incrementally expose source/API/diagnostics through shared tooling, external IDE remains primary |
| Game / Testing | Current Game/input foundation, M4/M5 movement/combat inspectors; each later subsystem extends debugging, M9 integrated qualification |

Character definitions are shared by Player/NPC rather than owning their behavior. Preserve canonical-human and explicit unique-rig contracts. Composer windows compile to existing native action assets/WorldSession; preview cues cannot cause damage. New motion warping, charge/burst/parry/block/invulnerability rules require authoritative acceptance, not only visible controls.

## Level design direction

Prioritize fast placement of Ashen Roots objects/game systems and mesh-first environments. Extend the locked M6 terrain, surface painting, splines and scatter tools as their products become usable. Advanced terrain sculpting is not mandatory for the first productive level-design loop.

M6 develops the required mesh-first terrain path, coherent collision/navigation products, sculpt modifiers, surface/material/biome painting, spline generation and batched scatter under DAE-015/016/017. Stacked surfaces, caves and floating islands must retain their 3D geometry and paint scope. Measure edit/cook responsiveness and runtime cost on representative content.

Advanced volumetric sculpting and a broader texture-painting suite remain later proposals. They do not introduce runtime voxels, digging/destruction or an unqualified geometry dependency. Precise Royal terrain/hut collision remains an earlier M4/M5 task.

## Milestone integration and acceptance

The authoritative [roadmap workstream](../architecture/DarkAngel_Implementation_Handoff.md#ashen-roots-editor-workflow-workstream) specifies M1–M9 authoring scope, dependencies and observable extension gates alongside unchanged runtime gates. M1/M2 completion is not reopened. M3 EOS remains separately blocked; M4/M5 remain In progress. Do not infer delivery from a workflow name.

Each workflow increment must provide a useful layout, relevant create/select, typed editing, compatible picking, bounded preview, visible validation/Save/cook, related-workflow links and testing through fresh Game. Reuse Inspector, Asset Browser, Console, Dependency Viewer, Cook Monitor, Tag Browser, Input Mapper, physics/nav/network/animation/ability/effect/world debuggers, Profiler, Validator, History and Scene Search as needed; optional Git status is lightweight. The vision owns the panel responsibility list.

## Implementation sequence from the current checkpoint

1. Preserve delivered native source edit/create, Ability/Effect/action forms, typed fields, kit assignment/effect composition, grouped draft history and coordinated Save. Current scope/limits are in NATIVE_AUTHORING_REPORT and NATIVE_AUTHORING_HISTORY_REPORT; no full visual Composer is implied.
2. Follow [ABILITY_AUTHORING_CONTINUE.md](ABILITY_AUTHORING_CONTINUE.md): read-only native tick ruler/block lanes and scrub selection with isolated compatible frozen clip/character pose preview. Scrubbing never activates gameplay. Keep existing numeric editing/history/Save.
3. Add focused structural blocks/timeline dragging, graph/layer/mask tools and broader thumbnail/tag picking; extend Character/Player kit assignment and approved modifier/status presets. New tag definitions retain rebuilt-consumer validation; broader formulas/triggers remain separate runtime increments.
4. Preserve complete Save → Cook → Fresh Play: validate/cook changed consumer closure, rebuild full scene/model/skin/rig/animation/input/kit package, reprepare GPU/CharacterPreviewResources and isolated probe, then start a new WorldSession. Reject dirty drafts, failed publication and stale generations visibly; preserve prior packages. Never only swap a kit or silently patch a live session.
5. Extend NPC/world, audio/VFX and progression/UI tools with their native milestones. Qualify integrated production usability in M9. Use focused tests per editor increment; full historical matrices belong at actual integration checkpoints or changed shared boundaries.

Creation/deletion Undo, automatic crash recovery, background preparation, broad picker and production-quality designers remain planned. Source files/SQLite publication is normal-failure recoverable, not crash-atomic. Existing numeric action forms and shared action duplication limits remain explicit.

## Combat-kit and locomotion direction (9 October 2026)

The initial game-facing kit exposes Light Attack, Heavy Attack, Ranged Light, Ranged Heavy, Spell 1, Spell 2, Block and Parry. Each slot binds a distinct stable InputManager action, and may have no ability assigned. The ability owns interpretation of Pressed/Hold/Released/Tapped, charge thresholds and cancellation; the kit does not convert all slots into press-to-cast. Shared ability definitions may occupy multiple slots with separate grant/slot identity. Future game profiles may define fewer/more slots; the initial eight-slot layout is an explicit bounded profile rather than a universal engine constraint.

A kit references its locomotion stance/set. Character authoring assigns a base locomotion set and a combat kit; the equipped kit can select the combat stance. Runtime replacement must prepare outgoing action/grant cleanup and stance transition, publish atomically, preserve attribution of already applied effects/projectiles, and reject stale input/grant events. Replacement does not imply resource refunds or clear unrelated effects. Ashen Roots mask items can reference kits; a future game-owned Luau equipment script requests kit swaps. No mask/equipment mechanism is to be baked into the engine or implemented in this increment.

The locomotion set is a native versioned Animation Graph asset, conceptually serving the animator-controller role. Extend the existing compiled clip/1D/2D blend plan into typed bool/int/float/enum parameters, declared enum domains, validated transitions and state/event inputs. Scripts use checked IDs and typed setters, never string-driven arbitrary mutation or direct pose/root authority. Character state includes IsInAir and typed OnJumped/OnLanded/OnDamaged events, with simulation tick and stable occurrence identity. Graphs consume committed or explicitly predicted character state; replay deduplicates presentation and events. Locomotion, combat layering/masks/additive slots and Action Composer timing must share the canonical rig, immutable generations and existing motor-achieved root ownership.

Ability/stat scope includes Health, Stamina and Essence resource values with explicit maxima, and extensible statistics such as Poise, typed elemental defence and Block. Final damage/defence/poise/block formulas are game-owned and still require design. Native costs/reservations, effects/tags, cooldowns, authoritative hit validation and predicted operation inclusion remain DAE-008/009 work. Do not interpret the scalar attribute prototype as a complete elemental damage resolver.

Current implementation is limited to tested native combat-kit routing/replacement and attribute calculation primitives. Asset cook/Character authoring, World/WorldSession grant lifecycle, Luau bindings, action cancellation, typed graph parameters/events and gameplay activation are subsequent steps; see [combat foundation report](COMBAT_FOUNDATION_REPORT.md).

Implementation continuation: the server-only WorldSession immediate-commit/action-lifecycle subset now extends these primitives; see [ABILITY_COMMIT_REPORT.md](ABILITY_COMMIT_REPORT.md). It does not implement predicted operations, damage/effects, composer rendering or editor authoring.


Implementation continuation (9 October): Latest asset increment (9 October): [frozen Ability/Attribute assets](ABILITY_ASSET_REPORT.md) adds versioned native sources, atomic action/schema cooking, dependency generation validation and source-free loading into the existing WorldSession grant/commit API. CombatKit/AnimationGraph cooking and reflected editor authoring remain open; Royal still has no playable hit/damage. M4/M5 remain In progress. Reproduce `python scripts/verify_ability_assets.py --build`.


Latest melee increment (9 October): [authoritative motor-relative melee](MELEE_REPORT.md) adds authored HitWindow profiles, a bound native post-physics query, registered game-owned damage evaluators, atomic Health/death/deduplication and existing Loopback baseline integration. Royal input/attack presentation, canonical socket sweeps, ability wire/prediction and the remaining M4/M5 gates are still open. Reproduce `python scripts/verify_melee.py --build`.


Latest graph increment (9 October): [frozen native locomotion graph](GRAPH_ASSET_REPORT.md) adds versioned `.dagraph` cooking, a coherent frozen clip/rig closure and source-free generation-fenced loading. Royal now consumes its cooked nine-clip graph in the primary editor; 7/7 affected native checks and 120-tick D3D12/Vulkan checks pass. CombatKit cooking, typed graph states/events/layers/actions, playable Royal ability/damage and full M4/M5 gates remain open. Reproduce `python scripts/verify_graph_assets.py --build`.


Latest kit increment (9 October): [frozen CombatKit assets](COMBAT_KIT_ASSET_REPORT.md) adds `.dakit` with eight optional ability slots, declared input actions and a coherent frozen input/attribute/ability/graph-stance closure. 9/9 affected native checks, the full-editor fixture and 4/4 ordinary Headless checks pass, including existing WorldSession active-grant replacement without refunds. Royal remains locomotion-only; Action Composer clip/root binding, playable combat, authoring, prediction/observer and full M4/M5 gates remain open. Reproduce `python scripts/verify_combat_kit_assets.py --build`.


Latest action increment (9 October): [frozen action clip binding](ACTION_CLIP_BINDING_REPORT.md) adds schema-2 Action Composer clip/rig/root-policy cooking through the Ability/CombatKit closure and a bounded timeline-local root helper for the existing motor. 16/16 affected native checks, full-editor binding fixture, feature-disabled rejection and 4/4 Headless checks pass. Real Jolt wall collision and synthetic 30/60/144 traces are checked. Royal input/attack/target integration, pending prediction/observer state, authoring and full M4/M5 gates remain open. Reproduce `python scripts/verify_action_clip_binding.py --build`.


Latest playable increment (9 October): [Royal native melee integration](ROYAL_COMBAT_REPORT.md) connects frozen kit/attributes/ability/action/clip assets to the existing CharacterSceneSession, serialized protocol-3 intents and atomic owner corrections, Jolt-achieved root motion, native melee damage and Game Health/Stamina display. Primary `scripts/open_royal_scene.ps1` uses the new RoyalCombat scene; other seven slots remain unassigned. 12/12 affected native checks, full-editor fixture, D3D12/Vulkan combat/active-pose captures and 4/4 Headless checks pass. M4/M5 remain In progress. Exact remaining gates are in the report. Reproduce `python scripts/verify_royal_combat.py --build`.

Usage stop (9 October): the requested threshold was reached: five-hour remaining 4%, weekly remaining 24%. Implementation stopped after this verified checkpoint. Next coherent increment is pending owner ability/cost/combination prediction with exact operation inclusion and dependent replay, followed by observer action/public state and active-state late join. Effects/tags/Luau composition, typed graph/layer/mask/socket/authoring, projectile/combo and full timing/fault/streaming/performance gates remain open. M3 live EOS remains externally blocked. No push or usage reset was performed. Obtain exact HEAD with `git log -1 --oneline`; the checkpoint tree should be clean.
