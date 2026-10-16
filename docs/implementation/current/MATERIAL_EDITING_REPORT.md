# R5 Asset and Material Editing

## A2: prefab/instance material assignments, Swap mapping and reimport repair (16 October 2026)

**Implemented and committed with focused verification. The full suite (complete build, full CTest, editor gates, M8 slice and the prefab/material/UI gates) is deferred until R5 is complete, at the user's request (16 October 2026); A2 has not had a full-suite pass yet.** Implemented by Codex; documented and committed by Claude Code.

### Delivered behavior

- **Model-component material assignments.** A placed model's entity can carry a `materials` record: stable imported material slot -> native `.damaterial` instance. Prefab members hold defaults; scene instances override them through a typed `SetMaterial` patch, and the scene's own objects can carry assignments too. An explicit empty value selects the model's own default. The resolver publishes them as `SpawnPlan::materials` (object -> slot material subasset ID -> instance ID). Plans without assignments keep their old serialization and fingerprint.
- **Commands and UI.** `set_instance_material` / `set_object_material`, override inspection with markers, Revert, Apply Selected to the prefab, Undo/Redo, and preservation through capture and Unpack. The Scene Inspector (instance members and own objects) and the Prefabs workspace (definitions and variants; variant edits write typed patches) edit the same references.
- **Swap mapping.** The Swap preview and commit take an explicit stable-slot map when the replacement has a different model. Destination slots are validated before any mutation; stale or colliding maps are refused; *Use replacement defaults* discards the material patches explicitly. Nothing is mapped by index or name.
- **Guided reimport repair.** `model_reimport_changes` reads the source's actual material keys without applying stale assignments. Assets offers *Review Reimport Changes*, an explicit map-or-retire choice for each removed key, and *Apply Explicit Reimport Repair*. Matching keys keep their IDs, remapped keys keep the chosen old ID and assignment, newly adopted keys get fresh IDs. Retiring a slot that a prefab or scene still references is refused until those consumers are repaired; a scene slot reference that no longer exists stays authored and has an explicit Clear action.
- **One transaction for material + metadata.** `AssetService::model_material_edit` composes a model-metadata change with pending `.damaterial` creations/edits in `prepare_native` / `commit_native`; Create Editable / Duplicate and Assign publish both heads together, and a failure keeps drafts and previous heads. Coordinated Save follows model, prefab and material consumers so affected heads publish together; reimport repair can be composed with pending prefab edits.
- **Cooking and frozen closures.** A standalone `.damaterial` cooks with its texture product as a dependency; `load_cooked_material_instance` loads it from registry/CAS only. Prefab cooking validates material slots against the model's real source keys and freezes material/texture dependencies, including nested and variant prefabs. Fresh Play and travel include the material roots; `validate_cooked_material_bindings` checks slots and the full material/texture closure before GPU resources are replaced.
- **Rendering.** `UploadedMaterial` holds a native instance's textures and static/skin shader bindings without copying model geometry. SceneResources keeps a bounded material cache with generation checks (including texture content), retires replaced bindings at GPU idle and rebinds on shader/model reload. Draws use the displayed plan's per-object assignments, including the Prefabs preview and in-context view.
- **Viewport refresh defect fixed.** `prepare_model` returned early for already uploaded models, so a saved material never reached the viewport (an A1 gap). Material save, assignment and repair now use an explicit `refresh_model` callback that prepares replacement resources and swaps at GPU idle.

### Decisions taken

1. Overrides are keyed by the imported material subasset identity (the stable slot), never by index or display name.
2. Different-model Swap carries a material override only through an explicit map; otherwise the author must choose replacement defaults.
3. Unsaved material drafts are shown in the Assets/Prefabs flows without being published; the scene viewport shows saved materials (refreshed at once on Save).
4. Retiring a referenced slot is refused rather than silently dropping its consumers' assignments.

### Focused verification (same machine, synthetic input)

- Native `MaterialTests`, `PrefabTests` and `DocumentIncrementalTests` pass 3/3 ([receipt](../evidence/r5a2-bindings-native.json)). They cover private preparation, stale-writer refusal, rollback, combined creation/assignment, source reorder with stable IDs, explicit adoption/retirement/remapping, referenced-slot retirement refusal, coordinated model/prefab consumer publication, immutable earlier prefab packages, missing dependencies, frozen binding preflight, mapped Swap with Undo and default policies, prefab defaults with local overrides, Revert/Apply, history, Unpack, and incremental/full resolve equivalence.
- `scripts/verify_materials.py` passes on D3D12 ([receipt](../evidence/r5-materials-98caa8cab4.json), [capture](../evidence/r5-98caa8cab4-materials.png)) and on Vulkan ([receipt](../evidence/r5-materials-ec0252cd0a.json), [capture](../evidence/r5-ec0252cd0a-materials.png)), editor SHA256 `6cc78257…a870a8`, protected content unchanged. The exercise covers local isolation of a prefab copy's material, Revert/Undo, Save/reopen fingerprint, frozen fresh-Play bindings, Assets -> Edit Prefab and back without losing the unsaved shared draft, shader and model reload, mapped cross-model Swap, and a shared colour edit that keeps the local variation. Captures were inspected (local copy green, shared/default copies blue). The Khronos validation layer is unavailable, so no validation-clean Vulkan result is claimed.
- Earlier focused failures are kept as evidence: `r5-9d8a834f47-MaterialTests.log` (`.daimport` rejected by the source validator; fixed) and `r5a2-closure-fixture-failure.log` (a test fixture missing a World type; fixed).
- **Pending in the R5 full suite:** full CTest, editor gates, the M8 slice with its new destination-only material override checked on host and guest after travel/rest/respawn, and the prefab/UI gates. An interrupted full run (stopped during the build on 16 October; its link failures are aborted processes, not compile errors) is not counted.

### Not done in A2

- A live viewport preview of *unsaved* material values in the scene viewport (the Assets and Prefabs flows keep the draft unpublished; the viewport updates on Save).
- Dedicated shader-reload/resize qualification beyond the material gate's reload steps.

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

- **A2**: delivered later the same day; see the A2 section at the top of this report.
- **A3**: normal, packed metallic/roughness, occlusion and emissive textures; stylized families; skinned-model material parity; a Vulkan run.
- Viewport preview of an *unsaved* material draft: the viewport updates on Save.
- A slot whose instance has a texture carries a second copy of that texture product in the model closure; identical bytes, no extra GPU work beyond the existing per-model texture upload.
