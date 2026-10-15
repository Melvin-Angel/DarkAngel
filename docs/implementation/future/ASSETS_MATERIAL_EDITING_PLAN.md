# Assets: imported-model materials and prefab editing entry points

**Scheduling: Planned, R5 (G)** in the [authoritative roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026), after the complete R4 prefab workflow. A0–A3 below are local material implementation IDs, not the old A movement feature. Use the [UX standards](EDITOR_UX_OVERHAUL_PLAN.md); R2 specifies existing-surface conventions without implementing these new forms.

15 October 2026. **Research/design for future implementation only.** Linked from the [authoring handoff](AUTHORING_NEXT_STEPS_2026-10-15.md), [prefab workflow](PREFAB_PLACEMENT_DESIGN.md) and [environment/rendering plan](ENVIRONMENT_LIGHTING_POST_PROCESSING_PLAN.md).

## Desired workflow

In **Assets**, select an imported model, preview it, select one of its material slots and edit its material values or swap a texture. Save those changes as engine-owned authored data that survives model reimport. Select a prefab in the same Assets catalogue and press **Edit Prefab** to enter the existing planned Prefabs workspace with that asset selected.

Assets remains the common search/navigation entry point; the specialized workflows own their drafts and inspectors. One AssetID has one authoritative draft even if opened from Assets, Place, Scene Inspector or another workspace. Opening an editor never duplicates a source asset, creates a placement or restarts Play by itself.

## Current source findings

`editor_assets.cpp` already has a searchable inventory, import/cook status and an **Open authoring** button with an extension allowlist. Filters are partly path-based. There is no dedicated Materials/Prefabs catalogue filter or imported-model material form.

`gltf_cooker.cpp` already imports stable-keyed material subassets with color, roughness, metallic, double-sided and a base-color texture. `CookedMaterial` and the GPU uploader consume those values, but native material creation/editing is not exposed. Texture import explicitly supports color/sRGB images; normal/data texture profiles are not already implemented. The skinned preview path has a narrower atlas/material representation, so full per-slot skin material editing requires a separate adapter extension.

`.daimport` stores source identity and stable subasset mappings. `external_import.cpp` injects `darkangel_key` into normalized meshes/materials, with index-derived fallback keys for new imports. That fallback does not prove a later artist file with reordered materials can safely remap all prior edits. Generated material products/CAS bytes are outputs, not editable source files.

## Catalogue and Edit routing

Extend the source inventory with typed kinds and an editor-route descriptor. Use it for Assets filters, **Edit** action labels and navigation rather than adding new parallel extension allowlists in every panel.

| Selected asset | Main action | Destination |
| --- | --- | --- |
| Model | **Edit Model / Materials** | Embedded Assets model preview and slot/material form |
| Native material instance | **Edit Material** | Same material form, showing definition and consumers |
| Prefab | **Edit Prefab** | Fixed Prefabs workspace, correct document/selection restored |
| Sky / lighting / post / bake profile | **Edit Sky / Lighting / Post Processing / Bake Settings** | Appropriate Tools page with that asset selected |
| Existing Player/NPC/Ability/UI/etc. | **Edit [type]** | Its existing/planned owning workspace |

Add **Prefabs** and **Materials** filters, plus environment/profile kinds as they ship; search display name/path/AssetID. Prefab rows show kind/badge, source path, dirty/cook/dependency state and optional preview thumbnail. Double-click and the visible Edit button take the same route. From Prefabs, **Reveal in Assets** restores the selected asset/filter; **Find in Place** selects its placeable entry. Place and Assets use the same project inventory and logical AssetIDs.

Referenced/missing/invalid sources should remain inspectable with diagnostics rather than vanishing from search. Preserve unsaved prefab drafts when navigating from another asset. Disable editing with an explicit reason where Play/authoring rules require Stop; browsing/selecting itself remains useful. A route is only enabled when its destination is implemented, never an empty tab presented as working prefab editing.

## Model/material editor in Assets

Layout: model preview + material-slot list + typed material Inspector. Selecting a slot highlights its affected submeshes. Show the effective assigned material, imported default, override status and edit owner. Preview controls include orbit/zoom, neutral lighting, explicit **Use Current Scene Lighting**, and a resettable preview exposure. These preview choices do not change the model/scene.

The first form edits actual supported values: base-color factor, base-color texture assignment, roughness, metallic and double-sided. Texture pickers support search, preview, Clear and Open; **Reset to Imported** removes the engine-owned slot assignment deliberately. Group a slider gesture or texture assignment as one Undo command. **Save**, Undo/Redo, stale-writer checks and diagnostics use the existing native authoring/AssetService paths, extended where necessary.

Add normal, packed metallic/roughness, occlusion and emissive slots only alongside their importer/runtime/shader support. Add normal strength, alpha mode/cutoff, UV set/transform, toon ramp, shadow tint, rim and outline controls by qualified MaterialDefinition family. Do not show controls that the selected shader ignores. General shader graphs, arbitrary custom shader code and texture painting remain later vision items.

Khronos defines base-color RGB as sRGB and metallic/roughness channels as linear data (roughness G, metallic B). Carry explicit texture semantics through import/cook/GPU views; a normal or ORM texture cannot use the existing color-only path unchanged. Validate supported channels, UVs and sampler state, and reject unsupported combinations. The exact stylized shader response remains a DarkAngel material-family decision. [glTF material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)

