# Next authoring work: prefabs, configuration, UI and rendering

15 October 2026. **Design and handoff only; no feature code, build or runtime verification in this pass.** Requested while Claude is completing the editor refactor. Expanded the same day with researched skybox, procedural sky, lighting/fog/bake, post-processing and Assets material/prefab workflows.

## Read these next

1. [Prefab placement and swapping](PREFAB_PLACEMENT_DESIGN.md): create reusable gameplay objects, find them in **Place**, edit definitions and replace a placed instance's prefab.
2. [Gameplay configuration](GAMEPLAY_CONFIGURATION_DESIGN.md): movement profiles for both Player and NPC, the ownership of other settings, and the deterministic runtime path.
3. [UI authoring MVP](UI_AUTHORING_MVP_DESIGN.md): a fixed UI workspace, document creation/editing/preview, HUD and world attachments, menus and puzzle screens, and C++/Luau orchestration.
4. [Environment, lighting and post processing](ENVIRONMENT_LIGHTING_POST_PROCESSING_PLAN.md): cubemap/equirectangular skyboxes, Tools > Procedural Skybox, Tools > Lighting, fog, real scene baking and a profile-driven post stack.
5. [Assets material and prefab editing](ASSETS_MATERIAL_EDITING_PLAN.md): imported-model material values/texture swaps that survive reimport, plus Assets > Edit Prefab into the dedicated workflow.

These are proposed implementation contracts within the existing vision and locked architecture. They do not mark features implemented or create new locked decisions. The 15 October user request brings UI authoring back into the next-work plan; the older UI plan's deferral and scheduling questions are historical.

## Existing plans and what this adds

- [Prefab workflow proposal](../PREFAB_WORKFLOW_PLAN.md), 14 October: detailed assembly ownership, publication, override and retained-identity design. Preserve it, including its untracked-file ownership. The new prefab document extends it with the requested **Swap Prefab** and quick-find flows, and treats compatible swapping as part of the first complete workflow.
- [Prefab/component TODO](PREFABS_AND_COMPONENTS_TODO.md): the original product request. Its statement that most missing work is editor work is qualified by the newer proposal's source findings: external links, mount transforms, gameplay patches and publication need real implementation.
- [UI plan](UI_PLAN.md), 12 October: useful starting design, but its current-state inventory is stale. Projected actor markers, pointer input, inventory and altar documents now exist. Asset-backed documents, general screen orchestration, the UI tab and 3D world surfaces still need work.
- [Engine vision](../../vision/DarkAngel_Engine_Vision.md), [Actor gameplay design](../../architecture/Actor_Gameplay_Design.md) and [DAE-010](../../architecture/Decision_Log.txt) remain the source of product ownership and runtime UI boundaries.
- DAE-005 already defines versioned materials, HDR/render-stage ownership and the direction for lighting. The new rendering plan turns the user's request into future editor/runtime increments; it does not mark deferred baking/probes or unintegrated DiligentFX components as delivered.

## Shared-checkout boundary

At inspection, Claude's uncommitted refactor touched `CMakeLists.txt`, `apps/editor/main.cpp`, the new `editor_app`, `play_controller`, `play_hud`, `viewport_tools`, rendering services and exercises. Hidden `DarkAngelEditor.exe` verification processes were running. The new systems all need some of those same integration points, plus AssetService and shared actor/scene contracts.

The initial pass added four design documents. The rendering follow-up adds two linked plans, updates their configuration/prefab cross-references and the implementation-doc index; it is also documentation only. Neither pass edits Claude's code, shared STATUS/CONTINUE headers, existing prefab proposal, content, evidence, build outputs or dependency state. No build, editor launch, commit, staging, push or message to another task is performed. A worktree alone would isolate source edits but would not make shared verification/dependency activity safe, or settle the moving service contracts.

## Recommended implementation order after a verified refactor checkpoint

