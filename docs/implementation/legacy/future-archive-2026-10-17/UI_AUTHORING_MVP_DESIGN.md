# UI workspace and reusable document MVP

**16 October 2026: U0/U1 and U2 are implemented and locally verified.** The legacy HUD is removed at the user's request: all gameplay UI uses cooked documents through UiService. The guarded full build and resumed acceptance gates passed; the single full-CTest fixture collision was fixed and its isolated retest passed. See the [R6 report](../current/UI_AUTHORING_REPORT.md) for decisions, evidence and remaining U3 scope.

**Scheduling: Planned, R6.1 (B: U0/U1), R6.2 (D: U2); E: U3 is in the deferred backlog (formerly R8, deferred 16 October 2026)** in the [authoritative roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026). R6 follows R5 and uses the [UX standards](EDITOR_UX_OVERHAUL_PLAN.md). U3 world-panel work must not block the screen/menu examples. U0–U3 here supersede the older UI_PLAN stage numbering.

15 October 2026. **Proposed design, not an implemented UI tab or API.** Resume through [next authoring work](AUTHORING_NEXT_STEPS_2026-10-15.md). This updates the current-state assumptions and sequence of the [12 October UI plan](UI_PLAN.md); [DAE-010](../../architecture/Decision_Log.txt) remains the locked contract.

## One document system, three surfaces

Use the same RmlUi document asset, bindings and command path for a HUD element, menu, NPC nameplate and puzzle panel. Keep a document's **purpose** (HUD/menu/puzzle/nameplate) separate from **surface**:

| Surface | Use | Ownership and presentation |
| --- | --- | --- |
| `Screen` | Player HUD elements, main/pause menus, full-screen puzzle | One local viewport's HUD layers or screen/modal stack |
| `WorldAnchor` | NPC health/name/awareness, player labels, interaction marker | Projected actor/object/socket position; bounded pooled documents in that viewport |
| `WorldSurface` | A panel on a wall, rune terminal, in-world puzzle | RmlUi rendered to a bounded texture on a planar scene quad, depth-tested and ray-picked |

A document declares permitted surfaces and defaults; an attachment/open request selects one. Health-bar markup can support Screen and WorldAnchor without duplicating its rules. A puzzle document can permit Screen and WorldSurface with the same typed puzzle model. The attachment owns transform/anchor; RML owns its local layout. Source preview of a WorldSurface document is possible before its 3D backend ships, and must be labelled **2D preview; 3D runtime pending**.

## What exists now, checked against source

- `Hud` owns RmlUi process initialization/shutdown and rejects a second concurrent Hud. It hosts the main HUD plus inventory and altar documents in one context; it is no longer literally one document.
- `games/AshenRoots/ui/hud.rml` and its sibling documents load by path. Play uses `DAE_GAME_UI_DIR` and `C:/Windows/Fonts/segoeui.ttf`; these are not AssetService UI assets or packaged font dependencies.
- Inventory/altar have native models, queued actions and pointer/wheel ingress. There is no general screen stack or complete keyboard/controller UI router.
- `world_ui_presenter.cpp` projects NPC bars and player labels using observed client state and the rendered camera. They are rows in the existing HUD's `world_markers` model, not authored per-NPC UI document attachments. They have no scene-depth occlusion.
- `ImGuiHudRenderer` composites RmlUi geometry in the editor Game viewport. Font atlas textures work; external image `LoadTexture` returns zero. General Diligent offscreen UI/world surfaces do not exist.
- `Workspace` has no UI entry; `AssetImportType::UI` is a placeholder. Current ScriptRuntime's world Context has yaw and task-wait methods, not `ui.open` or a UI command bridge.

This is enough to reuse models/actions, projected positions and editor hosting. It does not already provide the requested document authoring or script orchestration.

## The first UI tab

Add a fixed **UI** workspace alongside the current workflows. The initial layout is:

```text
UI documents             Document: Rune Puzzle       Save / Undo / Redo
Search / New / Duplicate  Surface and binding contract / dependency status
                         RML | RCSS | Descriptor       Live preview
HUDs                     source text + diagnostics    aspect / resolution
Menus                                                  sample model values
World markers                                          emitted command log
Puzzles
```

The UI workspace owns no renderer, session or gameplay. A narrow context supplies asset commands, a preview host, sample model providers and diagnostics. Reuse the existing native draft/history/publication owner, extended for text bundles. Do not move UI authoring into PlayController or make ImGui the game's document framework.

MVP controls:

