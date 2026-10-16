# R6.1 UI Authoring (first part)

16 October 2026. **R6.1 delivered as a first working subset and locally verified; R6.2 (gameplay integration) is not started.** This covers the start of R6 (B) in the [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) against the [UI MVP design](../future/UI_AUTHORING_MVP_DESIGN.md) (U0/U1). It was implemented autonomously overnight; the scope cuts and decisions below were taken without the user and are listed so they can be revisited.

## Delivered behavior

- **UI documents** (`.daui` under `ui/hud`, `ui/markers`, `ui/menus` or `ui/puzzles`): one native source holding the descriptor (name, purpose `hud`/`marker`/`menu`/`puzzle`, permitted surfaces `screen`/`world_anchor`/`world_surface`, logical size) and its owned RML markup and RCSS style. They cook like the other native sources (`ui.json` product) through the same drafts, Undo/Redo, validation and coordinated Save. Documents must be self-contained for now: links, scripts, templates, `@import` and `url(...)` are refused.
- **Four templates**: Stamina bar (HUD element), Nameplate (world marker), Main menu (buttons with commands) and Rune panel (puzzle buttons and a solved flag). All bind the `ui` data model: health and stamina percentages and text, cooldown, name, prompt and a flag. Clicks use `data-event-click="command('id')"`.
- **UI tab** (new fixed tab after Prefabs): documents grouped by purpose, *New from template*, *Duplicate*, purpose and surface controls, RML and RCSS text editors, Save, Undo/Redo and validation diagnostics. A live preview uses the same RmlUi backend and renderer as the game HUD, with an aspect choice and sample values (health, stamina, cooldown, flag, name). Clicks in the preview are logged as commands and never change the game. Load problems are shown, and a broken edit keeps the last good document on screen with a *Stale preview* badge. World-surface documents are labelled *2D preview; 3D runtime pending*.
- **One RmlUi owner at a time**: the preview and the game HUD share the process's single RmlUi lifetime. Starting Play evicts the preview (the HUD wins); the preview returns after Stop. Previews pause during Play, like the editor's other previews.

## Decisions and scope cuts taken while the user was unavailable

1. **Markup and style are stored inside the `.daui` source**, not as sibling `.rml`/`.rcss` member files. One file means one atomic native Save and no multi-file draft machinery yet. Splitting into owned member files, with shared stylesheets and virtual dependencies, is part of the remaining U0 work.
2. **No shared UiRuntime yet**: instead of a preview that coexists with a running HUD, the preview is evicted during Play. The design names coexistence as a requirement for preview *alongside* Play, so this is a deliberate partial step.
3. **Fonts are not packaged**: the preview uses the same system font path as the existing HUD (`segoeui.ttf`). Packaged font assets and their licence remain U0 work.
4. Templates and the `ui` sample model are engine-provided; game-specific binding contracts and their registration are R6.2.

## Verification

All on this machine (D3D12, pinned toolchain, `VCPKG_MANIFEST_INSTALL=OFF`; no third-party objects rebuilt). Same-machine evidence only.

- **New `R6.ui_documents` test** (passed) covers:
  - the four templates decode, and scripts, `@import`, duplicate surfaces and unknown purposes are refused;
  - every template loads, binds the sample model and draws in the preview;
  - sample stamina and name values reach the document text;
  - a click on the menu's Start button logs `menu.start`;
  - a broken document is refused and the last good one stays shown, marked stale, and still updates;
  - creating a Hud evicts the preview through its owner, and no preview can start while the Hud runs;
  - `.daui` sources cook, and invalid markup is refused at cook.
- **New D3D12 gate `scripts/verify_ui.py`** (`--exercise-ui`, passed: [receipt](../evidence/r6-ui-1648a19d47.json), [log](../evidence/r6-1648a19d47-ui.log), [capture](../evidence/r6-1648a19d47-ui.png)). It creates a menu document from its template in the UI tab, checks the live preview, edits the markup (the preview follows), clicks a button (command logged), checks that Undo reaches the preview, saves, and starts Play (the game HUD evicts the preview). After Stop the preview comes back with the saved text. The first run found that the game HUD kept RmlUi after Stop; Stop now releases it (`PlayController::stop_gameplay`).
- **Full CTest: 117/117 passed** ([log](../evidence/r6-ctest.log)).
- **Editor gates: 54/54 D3D12 gates passed** ([log](../evidence/r6-editor-gates.log)). These include the HUD, inventory, altar and Play gates affected by the shared RmlUi lifetime.
- **M8 slice gates passed**: solo, save/load, party and party travel ([log](../evidence/r6-m8-slice.log)).

## Not done (R6 remainder)

- **U0 remainder**: owned member files, shared styles/templates, packaged fonts and images with virtual URLs, a frozen dependency closure for them, and a shared UiRuntime so previews can run alongside Play.
- **R6.2 / U2**: attaching documents as Player HUD elements and NPC/Player world markers, the screen stack and input routing, the C++ and local Luau facade, and the sessionless menu and screen-puzzle examples with host-validated puzzle results.
- Element outline/selection in the preview, resolution presets beyond aspect, and a consumer list before deletion. Delete is not offered.
