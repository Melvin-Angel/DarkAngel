# R5 Asset and Material Editing

16 October 2026. **A0 and A1 implemented and locally verified; A2 (prefab/instance material assignments, reimport repair UI) and A3 are not done.** This covers the first half of R5 (G) in the [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) against the [Assets/material plan](../future/ASSETS_MATERIAL_EDITING_PLAN.md). It was implemented autonomously overnight; the decisions below were taken without the user.

## Delivered behavior

- **Material instances** (`.damaterial` under `materials/`): engine-owned values for the registered `lit-color-v1` family, which is what the static-model renderer draws. Fields are base colour (0–1), roughness, metallic, double-sided and an optional base-colour texture. The texture must be an imported colour texture of the project (`texture-color-v1`), checked against its own import identity. Unknown families and out-of-range values are refused. Instances cook as `material-instance.json` products through the ordinary native authoring paths (drafts, Undo/Redo, coordinated Save).
- **Slot assignments in the model's import metadata**: a model's `.daimport` gains a `materials` table from stable material slot key (for example `material/trim`) to an instance and its source. The glTF cooker applies the instance's values to that slot's material product; the slot keeps its own identity, so meshes, collision and gameplay identities are untouched. An instance texture is carried into the model's closure. A slot key the model no longer has is refused with a repair message instead of being remapped by order or name.
- **AssetService**: `assign_model_material(model, slot, material, source)` writes the table and must cook the model with it, or the previous metadata is restored. `material_values(model, slot)` returns a slot's current cooked values.
- **Assets workspace**: selecting an imported static model shows a *Materials* section. For each slot it offers the current assignment, a picker for any saved material, *Imported default* (reset), *Create editable material* (a new instance from the slot's cooked values, published and assigned), *Duplicate and assign* for an assigned slot, and an inline editor for the assigned material. Selecting a `.damaterial` shows the material form: colour, roughness, metallic, double-sided, texture picker, validation, Save, Undo/Redo, and the models that use it with a note that the material is shared. Saving refreshes the viewport models that use it. New *Prefabs* and *Materials* filters were added; *Open in its editor* on a prefab opens the Prefabs workspace (R4).

## Decisions taken while the user was unavailable

1. A new material starts untextured, even when the imported slot had an embedded texture: an embedded glTF image is not a project asset an instance can reference. The author picks a project texture, or keeps the imported default by not assigning a material.
2. Assignment is a separate validated metadata write after the material is published, not one combined transaction. A failure leaves an unassigned material, which changes nothing, and the previous metadata stays.
3. Only the `lit-color-v1` family exists; the form shows only the values the renderer uses.

## Verification

All on this machine (D3D12, pinned toolchain, `VCPKG_MANIFEST_INSTALL=OFF`; no third-party objects rebuilt). Same-machine evidence only.

- **New `R5.materials` test** (passed) covers:
  - imported slot values are readable, and an unassigned slot cooks them unchanged;
  - one material shared by two models' slots reaches both, and an edit of it reaches both on their next cook;
  - a project texture on the material reaches the cooked model and its texture closure;
  - an unknown slot, an out-of-range value and an unknown family are refused without changing any file;
  - a stale slot key in the metadata makes the model refuse to cook until repaired;
  - reset to imported removes the assignment and restores the imported values exactly.
- **New D3D12 gate `scripts/verify_materials.py`** (`--exercise-materials`, passed: [receipt](../evidence/r5-materials-19af3a62d9.json), [log](../evidence/r5-19af3a62d9-materials.log), [capture](../evidence/r5-19af3a62d9-materials.png)). It finds an imported static model, creates an editable material from a slot, assigns it, edits and saves it through the native drafts, checks that the cooked model carries the new colour and roughness, resets to imported (exact imported colour) and reassigns. The capture shows the Assets workspace *Materials* section.
- **Full CTest: 116/116 passed** ([log](../evidence/r5-ctest.log)).
- **Editor gates: 54/54 D3D12 gates passed** ([log](../evidence/r5-editor-gates.log)).
- **M8 slice gates passed** on the second run ([log](../evidence/r5-m8-slice.log)). The first run passed solo, load and party, but the party-travel host hit the script's 900 s timeout ([log](../evidence/r5-m8-slice-timeout.log)). This was the first slice run after the cooker hash changed (every model recook). The immediate rerun with no code change passed every gate. The timeout is recorded rather than explained away; if it recurs, investigate travel preparation time after a cooker change.

## Deferred or not done

- **A2**: material assignments on prefab members and scene instances (model-component slot assignments with override and Revert), the Swap preview for slot assignments, and an explicit reimport repair UI. Stale slot keys are refused at cook with a message, but there is no guided repair.
- **A3**: normal, packed metallic/roughness, occlusion and emissive textures; stylized families; skinned-model material parity; a Vulkan run.
- Viewport preview of an *unsaved* material draft: the viewport updates on Save.
- A slot whose instance has a texture carries a second copy of that texture product in the model closure; identical bytes, no extra GPU work beyond the existing per-model texture upload.
