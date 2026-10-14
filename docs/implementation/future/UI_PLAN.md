# Runtime UI as authored content: plan

**Deferred by the user on 12 October 2026 ("forget the UI stuff for now"). Do not start any stage of this plan, and do not ask its questions again, until the user brings UI back.** The text below is kept as the starting point for that day.

Written 12 October 2026 from a user request. Nothing in this file is built. It is a plan to be turned into a design per stage before code, like the other reports.

## The request

- Support world-space UI.
- A **UI tab** in the editor where UI documents are edited.
- Attach UI documents to NPCs for their world-space UI.
- Attach UI documents to the Player for the screen-space HUD, and later for world-space player UI.
- Think about how main menus work together with this.

## What exists today (HUD_REPORT.md)

- `darkangel::Hud` owns one RmlUi context, one document and one data model bound to `HudViewModel`. RmlUi keeps process-wide state, so a second simultaneous HUD is rejected.
- One document, `games/AshenRoots/ui/hud.rml`, whose path is compiled into the editor. It is not an AssetService asset, is not cooked and does not reload.
- The game owns the mapping from confirmed state to the view model (`ashen_roots::royal_hud`).
- The only host is the editor's Game viewport, drawn through the ImGui draw list. There is no Diligent render backend for RmlUi, no input path, no shipped font, no image loading.
- The target's Health bar is part of the player's HUD document, not something attached to the target.

Each of those is a limit this plan removes.

## Rules kept

- DAE-010: RmlUi for runtime UI, native view models and approved commands; ImGui is developer tooling only. Source editing and preview first; a drag-and-drop visual designer stays an optional later extension.
- UI is presentation. It reads confirmed or checked public state through view models and never decides gameplay. A document cannot reach the session; what it may do is emit a named command that native code has approved and handles.
- The server and headless processes construct no UI.
- Which attribute is Health, which tag means staggered and so on remain game rules: the engine offers binding sets, the game fills them.

## Design in outline

### 1. UI documents become assets

A UI document is a source under `content/ui/`: `.rml`, its `.rcss` style sheets, images and fonts, imported and cooked like any other source, with dependencies in the scene and package closure. Next to each document sits a small native descriptor (`.daui`, JSON) that says what the document is for:

- **kind**: `hud` (screen space, bound to the owning player), `world` (attached to an actor or a scene object), `screen` (a menu or other full-screen document that takes input);
- **binding set**: which typed view model it expects (below), so a picker only offers compatible documents and validation can name a variable the model does not have;
- **commands** it may emit (screens only);
- for `world`: default size in metres or pixels, whether it faces the camera, maximum visible distance, whether scene geometry hides it.

`games/AshenRoots/ui/hud.rml` moves under `content/ui/` and becomes the first asset. A font is shipped as an asset, which removes the dependency on the Windows fonts folder.

### 2. One UI service instead of one HUD

`Hud` becomes one consumer of a `UiService` that initialises RmlUi once and owns several layers:

- **screen layer**: a stack of `screen` documents (menus), topmost takes input;
- **HUD layer**: the `hud` documents of the local player, in order;
- **world layer**: instances of `world` documents, one per attachment that is currently relevant.

Each instance has its own data model. Instances of the world layer are bounded (a cap, nearest first, with a distance cutoff), because a scene holds up to thirty-two actors.

### 3. Binding sets (typed view models)

A small closed set to begin with, each a native struct filled from confirmed state:

- **owner**: what `HudViewModel` has today (Health, Stamina, staggered, active mask), growing with cooldowns, status icons and the interaction prompt;
- **actor public**: display name, faction, public Health, public status tags, and for an NPC its awareness toward the local player (the meter that exists since 11 October). This is what a nameplate or a health bar over an NPC's head binds to;
- **target**: the actor public set of the current HUD target, so the target bar can stay in the HUD document or move to the target's own world document;
- **menu**: whatever a screen needs (settings values, session list, save slots), added per screen with its commands.

The game supplies the mapping functions, as `royal_hud` does now.

### 4. World-space UI

Two ways to draw a document at a place in the world, built in this order:

- **Projected**: the attachment point (actor root, a named joint or an offset above the head) is projected to the screen each frame and the document is drawn there in screen space, scaled by distance, hidden beyond its range and when the point is behind the camera or (optionally) behind scene geometry by a depth test at the point. This is right for nameplates, health bars, awareness markers and interaction prompts, stays sharp at any distance and works through today's host adapter.
- **In the world**: the document is rendered to a texture and drawn as a quad in the scene with the scene's depth, so it is occluded and lit like an object. This is right for a sign, a terminal or a diegetic panel, costs a render target per instance and needs the Diligent RmlUi backend of stage U2.

Positions come from the same observed pose and motor state that visual cues attach to in M7, so the attachment code (actor root, joint, offset, follow) should be shared with the cue player's, not written twice.

### 5. Attaching documents

