# Native editor import and workspace increment

Implemented 8 October 2026 after planning checkpoint 9c8fd2b. This is the first shared-editor increment in M4/M5. It does not complete either milestone or the broader [editor workflow plan](EDITOR_WORKFLOW_PLAN.md).

## Available now

Launch `powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1`. The launcher mounts `content` for asset authoring and uses `.cache/editor-assets` for its rebuildable catalog/CAS, alongside the existing frozen Royal scene package.

The current full editor binary is `build/m5-editor-relwithdebinfo/DarkAngelEditor.exe`. Its milestone directory name is temporary iteration naming. Ashen Roots has one editor product containing all implemented features; legacy milestone directories and no-animation shared-library tests are not additional editor editions.

Use the Scene / Level Design and Assets navigation tabs to change the main layout. Scene retains the native viewport, hierarchy, inspector, Play/Stop/Pause/Step and existing source transactions. Assets uses an embedded full-workspace inventory with search, category-folder filters, selection, project paths and cook generations. The scene Content Browser also exposes the shared import/browser controls. Switching to Assets releases viewport controls; transform gestures must finish before switching layouts.

Choose File > Import asset, or Import in either browser. Select a file in the Windows file chooser, then explicitly choose an item type in the native import modal. Validate import previews the exact destination and copied source/dependency/metadata files. Import into project publishes the validated asset and updates the inventory. Imports are disabled during Play. Closing/cancelling an uncommitted import does not publish source files or a catalog entry.

| Supported type | Source profile | Automatic project destination |
|---|---|---|
| 3D model | Static glTF / GLB within the existing color-material profile; local buffer/image dependencies | `content/models/<name>-<id-prefix>/` |
| Skinned mesh | Renderable canonical human GLB with the existing embedded-atlas profile; explicit project `.daskeleton` | `content/characters/<name>-<id-prefix>/` |
| Animation | Already normalized canonical GLB; explicit project `.daskeleton` and loop setting | `content/animation/clips/<name>-<id-prefix>/` |
| Color texture | PNG / JPEG, sRGB, native DDS mip chain | `content/textures/<name>-<id-prefix>/` |

VFX, Audio, Material and UI appear as type choices with an explanation of their pending integration. They cannot be imported by pretending another category is implemented. The current full editor includes the canonical animation profiles. No-animation checks exercise isolated shared-library compatibility. FBX conversion, arbitrary rigs, data/normal textures and standalone material authoring are not delivered by this increment.

## Source and publication behavior

- External originals and their sidecars remain untouched. The project owns a copied snapshot and a fresh root/subasset UUID mapping; this is a new import, not an external project's identity-preserving migration.
- Dependency paths stay inside the selected source directory. Remote/data/encoded unsafe URIs and escaping paths reject. Dependencies retain their relative layout in the imported asset directory.
- Existing explicit glTF subasset keys survive; missing mesh/material keys are assigned to the owned copy before adoption. The GLB binary chunk is preserved. This permits supported ordinary static glTF sources without editing their originals.
- Same-name imports receive separate directories and AssetIDs. Existing project assets must use their current identity; the external importer rejects selecting them as new files. Existing content paths are not reorganized.
- Validation snapshots and hashes the bounded input closure, performs the real native conversion in an isolated cache scratch directory, and prepares the standard `.daimport` metadata. No source/catalog publication occurs at this stage.
- Commit checks the owning coordinator identity, source/canonical hashes and unused destination again. It stages the owned files, renames the complete directory, then uses the existing scan/cook/CAS transaction. Failure restores the source inventory; immutable orphan CAS data is allowed, a partial generation head is not. Prepared imports cannot be reused or transferred to a new coordinator lifetime.
- The editor's scene document is not mutated by importing. These asset-library operations use AssetService prepare/commit; scene/component edits continue through EditorDocument. Import deletion/undo and GUI reimport are not yet implemented.

Limits: one selected file plus at most 256 snapshotted inputs, 128 MiB input closure, existing profile/topology limits and at most 64 MiB per cooked product. Validation/cooking is synchronous on the owning editor thread in this initial slice. Batch jobs, progress/cancellation during conversion, and measured interactive latency remain planned; Cancel currently discards a prepared import.

## Verification

Reproduce with `python scripts/verify_external_import.py --build`. Omit `--build` when the affected binaries already match the source. [Receipt](evidence/external-import.json) records binary/source hashes and the scoped checks.

- Native external-import tests pass for model/dependency routing, absent stable-key normalization, same names, cancellation, stale inputs/canonical data, unsafe paths, destination collisions, failed-commit rollback, coordinator lifetime, color textures, complete canonical human/clip load and cold catalog rebuild.
- Five affected optimized native tests pass: clips, CharacterSceneSession, renderable human, asset pipeline and EditorDocument transactions.
- Three no-animation checks pass: external import's supported/unsupported profiles, asset pipeline and editor transactions.
- Native modal validation/commit callbacks and inventory views render on D3D12 and Vulkan. The callbacks publish exactly one imported model without changing scene authoring data. The default modal has no selected type. Screenshots were visually inspected for readable, unclipped navigation, controls and inventory.
- Existing Royal editor checks still pass for 120 fixed movement ticks on each backend and Play/Step/undo/redo/save/open/resource-reload regression. Additional source-mounted runs verify the new launcher profile while playing on both backends.

These are focused receipts, not a refreshed full milestone suite. The prior integrated baseline was 46/46. The scripted GUI checks use the same native callbacks as the buttons; OS file-chooser clicks and manual keyboard/mouse import interaction were not exercised. Vulkan's Khronos validation layer remains unavailable. No new independent GNS or live EOS acceptance is claimed.

Screenshots: [type selection](evidence/external-import-editor-choice.png), [imported result](evidence/external-import-editor-d3d12.png), [Assets workspace](evidence/external-import-editor-vulkan-browse.png), [source-mounted Royal Play](evidence/external-import-royal-mounted-d3d12.png).

## Remaining M4/M5 work

The asset inventory is a table, not the planned visual reference picker. Thumbnail grids, rotating model/animation previews, persistent searchable tags, compatible field selection and list multi-select remain next shared-editor tasks. Full subsystem tabs and saved layouts still require a designed blueprint. Imported assets are not automatically assigned to scene objects or added to the currently running Royal package.

Continue the real M5 action/ability authority path and Character/Ability/Composer/Projectile-AOE authoring through existing native definitions. Attack remains a pose diagnostic; costs/cooldowns/effects/tags/damage/prediction/late join and runtime health UI are open. M4 clock/input/fault/streaming gates and precise Royal mesh collision remain open. M3 live EOS is blocked; M6 remains gated by required M4/M5 completion.
