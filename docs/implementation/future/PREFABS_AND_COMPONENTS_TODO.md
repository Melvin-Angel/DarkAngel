# TODO (high priority): prefab workflow and "Add component"

**Status: not implemented. The user considers this important for the engine to be usable** (14 October 2026). It should be planned before or with the editor-wide UX/UI pass, ahead of building real levels.

## What the user asked for

- Altars, doors, gates, platforms, pickups and similar objects need their own **3D models attached**, and they are reused many times across scenes. That calls for a **prefab** workflow: define a configured object once (model plus components and their settings), place it many times, and change the definition in one place.
- The Scene / Level Design Inspector needs an **Add component** button, so any object can gain a component (altar, mover, pickup, interactable, model, and so on). Today each component only comes from its own Place item.
- Probably a dedicated **Prefabs tab** (or a prefab editing mode) to create and edit prefabs.

## What exists today (foundation to build on)

The scene assembly format already has most of the data model; `engine/runtime/assembly.cpp` `resolve_assembly`:

- **Mounts:** an assembly (scene) source can mount another assembly asset under a stable mount ID, with per-placement **patches** (typed edits). Nested to depth 16, cycles refused, at most 128 mounts.
- **Variants:** an assembly can be a `base` plus `patches` (one variant level), pinned by `base_fingerprint` so a changed base needs an explicit rebase.
- **Exports:** mounted assemblies expose named objects for references across the mount boundary.
- **Stable identities:** object identities derive from the mount path, so the world ledger's persistent IDs stay stable per placement. Two placed copies of an altar prefab are two distinct persistent objects, which is what altar discovery, door state and pickup collection need.
- **Components:** gameplay components (`altar`, `mover`, `pickup`, `interactable`, `entry`, `transition`, `encounter`, `spawn`, `model`, `audio`, `collision`) are plain JSON on scene entities, validated by their decoders, and can already sit together on one entity. A model on an altar, door or pickup object is drawn, and is excluded from static collision (movers ride their kinematic box).

What is missing is mostly **editor**, plus a few runtime checks:

1. Prefab assets: a "prefab" is an assembly asset meant for mounting (create, rename, delete, list), probably its own extension or kind for pickers.
2. **Create prefab from selection** (one object, later several), and **Place prefab** from the Place panel, which mounts it.
3. A **Prefabs tab** (or isolated edit mode) that edits the prefab's own entities with the same Inspector, a preview, and Save/Undo through the existing NativeAuthoring/EditorDocument paths.
4. **Per-instance overrides** in the Scene Inspector, shown as overrides and stored as mount patches, with Revert and "Apply to prefab".
5. **Add component** in the Inspector: a list of component kinds with sensible defaults (the same defaults the Place items use today), plus Remove component. The editor already has `scene_set_component`; Add/Remove is mostly UI plus validation of incompatible pairs (for example a spawn point cannot carry other components).
6. A **model component picker** in the Inspector (choose or replace a model on any object), so an altar or door gets its mesh without placing a separate model object.
7. Propagation: editing a prefab updates every placement on the next Play preparation. Persistent state of existing placements is keyed by mount path and is unaffected.
8. Validation: the patches of mounts whose prefab changed shape are reported (the existing base-fingerprint rule for variants is the model).
9. Gates: create a prefab from an altar with a model, place it twice in two scenes, change its model in the Prefabs tab, and check that both placements change while their discovered state stays separate.

## Open design questions (ask the user when this is planned)

- Prefab file kind and folder convention (for example `content/prefabs/*.daprefab` or reusing `.dascene` assemblies).
- Whether nested prefabs (a prefab containing prefabs) are needed in the first version (the format supports depth up to 16).
- The Prefabs tab layout: a separate tab, or an "edit prefab in context" mode in the Scene tab.
- Whether a prefab can carry scripts (bindings already resolve inside mounts).