| Increment | Reviewable result | Readiness/acceptance |
| --- | --- | --- |
| A. Movement authoring | One native movement profile, reference and shared editor form in Player/NPC; save/cook/fresh Play | Unchanged defaults; a slower NPC and faster player; authority/prediction/replay use the same frozen parameters |
| B. UI editor MVP | Asset-backed UI bundles, shared RmlUi lifetime, fixed UI tab, source/style editing, preview and sample models | Create/duplicate/edit/save/reopen; malformed draft retains last good preview; preview and Play coexist |
| C. Useful prefab loop | Add Component/Model commands, external prefab links, mount placement and prefab workspace; searchable Place; compatible swap | Two scenes, distinct instance identities/state, override/Revert, swap preserving placement and compatible references; fresh collision/Play |
| D. UI in gameplay | Authored Player HUD/NPC markers, screen stack/input, local C++/Luau facade, menu and screen-puzzle examples | Two independent instances; modal blocks gameplay; typed commands and lifecycle cleanup; front-end preview without a session |
| E. World puzzle UI | UI Surface component/prefab, Diligent offscreen renderer, ray-to-panel input, host-validated puzzle request adapter | Depth/picking/range checks; two-client puzzle outcome; close on unload/disconnect |
| F. Prefab publication completion | Atomic Apply Selected and identity-preserving selection-to-prefab conversion | Retained save/reference identities; interrupted/stale publication recovery; no partial scene/prefab writes |
| G. Assets material workflow | Edit imported model slot values/texture references; native material instances; Assets Edit Prefab route | Saved edits survive stable-key reimport; shared/local scope is explicit; generated defaults remain immutable; one prefab draft owner |
| H. Sky and Lighting tools | Cube/panorama sky assets, procedural generator, Lighting environment/fog/light sections | Correct HDR/projection/seams; background and lighting independent; saved recipes; supported controls match actual renderer capabilities |
| I. Post-processing profiles | Lighting > Post Processing, scene/camera profile selection and legal effect stages | One color/output pipeline; per-view isolation; scene effects do not process screen HUD; invalid dependencies/order reject |
| J. Scene light baking | Bake settings/status, UV/backend spike, then static indirect lightmaps and later probes | Real sampled bake products; placement bindings; no double lighting; stale/cancelled jobs preserve old results; packaged runtime loads them |

A and B are small visible deliveries and need not wait for the complete prefab system. C has deeper source/document prerequisites; begin with the foundation and command tests from its design, not a Place-only button. Basic overrides and Revert belong in C; coordinated Apply and conversion can land in F. Swap belongs in C, not the later nested-prefab/variant work. E builds on the same UI assets, never a second puzzle UI format.

G-J are a rendering/Assets stream, not a requirement to finish E/F before adding skies or material editing. Basic material editing can start after the shared AssetService/renderer boundaries settle; Assets Edit Prefab waits for C's owning workspace. H/I share texture/HDR/material resources; J requires actual UV/lightmap/baker support and is deliberately later than a settings-only panel. Preserve the original task priorities until the user chooses a different sequence.

## Resume instructions

Read the newest paragraph of `CONTINUE.md`/`STATUS.md` and the end of `current/REFACTOR_REPORT.md`; check working-tree ownership and active build/editor processes again. The current report is historical evidence, not permission to run concurrent checks. Do not use the refactor's behaviour-preserving prompt as this feature task's scope.

Start A as a coherent implementation and verification chunk, then B, unless the user supplies a different priority. Use focused existing native test families and one authoring gate per visible increment. Broaden once at shared resolver, prediction/protocol, publication or renderer integration points. Keep failed receipts and exact remaining work in each implementation report; add a newest continuation entry when feature implementation actually starts. Preserve protected Royal scene sources, `.records` directories, existing staged/untracked work and pinned build/dependency state. Do not push without explicit authorization.

## Verification of this design pass

Repository source and existing plans were inspected read-only. The rendering follow-up also consulted primary DiligentFX, Khronos, Blender, Unity and xatlas sources, linked next to the supported claims in its plans. Proposed file kinds, fields, commands and API examples are not available today. Markdown links, code fences, whitespace and the documentation diff are checked separately; no build/test evidence is claimed for a documentation-only change.
