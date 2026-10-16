# R5 Asset and Material Editing

**16 October 2026: R5 complete (A0–A3) and verified with the full suite once at the end, as the user asked.**

## R5 full-suite verification (16 October 2026)

All on this machine (D3D12 unless stated, pinned toolchain, `VCPKG_MANIFEST_INSTALL=OFF`; no third-party objects rebuilt). Same-machine evidence only.

- **Guarded complete build** passed.
- **Full CTest: 116/117 in the suite run** ([log](../evidence/r5-full-ctest.log)). The one failure, `M5.external_import`, was "access denied" renaming a freshly written import folder, the machine's known transient file lock; the test passed 3/3 when rerun alone with the same binaries. No code change was made for it.
- **Editor gates: 54/54 passed** ([log](../evidence/r5-full-editor-gates.log)), including the legacy no-argument NativeAuthoringTests composite (fresh fixture).
- **M8 slice: all gates passed** ([log](../evidence/r5-full-m8-slice.log)), including A2's new destination-only material override checked on host and guest after travel, rest and respawn.
- **Area gates passed:** prefabs ([receipt](../evidence/r4-prefabs-dba58cb619.json)), materials ([receipt](../evidence/r5-materials-ad88495bb5.json)) and UI ([receipt](../evidence/r6-ui-c09559c467.json)).
- **One defect found and fixed during the run.** The prefab gate failed at its variant step: since A2, coordinated Save also recooks a base prefab's variants, so saving a base edit is refused until its variants are rebased in the same Save (stricter than R4's "base saves, scene refuses" and kept). The variant form now rebases against the base's unsaved draft, and the gate checks the refusal and the joint Save (commit `d4eabb8`). The three area gates were rerun on the fixed editor and passed; the editor gates and M8 slice ran on the binary before this exercise/form change, which touches neither.

## A3: texture semantics, material definitions, render states and skinned parity (16 October 2026)

**Implemented and verified (focused D3D12/Vulkan checks, then the R5 full suite above).** Scope follows the user's rendering decisions of 16 October 2026: R5 builds the material infrastructure; the Character/Entity/Terrain toon families stay in the deferred SR milestones.

### Delivered behavior

- **Texture semantics.** An imported texture records what its texels mean in its importer: `texture-color-v1` (sRGB), `texture-data-v1` (linear; packed occlusion R, roughness G, metallic B as in glTF) or `texture-normal-v1` (linear tangent-space normal map; each mip texel is renormalised). The import dialog asks for the meaning; Assets shows it for a selected texture and can change it with `AssetService::set_texture_semantic`, which is refused while a material slot or an item icon needs the current meaning. Cooked textures report `srgb`, and the GPU upload picks `RGBA8_UNORM_SRGB` or `RGBA8_UNORM` accordingly.
- **Material definitions.** A registry (`material_definitions()`) names each family's parameters, texture slots with their required semantic, and allowed alpha modes. `lit-color-v1` is unchanged. The new `lit-surface-v1` adds a normal map and strength, a packed surface map and occlusion strength, an emissive colour and map, and opaque, cutout or transparent alpha with a cutoff. A field another family owns, a texture of the wrong semantic and an unknown alpha mode are refused. The Assets material form shows a family selector and only that family's controls; texture pickers list only textures of the slot's semantic.
- **glTF static profile.** Imports now accept normal textures, a metallic-roughness texture with occlusion packed in its red channel, emissive colour and texture, and MASK (cutout) or BLEND (transparent) alpha. New texture subassets get stable keys (`texture/normal/…`, `texture/surface/…`, `texture/emissive/…`); existing models keep their keys and cook exactly as before. A separate occlusion image and other extensions stay refused with a message.
- **One shared surface shader.** The built-in prop shader and `games/AshenRoots/shaders/stylized_prop.hlsl` share the new contract: optional normal, surface and emissive maps selected by per-draw feature bits (no permutations; an unused map is never sampled), and the same stylized banded lighting. A material without the new features draws exactly as before. Normal mapping derives its tangent frame from screen derivatives, so the mesh vertex format is unchanged. An older game shader without the new maps keeps working until a material actually needs one, which is then an explicit error.
- **Render states.** Opaque, alpha-cutout (discard below the cutoff) and transparent. Transparent surfaces use blending pipelines with the same resource layout (depth test, no depth write); static ones are drawn after every opaque surface, sorted back to front per object. A skinned transparent surface is drawn in place, because its pose palette belongs to that object.
- **Skinned-model parity.** Each primitive of a renderable skin is a material slot (its stable mesh key). Assets lists them; assigning a material instance (any family) writes the same `.daimport` table as static models, and the skin cook applies its values and textures to that primitive only; the other primitives keep the shared atlas and imported colour. Reset, stale-key refusal and *Create editable material* from a primitive's values work as for static models.

