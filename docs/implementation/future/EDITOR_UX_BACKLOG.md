# Editor UX backlog

User requests and implementation notes. R1 Scene navigation is **Implemented / locally verified**; R2 is **In progress**, with the source audit, shared design system and task-specific layout implementation tracked in the [R2 report](../current/EDITOR_UX_OVERHAUL_REPORT.md). Other entries remain **Planned / not implemented** unless explicitly marked otherwise. The [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) owns priorities: R1 Scene navigation, then the dedicated R2 UX overhaul. [EDITOR_UX_OVERHAUL_PLAN.md](EDITOR_UX_OVERHAUL_PLAN.md) owns its audit, shared standards and phases 2.1–2.3. The older after-M8/before-M9 description is superseded; historical M8/M9 IDs are unchanged.

## R4 requirement: prefab workflow and Add Component

The user considers this important for the engine to be usable: reusable prefabs (altars, doors and so on with their own models), an Inspector **Add component** button, and a Prefabs tab or editing mode. The full note is in [PREFABS_AND_COMPONENTS_TODO.md](PREFABS_AND_COMPONENTS_TODO.md). The assembly format already supports mounts, typed patches, variants and exports, but usable prefab links, mount transforms, gameplay-field patches, identity retention and coordinated publication still need implementation. The [prefab proposal](../PREFAB_WORKFLOW_PLAN.md) and [placement design](PREFAB_PLACEMENT_DESIGN.md) own those contracts. R2 audits existing surfaces and specifies shared conventions; complete prefab authoring/publication is R4 (C + F).

## R1: Scene tab camera — Orbit and Free look modes (requested 14 October 2026)

**User request (paraphrased):** "The main issue is that I can't move and rotate the camera in the scene level design view, so I can't get a good sense of where I place objects. I would like two camera modes in the Scene tab: orbit and free look. In free look we should be able to fly around, similar to Unity; in orbit mode we should orbit the currently selected object or objects."

**Delivered 15 October:** Scene now uses an editor-owned per-scene camera with Orbit/Fly, pan/zoom, bounded adjustable speed and bounds-based Focus. The default view starts at (26,24,34) toward (0,1,0). See [R1 implementation report](../current/SCENE_NAVIGATION_REPORT.md) for implementation, six focused passing checks, captures and synthetic-input qualification. Play and Character/Composer preview cameras keep their own paths. The requirements below remain the R1 contract; they no longer describe missing navigation.

**R1 behavior contract (delivered, with single-selection scope):**

- A camera mode switch in the Scene tab: **Orbit** and **Free look**. It should be a visible toggle on the viewport, with a keyboard shortcut.
- **Free look (Unity-like fly camera):**
  - Hold the right mouse button to look around (yaw and pitch).
  - While held, WASD moves, Q/E move down/up, and Shift speeds up.
  - The mouse wheel changes fly speed (or dollies when the right button is not held).
  - The middle mouse button pans.
- **Orbit:**
  - Orbits around the current selection, using the centre of the selected object or the combined bounds of several selected objects.
  - Alt + left drag (or right drag) orbits, the middle button pans the pivot, and the wheel zooms towards the pivot.
  - "Frame selection" (F) moves the pivot to the selection and fits its bounds in view.
  - With nothing selected, it orbits the last pivot (or the scene origin).
- Show adjustable movement speed with visible current value, bounded slow/fast settings and reset; display effective navigation shortcuts in viewport help.
- Switching modes keeps the current view; the orbit pivot is taken from the selection at the moment of switching. The delivered shortcut is **Shift+F**; **F** and the visible Focus button frame the selection; **Esc** releases navigation.
- Viewport input must not fight the transform gizmo, picking or the Place panel's drop-to-place. Camera drags start only when the gizmo is not hovered, and a click without a drag still selects or places.

**Implementation notes:**

- Placement and picking already read `shell.scene_view` / `shell.scene_projection` for each frame, so a moving camera should only need the view matrix to come from the new camera state instead of the fixed `LookAt`. The projection near/far planes may need to follow distance.
- Multi-selection does not exist yet (`editor.selected` is a single object). Orbit-around-several needs a selection set or an equivalent. Until then, orbit the single selection.
- The camera state belongs to the editor shell and is remembered per scene within this editor session. Persistent user preferences remain R2; camera navigation is not serialized into scene source.
- This is editor-only presentation: no runtime, scene-format or gameplay change.
- The existing `CameraTests` / camera rig code is for gameplay cameras; reuse the maths where it fits, but keep the editor camera separate from authored Player camera rigs.
- Gates: a scripted exercise that switches modes, flies, pans/zooms, changes speed and focuses a selected object, then checks view continuity, selection bounds and that picking/placement land where the cursor points. Include empty selection, text focus, gizmo drag/cancel, release outside the viewport and Play input isolation. Until multi-selection exists, focus/orbit a single object; retain combined bounds as a later selection extension. Confirm controls are discoverable in a short author pass. R1 acceptance must pass before R2 qualifies the integrated toolbar/workflows.

## Notes from the M8 hand test (14 October 2026)

- Dropping objects onto rooftops or other geometry is intended behaviour (the user confirmed it is fine). The real authoring problem was not being able to see the scene from other angles, which the camera TODO above addresses.
- **Deferred, optional low priority:** investigate reachability hints for Interact-activated transitions, pickups or altars far above/below walkable ground. Height alone is not proof of unreachability. A qualified Inspector warning could help, but it is not a required R1/R2 gate.

## Deferred technical work

The main roadmap retains scene-scale phase 6 GPU instancing, reduced-feature build verification, cross-machine networking and EOS validation separately from core usability. None is marked complete by this backlog; the optional reachability warning above stays here for traceability.

## R9 World Tools integration

The deferred future volumetric terrain proposal is specified in [VOLUMETRIC_TERRAIN_WORLD_AUTHORING_PLAN.md](VOLUMETRIC_TERRAIN_WORLD_AUTHORING_PLAN.md). It is not scheduled. If activated by a future roadmap decision, its terrain, spline, water, paint, scatter, placement, and greyboxing tools belong inside the existing Scene / Level Design workspace and must reuse this backlog's shared selection, viewport, gizmo, command-history, undo/redo, property, layout, and background-job conventions. Do not create one top-level tab per tool or a parallel scene format. R8 remains the separately deferred World Puzzle UI slot.
