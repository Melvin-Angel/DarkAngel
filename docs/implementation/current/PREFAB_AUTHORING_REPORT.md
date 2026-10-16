# R4 Prefab Authoring and Publishing

16 October 2026. **Implemented for the first complete version (single-object gameplay prefabs) and locally verified**; see *Verification* and *Deferred or not done*. This delivers R4 (C + F) in the [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) against the [prefab architecture](../PREFAB_WORKFLOW_PLAN.md), [placement and swap design](../future/PREFAB_PLACEMENT_DESIGN.md) and [request checklist](../future/PREFABS_AND_COMPONENTS_TODO.md). It was implemented autonomously overnight; the decisions below were taken without the user and are recorded so they can be revisited.

## Delivered behavior

- **Prefab assets** (`.daprefab` under `prefabs/`): an assembly source with `kind: "prefab"`, a display name, one to 64 objects and optional exports. Prefabs hold models and world objects (altar, door or platform, pickup, interactable, trigger). Scene roles (spawn points, encounters, volumes, splines, entries, transitions, audio and collision profiles) stay in scenes. A prefab must resolve on its own, so every component decoder validates it. It cooks to a `prefab.json` product through the same native catalog, preview, coordinated Save and history paths as the other gameplay sources.
- **Prefabs workspace** (new fixed tab after Game): search, *Create prefab*, and a definition form that always says *Editing prefab definition*. The form covers the object name, a Model picker, *Add component* / *Remove component*, field editing for each world-object component, live validation, Save, Undo/Redo, *Place in scene* and an instance count for the open scene. Drafts are the ordinary native authoring drafts.
- **Linked instances in scenes**: a scene mounts a prefab by AssetID and records it in a `links` list in its manifest. The prefab's text is never copied into the scene; opening a scene resolves its links from the project's published sources, and a missing linked prefab refuses to open. Saving a prefab refreshes the open scene at once; running Play keeps its frozen plan, and the next fresh Play uses the new definition.
- **Place**: saved prefabs appear in a *Prefabs* category of the existing Place panel (search, drag and double-click). Placing creates a mount with a fresh identity and a placement (position and yaw); the instance's models are prepared first.
- **Instance editing** in the Scene Inspector: a *Prefab instance* section with *Open Prefab*, *Duplicate instance*, *Delete instance* and placement fields. The gizmo or transform fields of a member move the instance (its mount placement), never writing a transform override. Component fields show inherited values normally; overridden ones are marked with `*`, have a per-field *Revert*, and show the prefab value in a tooltip. Setting a field back to the prefab's value removes the override. Instances can add and remove world-object components. An *Overrides* list supports *Revert selected* and *Apply selected to prefab*.
- **Apply Selected**: writes the selected overrides into the prefab definition, publishes it (validated; a failed publication undoes the draft change), refreshes the scene and then removes exactly those overrides. Between the two steps the instance resolves to the same values, so an interruption cannot change Play.
- **Swap prefab**: a preview lists what is kept (mount, placement, persistent identity), carried and discarded overrides. Only prefabs with the same gameplay contract (the same set of world-object components) are compatible, and multi-object swaps are refused. *Use replacement defaults* is an explicit policy. The member's persistent identity is kept across different member keys through a scene-owned retained-identity entry.
- **Create Prefab from Selection**: turns one of the scene's own objects into a prefab (its pose becomes the instance placement) and replaces it with a linked instance that keeps the object's persistent identity, and therefore its world-ledger state and script-binding identities. The prefab is published before the scene edit, so a failed scene edit leaves the original object untouched. Objects that other scene objects reference, or that reference others, are refused for now.

## Implementation notes

- **Resolver** (`engine/runtime/assembly.cpp`): mounts accept `placement` (`x`, `y`, `z`, `yaw`; scale one) composed through nested mounts, and `retain` (member local key → authored path whose identity the member keeps). Every identity a member derives (object, spawn owner, volume encounter, spline, script binding) uses the retained path. New typed patch operations are `SetComponentField`, `AddComponent`, `RemoveComponent` (for altar, mover, pickup, interactable and trigger only) and `SetName`. A removed component or field is an unresolved target and fails the resolve, so stale overrides are never dropped silently. Mounts without these keys resolve exactly as before; the legacy-fingerprint test checks this.
- **EditorDocument**: linked sources resolve with the scene but are refused by scene source edits, excluded from history and dirty state, and saved only as `links`. `link()` replaces linked definitions and re-resolves; if the scene no longer resolves it throws and nothing changes. `open(path, LinkSource)` takes the resolver. A Placement-scope transform edit of a linked instance member is turned into the mount placement that puts the member at the requested pose.
- **Editor modules**: `apps/editor/prefab_commands.*` holds the scene-side commands, free of ImGui and shared by the UI, the exercise and the tests. `apps/editor/prefab_workspace.cpp` holds the workspace and the Inspector sections. `Controller::prefab_links()` / `refresh_prefab_links()` connect the project sources. Every scene that opens (editor Open, travel listing, Play travel loading) resolves links through the project.
- `engine/assets/prefab_cooker.cpp` validates and cooks prefab sources; it and the resolver are cooker hash inputs.