- **New** from HUD Element, World Marker, Menu/Screen or Puzzle Panel templates; name/folder, binding contract and permitted surfaces; **Duplicate**, **Delete** with consumer validation, Open/Reveal references.
- An RML text editor, RCSS text editor and typed descriptor/dependency form. Source editing is the initial supported authoring workflow; a drag-and-drop layout designer is later work under the vision/DAE-010.
- Live preview using the same supported RmlUi dialect/backend as Play. Resolution/aspect presets and scale; WorldAnchor preview has sample distance and actor/object position. A read-only element outline/selection highlight can follow without becoming a layout editor project.
- Typed sample values: health/current maximum, name, cooldown fraction, menu flags or puzzle state. A new game-specific model exposes its sample generator through the same registration contract as its runtime projection.
- Source/line diagnostics where supplied, otherwise a document/dependency diagnostic. Failed parse/layout preparation retains the last good preview with an obvious stale badge and preserves the invalid draft. Invalid candidates cannot publish.
- Preview clicks execute local presentation interactions and log typed commands; they never start a game, grant inventory, solve a puzzle or write saves. A selected element's stable ID helps focus/debugging.
- Undo/Redo, explicit Save, dirty marker, external-change/stale-source detection. Changing workspace preserves drafts. Save publishes a validated bundle, then refreshes preview/dependent authoring consumers.

Useful first delivery: create a Stamina ring/bar, edit its RML/RCSS, change sample values, undo, Save, reopen, assign it as a Player HUD attachment, and see it in fresh Play. Document creation/preview can land just before attachment integration, but the report must identify that boundary accurately.

## Asset and multi-file draft contract

Proposed folder layout (new, not created by this design):

```text
content/ui/hud/stamina.daui       # native identity/kind/schema + descriptor
content/ui/hud/stamina.rml        # owned markup source
content/ui/hud/stamina.rcss       # owned style source
content/ui/shared/...             # shared templates/styles, with explicit dependencies
content/ui/fonts/...              # shipped font and its license
content/ui/menus/main_menu.*
content/ui/puzzles/rune_panel.*
```

The `.daui` is the logical AssetID owner, with name/version, markup/style references, declared font/image/template dependencies, binding-contract ID/version, allowed command IDs, permitted surfaces and default logical size/scaling. World surface metres/pose belong to the attachment, not the document. Fonts/images resolve through cooked asset dependencies and virtual URLs; no Windows font path or arbitrary runtime source lookup.

Extend AssetService import/scan/cook/load and coordinated native publication. The cooker records all transitive RML includes/templates, RCSS imports, images and fonts in the frozen closure; declare the supported URI/property/widget profile and reject unresolved or unsupported required features with diagnostics. Check pinned RmlUi headers/source before choosing adapters; no dependency upgrade or optional ordinary-Lua plugin is needed.

A UI draft consists of the descriptor plus owned text members. Existing NativeAuthoring JSON drafts are not already a multi-file text editor: add bounded text-member commands/history to that owner. An edit/Undo/Redo group covers related changes; `New`/`Duplicate` stage all new owned members with the descriptor and fresh identity. Shared stylesheet edits have their own owner and consumer list; duplicating a document copies its owned files, while preserving shared dependency links deliberately.

Prepare source bytes, revisions and transitive dependencies privately, parse/bind the candidate, then publish descriptor/text heads/catalog as one recoverable transaction. An interrupted commit, stale writer or parse failure must not leave a saved descriptor pointing at half-written markup. Raw file copy followed by unrelated JSON Save is insufficient. Existing native draft size limits must be checked and explicitly extended for bounded UI text members; do not globally remove limits.

Preserve authored element IDs across reload where possible. A successful preview reload may restore local selection/focus/scroll after verifying compatible IDs; gameplay state remains owned by its native model. Fresh Play uses frozen cooked generations. Hot-replacing a running gameplay document is a separate explicit development feature, not an automatic side effect of Save.

## Runtime ownership and bindings

Introduce a client-side **UiRuntime** (called UiService in the older plan) owning RmlUi initialization once per process. Editor preview and Game each get bounded contexts/surfaces under it. The existing Hud becomes a compatibility consumer while its documents/models move incrementally. This shared lifetime is required before the UI preview can coexist with a running Hud; a second direct Hud initialization is not safe.

Keep document instances independent: two NPC nameplates share an asset but have different models, instance handles and lifetimes. Multiple local viewports have separate focus/HUD/anchor state. Resolve logical binding names into per-instance model names inside the adapter, using pinned supported APIs and trusted load-time instantiation; do not bind every same-asset instance to one mutable global model. Model storage outlives all referencing documents and handles.

Start from existing typed models and registration adapters:

| Binding contract | Projection source |
| --- | --- |
| Owner HUD | Confirmed owner state; explicit existing predicted presentation where supported |
| Actor public / Target | Checked observed actor state, public Health/name/status and permitted awareness fields |
| Inventory / Altar | Existing inventory and altar models/action adapters |
| Front end / Settings | Application flow and local settings drafts, available with no scene |
| Game-specific puzzle | Registered game model keyed by a checked scene object, projecting the permitted puzzle state |