### Decisions taken

1. Semantic is part of a texture's import identity (its importer), not a per-material flag, so one image is never read with two transfer functions; using one image both ways needs two imports.
2. The stylized shading stays one shader with uniform feature bits rather than compiled variants: the optional maps are cheap branches and the permutation budget stays at zero. Variants remain the tool for genuinely expensive features later (DAE-005 note of 16 October).
3. Occlusion must be packed (glTF ORM); a separate occlusion image is refused rather than silently ignored.
4. A new editable material made from an imported slot still starts as `lit-color-v1`: embedded glTF images are not project assets an instance can reference (A1 decision 1 unchanged).
5. Per-placement material overrides for skinned *actors* are not added: characters are spawned from NPC/Player definitions, not placed models. Their materials are set per primitive on the skin asset.

### Focused verification (same machine)

- `R5.materials` passes ([log](../evidence/r5a3-MaterialTests.log)): colour/data/normal transfer functions, renormalised normal mips, the lit-surface family and its refusals (wrong semantic in a slot, a field of another family, unknown alpha mode), cooked slot values and texture closure, the standalone material closure, refusal to change a used texture's meaning, a generated glTF with normal/ORM/emissive/MASK, and refusal of a separate occlusion image. An earlier run failed because the cooker iterated `.items()` of a temporary JSON (the known dangling-temporary pitfall) and because a texture shared by two slots skipped the second slot's semantic check; both fixed.
- `M5.renderable_human` passes ([log](../evidence/r5a3-RenderableHumanTests.log)): one assigned primitive changes, its texture joins the skin closure, others keep the atlas, an unknown slot is refused, reset restores the atlas.
- `R4.prefabs` and `M2.document_incremental_edits` still pass.
- `scripts/verify_materials.py` passes on D3D12 ([receipt](../evidence/r5-materials-9f9b48d1db.json), [capture](../evidence/r5-9f9b48d1db-materials.png)) and Vulkan ([receipt](../evidence/r5-materials-0d473cf85b.json), [capture](../evidence/r5-0d473cf85b-materials.png)). The new steps import a normal and a surface map, turn the local material into `lit-surface-v1` with normal, surface, emissive and cutout, check its cooked closure and transfer functions, and assign a transparent glass material to the second copy. Both captures were inspected and match: the local copy draws green with surface relief, the glass copy blends over the scene, the shared/default copies are unchanged. The Khronos validation layer is unavailable, so no validation-clean Vulkan result is claimed.

### Not done in A3

- Toon/cel material families (Character, Entity, Terrain), rim light, outlines, dissolve and hit flashes: deferred SR milestones by the user's decision.
- Scene-viewport preview of unsaved material values (unchanged from A2).
- Mirror-ball/HDR textures and texture transforms (`KHR_texture_transform`); UV sets other than UV0.

## A2: prefab/instance material assignments, Swap mapping and reimport repair (16 October 2026)

**Implemented and verified: focused checks during implementation, then the R5 full suite above.** Implemented by Codex; documented and committed by Claude Code.

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
- The R5 full suite above covers the rest (CTest, editor gates, the M8 slice with its material override after travel/rest/respawn, and the area gates). An interrupted earlier full run (stopped during its build; the link failures were aborted processes) is not counted.

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
- **A3**: delivered; see the A3 section at the top of this report.
- Viewport preview of an *unsaved* material draft: the viewport updates on Save.
- A slot whose instance has a texture carries a second copy of that texture product in the model closure; identical bytes, no extra GPU work beyond the existing per-model texture upload.
