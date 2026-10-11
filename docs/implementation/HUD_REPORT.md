# Runtime Health HUD through RmlUi - 11 October 2026

Third item of the autonomous M5 list and the "runtime Health UI" part of the original M5 gate: Play now shows a HUD built with RmlUi from a native view model of confirmed state. M4/M5 remain In progress.

## Dependency step

RmlUi 6.2 with FreeType was pinned and downloaded but not installed in the editor build tree. Following the standing rule, a dry run was done first with the same triplet, overlay ports, install root and buildtrees root that the manifest step uses:

- With the existing features (`m1;m2;m3;m4`) every one of the 19 installed packages reported "already installed", so the dry run saw the same ABI as the real tree.
- With RmlUi added the plan listed six packages to build and install (brotli, bzip2, freetype, itlib, rmlui, robin-hood-hashing) and nothing to remove or rebuild.

Only then was it installed (3 minutes; the log shows exactly those six builds). `vcpkg.json` gained a small `m5-ui` feature (rmlui with freetype); versions come from the existing overrides. `VCPKG_MANIFEST_INSTALL=OFF` is still set. So that the records agree with the tree, the `m5-editor-relwithdebinfo` preset and the build directory's cached feature list now name `m5-ui`. CMake looks for RmlUi quietly and builds the HUD only where it is installed, so other presets are unaffected. Licence notices for all six packages were already under `third_party/notices`.

## What was built

- **`darkangel::Hud`** (`engine/runtime/hud.hpp/.cpp`, library `DarkAngelHud`): owns the RmlUi context, one document and a data model bound to `HudViewModel` (Health, maximum Health, Stamina, maximum Stamina, staggered, optional target Health). `update()` copies the view model, marks the bound variables and runs layout; `render()` draws through a render interface supplied by the host. It reports laid-out element boxes and text for checks. It reads no session, decides nothing and has no input path. RmlUi keeps process-wide state, so a second simultaneous HUD is rejected; it can be destroyed and created again.
- **Game document** (`games/AshenRoots/ui/hud.rml`): player Health and Stamina bars bottom-left with numbers, a "Staggered" caption, the target's Health bar top-centre and a "Defeated" banner. Bars and text are data-bound; the document carries no rules.
- **Game mapping** (`ashen_roots::royal_hud`): which attributes are Health and Stamina, where Stamina's ceiling comes from and which tag means staggered are game rules, so the mapping from the owner's confirmed snapshot and the target's public Health lives with the game, not the engine.
- **Editor host adapter** (`apps/editor/hud_overlay`): an RmlUi render interface that appends HUD geometry to the Game viewport's ImGui draw list, clipped to the viewport, with font atlases created as device textures. The HUD is created when a combat Play session exists and destroyed when it ends.

## Verification

Every target was rebuilt through the pinned build command after the CMake change; regeneration ran no vcpkg step. `python scripts/verify_effect_reaction.py` ran twenty-three gates and all pass ([receipt](evidence/effect-reaction.json)). The whole CTest suite was then run once: 83 of 88 pass, and the five that do not are the known unrelated ones (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`).

- **HudTests** (`M5.hud`, headless, a recording render interface in place of a GPU):
  - `royal_hud` maps a confirmed owner snapshot (Health 75 of 100, Stamina 85 with the authored bound 100, the staggered tag) and public target Health 60 of 100 to the view model, and invents no target when there is none.
  - The real game document loads with no RmlUi warnings; the player panel is anchored bottom-left and the target panel is centred at the top at 1280x720; the Health, Stamina and target fills are 75, 85 and 60 percent of their bars; the numbers read "75 / 100", "85 / 100", "60 / 100"; "Staggered" is shown and "Defeated" is not.
  - Geometry and a generated font texture reach the host; no image file is requested. An unchanged view model redraws.
  - Health 0 without a target: empty Health bar, "0 / 100", "Defeated" shown, "Staggered" and the target panel hidden. A resize to 640x360 keeps the anchoring.
  - A non-finite view model and a second simultaneous HUD are rejected; a missing document fails cleanly; on destruction every host geometry and texture handle is released, and a new HUD can be created afterwards.
- **D3D12 editor Play** with the scripted attacker, each run asserting that the HUD exists, drew geometry this frame and shows the confirmed Health. The Light capture below was inspected as committed; the Heavy and death pictures were inspected from a direct run with the same arguments just before the final verify pass regenerated them:
  - [Light hit, tick 40](evidence/attacked-light-d3d12.png): Health bar at "75 / 100", "Staggered" caption, full Stamina, target bar "100 / 100" at the top.
  - [Heavy hit, tick 40](evidence/attacked-flinch-d3d12.png): "60 / 100".
  - [Death, tick 240](evidence/attacked-death-d3d12.png): "0 / 100", empty Health bar, "Defeated" banner over the fallen player.
- The other editor gates (reaction, death, dodge, masks, cameras) now run with the HUD drawn and still pass.

## Limits

- The only host is the editor's Game viewport, composited through the editor's ImGui draw list. There is no standalone Diligent render backend for RmlUi yet; a packaged game needs one. The `Hud` class and document do not change for that.
- The font is Segoe UI read from the Windows fonts folder (as the editor's own UI does). No font asset is shipped; without that file the editor logs "Runtime HUD unavailable" once and Play continues without a HUD.
- The document path is compiled into the editor from the source tree; the HUD document is not an AssetService asset, is not cooked and does not hot-reload.
- The view model follows confirmed state (the owner correction), not predicted costs, so Stamina drops on confirmation. In loopback that is the same tick.
- No HUD input, menus, damage numbers, status icons, cooldowns, mask indicator or screen-space target markers. No image decorators (the adapter loads no image files) and no RmlUi transforms, clip masks or filters.
- Not verified: Vulkan, high-DPI scaling, a physical resize of the Game panel (resize is covered by the headless test only).