## Decisions taken while the user was unavailable

1. `.daprefab` under `prefabs/` in the source root; a fixed **Prefabs** tab placed after Game, so the R2 Ctrl+1–0 and Ctrl+G shortcuts are unchanged (Prefabs is reached by clicking or Ctrl+PageUp/PageDown).
2. Prefab definitions are edited through the native authoring drafts (form-based), not through a second scene document with its own viewport. This gives Undo/Redo, validation and coordinated Save for free; a 3D preview of the definition is not part of this version.
3. Instances resolve against **published** prefab sources only. Unsaved prefab edits are visible in the Prefabs workspace and reach scenes on Save.
4. Apply Selected publishes the prefab immediately and is two history steps (the prefab draft in native authoring history, the override removal in the scene history), ordered so that every intermediate state resolves identically.
5. Moving any member moves the whole instance (the mount). Pitch, roll and scale edits of a member remain property overrides.
6. Swap compatibility is "same set of world-object components", and is refused for multi-object prefabs.

## Verification

All on this machine (D3D12, pinned toolchain, `VCPKG_MANIFEST_INSTALL=OFF`, `D3D12_SUPPORTED=TRUE`; no third-party objects rebuilt). Same-machine evidence only.

- **Guarded full build** passed. Two defects were found and fixed during testing: range-for loops iterating `.items()` of temporary JSON values (a dangling reference that broke `M2.document_incremental_edits`), and an unguarded mount scan in the link list that saving a scene used.
- **New `R4.prefabs` test** ([log](../evidence/r4-58dcf219a0-PrefabTests.log)) covers:
  - prefab validation and refusal of scene roles or invalid components;
  - placement composition (position and yaw);
  - two instances as two persistent objects, and Undo/Redo of placement;
  - instance override, and removal of a redundant override;
  - template change with the local override kept, and add/remove component with Revert;
  - Apply Selected, and member moves becoming placement changes with no transform patch;
  - Swap across different member keys keeping identity, refusal of an incompatible swap, and swap Undo;
  - conversion keeping identity and pose, and a duplicate never sharing it;
  - delete instance, and links Save/reopen (including refusal of a missing link and of scene edits to linked sources);
  - refusal of a definition that breaks an override, with the scene unchanged;
  - legacy scene fingerprint unchanged, and prefab cook and refusal through AssetService.
- **New D3D12 gate `scripts/verify_prefabs.py`** (`--exercise-prefabs`, passed: [receipt](../evidence/r4-prefabs-58dcf219a0.json), [log](../evidence/r4-58dcf219a0-prefabs.log), [capture](../evidence/r4-58dcf219a0-prefabs.png)). Through the editor's own commands it creates a modelled altar prefab, places two instances from the Place panel's Prefabs category, overrides one, swaps the other to a second prefab, converts a loose altar, applies the override to its prefab, shows the Prefabs workspace, saves and reopens the scene with an identical plan fingerprint, and starts a fresh Play. Protected Royal content was unchanged.
- **Full CTest: 115/115 passed** ([log](../evidence/r4-ctest.log)).
- **Editor gates: 54/54 D3D12 gates passed** (composite authoring gate script, editor-only) ([log](../evidence/r4-editor-gates.log)).
- **M8 slice gates passed**: solo, save/load, party and party travel ([log](../evidence/r4-m8-slice.log)). These exercise the changed resolver and document paths through Play, saves and travel with existing content.

## Deferred or not done

- **Outliner instance rows**: instance members are listed as ordinary objects (named after the prefab member); there is no expandable instance row or prefab badge yet. Selecting a member shows the *Prefab instance* section.
- No 3D preview inside the Prefabs workspace (the definition is previewed by placing it).
- Multi-object capture, nested prefab authoring, variants, in-context editing and identity-preserving Unpack remain the later increment, as planned.
- Create Prefab from Selection refuses referenced or referencing objects (it would need exports and a reference rewrite).
- Renaming or deleting a referenced prefab is not offered; consumers are not listed.
- AgentHost and the cooker's scene-collision bake open scenes without a link resolver, so they refuse scenes that link prefabs.
- Two-scene propagation and distinct world-ledger state per copy are covered by identity tests (distinct persistent IDs, duplicates never sharing retained identity) and the single-scene D3D12 gate, not by a two-scene Play/save run.