The engine provides typed model registration and command dispatch. Ashen Roots supplies the meaning of Health, masks and puzzle states. Avoid adding every puzzle field to `HudViewModel` or inventing a free-form global string/event bag. External text is text, not trusted RML. Dynamic list sizes and model updates remain bounded and revision-driven.

## How authors attach and use documents

**New Player HUD element:** UI > New > HUD Element; choose owner binding, edit/preview, Save. Player > UI > HUD > Add chooses the document and order/visibility. Several independent Screen attachments can show health, stamina and interaction prompts; updating one shared document updates its consumers on fresh preparation. Existing HUD, inventory and altar adapters migrate without replacing their gameplay request paths.

**New NPC or other-player marker:** UI > New > World Marker; choose actor-public binding and Save. NPC > UI > World Anchors > Add chooses document, root/socket/offset, scale and distance/visibility policy. Player has the analogous public marker list, normally hidden from its owner. Share checked rendered-pose attachment code with cue attachments where suitable; do not read authority-only NPC state on a guest. Begin with current projection/clipping/distance behaviour; budgeted line-of-sight occlusion is explicit follow-up.

**Scene object marker/panel:** Scene/Prefab Inspector > Add Component > UI Attachment. Choose document, WorldAnchor or WorldSurface, anchor/offset and binding target (default self). WorldSurface additionally has planar size in metres, render resolution and input/range policy. This becomes a normal prefab component/override through the prefab command system. A screen-space puzzle can be opened by interaction before 3D surfaces are ready.

**Main menu:** UI > New > Menu/Screen, choose front-end binding and commands such as Start/Continue/Settings/Back/Quit. Preview works without a world. The application/game definition selects main, pause and loading screens; a small FrontendController handles boot/menu/loading/in-game and session lifecycle using Play's service boundary. Editor Play keeps direct-to-scene as default, with **Start at Front End** for testing. A sessionless editor harness can prove menu commands before a standalone game executable exists; a packaged main menu is only claimed once the standalone host exists.

**Bespoke puzzle:** New > Puzzle Panel, choose a registered puzzle contract, author buttons/status and Save. The object's interaction handler opens that document for the interacting local player, or mounts it as a WorldSurface. Multiple terminals can share the document while binding different puzzle object IDs. Puzzle rules live in the native/Luau gameplay controller, not in markup or a UI-only copy of puzzle state.

## C++ and Luau: proposed orchestration API

The following is **illustrative API design, not callable current code**. Use one generation-checked facade for local UI operations; generated asset/schema/command identifiers should replace ad hoc strings in production.

```cpp
// Client/application code. Binding factories return checked, typed model handles.
auto menu = ui.open(UiAssets::MainMenu, frontend_model, Screen, Modal);
auto puzzle = ui.open(UiAssets::RunePanel, game.puzzle_model(object), Screen, Modal);
auto marker = ui.attach(UiAssets::NpcMarker, game.public_model(actor), WorldAnchor, anchor);
ui.close(puzzle); // Also closed automatically with its owning scope.
```

```lua
-- Client presentation Luau facade; UI/model/asset handles are host-validated.
local panel = ui:open(assets.ui.rune_panel, models:puzzle(object), {
    surface = "screen", input = "modal"
})
ui:close(panel)
```

Each handle has runtime/viewport/instance/generation ownership, plus a scope: application/front-end, current scene epoch, actor/object or script binding. Closing is idempotent. Closing a screen, removing its actor/object, scene unload, disconnect and script handler reload cancel owned subscriptions/pending UI callbacks and release focus/resources. No raw RmlUi DOM/context pointers reach scripts.

C++ registers typed command handlers and model projections. The Luau facade supplies bounded orchestration and typed callbacks in the existing sandbox, not RmlUi's optional standard Lua VM. It must be deliberately integrated: current world ScriptRuntime and behaviour-tree leaves are not client UI controllers and should not gain unrestricted rendering access. First implementation provides a narrow local presentation host/registration contract; game logic scripts can request presentation through the bridge below.

### Opening UI from gameplay and returning puzzle input

```text
Player interacts -> host validates object/range/permission
 -> targeted presentation request for that player and scene/object epoch
 -> local client controller ui.open(document, checked model)
 -> button emits registered typed puzzle command
 -> existing gameplay request/authority path validates it
 -> confirmed puzzle state updates the view model (all relevant clients)
```

A server-side C++/Luau handler can enqueue an **OpenUi presentation request** containing a registered document/route, recipient, checked object/model reference and scope. The local client resolves it through the same facade. This targeted request adapter and any needed versioned transport message are new implementation work; server/headless code never constructs UiRuntime or a document. Do not route it through an unrestricted global event string, or assume `signal_event` is already a network UI channel.