## Authored ownership and reimport survival

Follow DAE-005's **MaterialDefinition / MaterialInstance** split. A definition selects a versioned native shader family, typed parameter schema and allowed render state; an instance owns values and texture references. Initial definitions can be registered engine families; definition authoring/shader publication can follow. Proposed native `.damaterial` sources under `content/materials/` hold instances with their own persistent AssetIDs. Do not create a parallel per-model shader language.

Keep three scopes explicit:

| Edit | Source/owner | Who changes |
| --- | --- | --- |
| Change imported default in the external artist file | Original import source, then validated reimport | Consumers still inheriting that imported default |
| Edit/assign material for this model slot | Native MaterialInstance + model import assignment metadata | All render uses of that model/slot unless locally overridden |
| Give one prefab/placement a different material | Slot-to-MaterialInstance assignment on model component/prefab patch | That prefab's consumers, or just the patched instance |

For the first edit of an inherited imported slot, **Create Editable Material** stages an instance initialized from its current effective imported values and assigns it to that slot. The UI makes that destination explicit. **Edit Shared Material** lists consumers; **Duplicate and Assign** makes a variation. Subsequent edits change the owning native instance, not the imported generated default.

Extend the existing model `.daimport` with a validated material-assignment table keyed by stable source material key/subasset identity. Cook applies those native references after decoding imported defaults; preserve original source mesh/material data. This avoids a second shadow model asset solely to store a few assignments. Publication of new material + import assignment is one prepared/recoverable edit. If this needs an extension of AssetService's coordinated transactions, implement it before claiming atomic Save; do not write one file and hope the second succeeds.

Prefer explicit persistent authoring keys from the normalized source. Reimport preserves matching stable material keys, updates inherited defaults and retains native assignments. Removed, duplicated or ambiguous keys produce a repair list and preserve unresolved assignment bytes. Never remap an override by material array index or display name. A new source without matching keys needs explicit mapping before commit; upstream reordering must not silently put the roof material on the door. Changing a default texture path is not permission to regenerate its logical material identity.

Textures remain ordinary assets/references. Importing/replacing a texture follows the established metadata/AssetID workflow; a path rename does not silently become a new logical asset. Reference deletion is validated against all consumers. Use per-semantic import settings if one image is used both as color and linear data; define derived view/product identity instead of changing every consumer's transfer function globally.

## Prefab and instance material assignments

Extend the Model component with typed material-slot assignments, keyed by stable slot identity, rather than embedding arbitrary material JSON in a scene. A prefab can select a MaterialInstance for a slot; a scene instance can override that assignment and Revert it through the same prefab patch commands. Numeric material variation is **Duplicate Material and Assign** initially, keeping one clear owner for the values.

Changing a material on a prefab affects its linked consumers; changing the scene instance assignment affects that placement. Opening a referenced material to edit it is visibly different from swapping the reference. Swap Prefab retains only compatible slot assignments by explicit stable mapping; missing replacement slots are reported by the swap preview, never guessed from order/name. An ordinary texture/material edit preserves entity, mesh-slot and persistent gameplay identities.

Use render-material bindings independently of gameplay physical-surface IDs. Current collision cooking labels surfaces using imported material IDs; overriding appearance must not silently change footstep/hit/physics semantics or rewrite collision/persistent identities. Geometry-free parameter changes invalidate the needed render products and any dependent light bake, but should not force gameplay collision generation solely because a visual material changed.

## Publication, rendering and implementation increments

Prepared material/model candidates resolve the complete texture/definition dependency closure and validate schema, field ranges and feature availability. The preview can show an unsaved private candidate with a dirty badge. Save rechecks source hashes/revisions and publishes coherently; failure preserves drafts and the prior valid cooked generation. GPU generation activation occurs at a frame boundary with old resources retired after their last use. Fresh Play uses the saved frozen closure; unsaved preview data does not patch live authority/resources automatically.

| Step | Delivery | Acceptance |
| --- | --- | --- |
| A0 | Typed Assets editor routing, model-slot inspection and native material-instance/assignment contract | Existing imports unchanged; generated defaults remain read-only products; no duplicate draft owners |
| A1 | Static-model material values and base-color texture editing | Edit/Undo/Redo/Save/reopen/fresh Play; two model consumers update; one instance override stays local; missing texture/publication failure retains previous state |
| A2 | Reimport and prefab integration | Stable-key source reorder/removal repair; material/assignment transaction; Assets Edit Prefab and return navigation; prefab instance Revert/swap compatibility |
| A3 | Data/normal/emissive textures and qualified stylized families; skinned model parity | Correct color/data transfer, tangent/UV/shader contracts, skin-slot representation and both renderer backends |

Test through the same public asset/material/prefab commands the UI calls. Include non-color maps, stale external writers, shared-versus-local edit scope, slot reordering and missing keys, cold cooked load without artist-source files, and material changes invalidating light bakes. Use one small visual material scene/gate for Assets, then D3D12/Vulkan when binding/shader/texture formats change. No general gameplay/network matrix is needed for a visual tint form.

Likely owners for R5 on the completed refactor baseline: Assets workspace/routing, native authoring, AssetService/import metadata, model/texture cookers/loaders, the shared component/prefab command layer and renderer material resources. Preserve current pins; DiligentFX examples are optional implementation research, not automatic dependencies. The earlier prefab publication/link foundations still apply.