- **NPC** (`.danpc`): a `ui` list; each entry is a `world` document, an attachment (joint or offset) and optional overrides of range and scale. Edited in a UI section of the NPC tab. Every actor spawned from that NPC gets an instance while it is relevant.
- **Player** (`.daplayer`): a `hud` list (screen-space documents shown to the owner) and a `world` list (shown to other players over this player, for example a name; unused until there is a second client). Edited in a UI section of the Player tab. The compiled-in HUD path goes away: Play shows the HUD the Player asset names.
- **Scene objects**: an interactable or a plain object may carry a `world` document later (a prompt, a sign). Not in the first stages.

Open detail for the design stage: whether the local player sees their own `world` documents (normally no).

### 6. The UI tab

A top-level **UI** workspace (`Workspace::Ui`), item 13 of the vision ("UI / HUD Designer"):

- catalog of UI documents with New (from a template per kind), Duplicate, Delete, as ordinary drafts with Undo/Redo, validation and coordinated Save;
- a source editor for the RML and RCSS text, and a form for the descriptor;
- a live preview next to it that reloads on edit, with a resolution and aspect picker, and for `world` documents a view of the document over the preview character at a chosen distance;
- a panel of **sample values** for the document's binding set (set Health to 25 of 100, toggle staggered, type a name), so a document is checked without starting Play;
- RmlUi's warnings and errors shown against the source, and a failed parse keeping the last good preview;
- for screens: click through the preview and see which commands were emitted, without executing them.

Not in it: drag-and-drop layout.

### 7. Main menus and the rest of the front end

A main menu is a `screen` document in the same service, with three things the HUD never needed:

- **Input.** Mouse, keyboard and gamepad events routed into RmlUi, with focus and navigation, and an input mode so gameplay input is not read while a screen has focus (the pause menu over a running game is the mixed case).
- **Commands.** A screen emits named commands from a list approved in native code: `start`, `host`, `join`, `continue`, `open_settings`, `apply_settings`, `resume`, `quit_to_menu`, `quit`. The game handles them. A document cannot do anything that is not on the list.
- **A front-end flow that exists without a session.** Today everything starts inside Play with a session already running. Menus need an application-level state above the session: boot, main menu, loading (session being prepared or joined), in game (HUD layer, pause screen on top), back to the menu. The game declares this flow as data (which screen is the main menu, which is the pause screen, which is loading) and the engine's client runs it. The session is created and destroyed by that flow rather than by the editor's Play button alone.

What it depends on that is not built: a game executable outside the editor (the flow is hard to judge only inside the Game tab), the Diligent RmlUi backend, the host and join flow and the bootstrapper (recorded on 11 October as later-milestone items), settings storage, and saves for "continue" (M8). So the menu system is designed with the rest of UI, and built in two parts: the screen layer, input and commands early (a pause menu and a settings screen are useful in the editor's Play at once), the full front-end flow when the standalone client exists.

In the editor: the Game tab's Play keeps starting straight in the scene by default, with an option to start from the game's front end once that exists.

## Stages

Each is one chunk: design section first, then build, then one verification pass.

- **U1. Documents as assets and the UI service.** `content/ui/`, the `.daui` descriptor and cooker, a shipped font, `UiService` with a HUD layer; the existing HUD document moved and loaded through AssetService; the Player asset names its HUD. Existing HUD gates must pass unchanged.
- **U2. Diligent render backend for RmlUi.** Needed for a packaged game, for in-world documents and for images. The editor's Game viewport may keep the draw-list adapter or switch; decide in the design.
- **U3. World layer, projected.** The actor public binding set, attachment, bounds; NPC `ui` list and the NPC tab section; a first owned nameplate with a Health bar and awareness marker on the Royal Guard and Archer; a gate with a capture.
- **U4. The UI tab.** Catalog, source editor, preview, sample values, diagnostics.
- **U5. Screen layer, input and commands.** A pause menu and a settings screen in Editor Play; the command list; input mode switching.
- **U6. In-world documents** (render to texture) and scene-object attachment.
- **Later, with the standalone client:** the front-end flow, main menu, loading, host and join screens; "continue" with M8 saves.

U4 could come before U3 if editing documents by hand in a text editor is in the way sooner; U3 is first here because it delivers the thing asked for (UI over NPCs) with the least new surface.

## Where this sits in the roadmap

The handoff places wider RmlUi authoring in M8's extension row ("extends M5 UI; source/preview first"). This plan starts it earlier, after M7's steps 1 to 4, at the user's request; it does not change M7's or M8's gates. If it should come before the rest of M7, say so and the order in CONTINUE.md changes.

## Questions for the user (none blocking)

1. Order: UI stages after M7 step 4 (the VFX tab), as written, or earlier?
2. First world-space document on NPCs: a name, a Health bar and the awareness marker, shown only within a range and only once the NPC has noticed the player or been hit? Or always shown?
3. Should the target's Health bar leave the HUD and become the target's own world document, or stay at the top of the screen as well?