A click carries a registered command ID, bounded typed payload, instance/object/scene scope and operation ID where it reaches gameplay. The native command registry defines permission and payload schema; a document's declared command list only narrows that registry. Discarded/stale/duplicate requests do not mutate the world. Inventory/altar requests reuse their current host adapters; a new puzzle request needs its own validated game adapter using the established transport, not a second authority path. Pending/rejected actions are reflected in the model.

## Focus, visibility and rendering

Use a UiInputRouter with one focus owner per local viewport. HUD/passive anchors do not capture gameplay by default. The top modal blocks gameplay actions, releases/cancels held movement/combat through the normal input path, and owns pointer/keyboard/controller navigation until closed. Consume an action once; closing a modal cannot turn its held Confirm into an attack. Restore focus to a surviving stable element. Opening a menu does not pause a multiplayer host simulation.

MVP preview pointer input logs commands. Interactive runtime menus need keyboard/controller focus, remappable navigation/confirm/cancel, text/wheel handling and repeat policy before they are considered complete. Keep application-local commands (open settings/back) separate from gameplay requests (submit puzzle/inventory action).

WorldAnchor remains projected overlay UI with clipping and bounded distance/priority selection; no texture or context per NPC. WorldSurface uses a Diligent offscreen RmlUi backend and bounded pooled texture/context work. A ray hit is converted from quad-local coordinates to document pixels. Choose the closest eligible visible surface; require a fresh visibility hit for press, define pointer capture/release and cancel on disappearance. World geometry blocks interaction; server range/permission checks still decide gameplay commands.

Keep DAE-010 colour/alpha, transforms, clip masks and resource-fence contracts. The current draw-list adapter's image and clipping limits must be declared in preview; either implement a feature before advertising it or diagnose it. Ship a redistributable font asset/license and texture loading in the useful document MVP. Full Diligent backend/world render targets follow as one renderer increment, with screen output after scene tone mapping and explicit world-texture colour handling.

## Delivery and acceptance

| Increment | Scope | Acceptance |
| --- | --- | --- |
| U0 | Multi-file `.daui` contract/cooker, virtual dependencies, shared RmlUi lifetime | Existing HUD/inventory/altar checks; preview + Play lifetimes; cold cooked load without source/font paths; failed bundle publication retains last generation |
| U1 | UI tab, four templates, source/style/descriptor drafts, preview/sample values/command log | New/duplicate/edit/Undo/Redo/Save/reopen; invalid source retained; two independent preview instances; unsupported features diagnosed |
| U2 | Player HUD + NPC/Player WorldAnchor lists; Screen stack/input; C++ and local Luau facade; sessionless menu and screen-puzzle examples, targeted UI requests and host-validated puzzle adapter | Assigned Stamina HUD and NPC marker visible in fresh Play; focus isolation; independent same-document models; stale-handle and reload cleanup; menu without a scene; screen puzzle uses approved requests and shows host-confirmed results |
| U3 | Scene/prefab UI Surface (WorldSurface attachment), offscreen renderer/ray input; extend the U2 targeted gameplay/puzzle adapter to physical panels | Screen and physical-panel versions of one puzzle; occluded/out-of-range/stale presses refuse; authoritative result seen by both clients; travel/disconnect cleanup |
| Later | Packaged front end, richer widgets/layout designer/localization workflows and optimizations | Qualification on standalone client and intended devices; no claim from editor preview alone |

Follow R6 in the main roadmap after R5; technical independence of preview from prefabs does not override that priority. U0/U1 deliver 6.1; U2 delivers 6.2, including screen-puzzle examples through the targeted/host-approved path described above. U3 (deferred backlog, formerly R8) adds physical-panel rendering/input using that same adapter. A standalone executable is not required for the sessionless menu harness. U1 may preview WorldSurface-capable markup in 2D but must not claim 3D placement support. UI Surface is the author-facing component backed by a WorldSurface attachment, not another document system.

Use focused asset/model/lifetime/input tests and one UI authoring gate for U0-U2, retaining existing HUD/inventory/altar regressions. Broaden renderer checks to D3D12/Vulkan at U3, including scissor/transformed clipping, images/font alpha, resize/device resource lifetime and world hit projection. Run a separate-process party gate when the targeted-open/screen-puzzle request path lands at U2, then extend it for physical panels at U3; retain the same-machine qualification. A screenshot alone does not prove focus, independent models or authority validation.

Source owners to revisit after refactor: runtime `hud.*`, `inventory_document.*`, `altar_document.*`, `world_ui.*`, script integration; editor `hud_overlay.*`, `world_ui_presenter.*`, `play_hud.*`, native authoring, workspace/navigation and Play resources; AssetService/cooker; game `royal_hud` and new frontend/puzzle adapters. Introduce focused `UiWorkspace`, `UiRuntime`, `UiInputRouter` and command/model adapters with narrow contexts. Avoid another monolithic HUD class or a second gameplay event/VM system.
