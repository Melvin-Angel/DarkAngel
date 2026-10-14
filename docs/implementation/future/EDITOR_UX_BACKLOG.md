# Editor UX backlog

Items for the editor-wide UX/UI pass the user has scheduled after M8 and before M9. Each entry records what the user asked for, what the editor does today, and notes for the implementer. Entries are **not implemented** until marked otherwise.

## HIGH PRIORITY TODO: prefab workflow and Add component

The user considers this important for the engine to be usable: reusable prefabs (altars, doors and so on with their own models), an Inspector **Add component** button, and a Prefabs tab or editing mode. The full note is in [PREFABS_AND_COMPONENTS_TODO.md](PREFABS_AND_COMPONENTS_TODO.md). The scene assembly format already supports mounts with per-placement patches, variants and exports; the work is mostly editor workflow.

## TODO: Scene tab camera — Orbit and Free look modes (requested 14 October 2026)

**User request (paraphrased):** "The main issue is that I can't move and rotate the camera in the scene level design view, so I can't get a good sense of where I place objects. I would like two camera modes in the Scene tab: orbit and free look. In free look we should be able to fly around, similar to Unity; in orbit mode we should orbit the currently selected object or objects."

**Today:** the Scene workspace renders from a fixed view (`XMMatrixLookAtRH` from (26, 24, 34) towards (0, 1, 0) in `apps/editor/main.cpp`). There is no navigation, so placing and positioning objects relies on the gizmo and Inspector values alone. Play uses the Player camera rig, and Character/Composer previews have their own small orbit; neither helps scene authoring.

**Wanted behaviour:**

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
- Switching modes keeps the current view; the orbit pivot is taken from the selection at the moment of switching.
- Viewport input must not fight the transform gizmo, picking or the Place panel's drop-to-place. Camera drags start only when the gizmo is not hovered, and a click without a drag still selects or places.

**Implementation notes:**

- Placement and picking already read `shell.scene_view` / `shell.scene_projection` for each frame, so a moving camera should only need the view matrix to come from the new camera state instead of the fixed `LookAt`. The projection near/far planes may need to follow distance.
- Multi-selection does not exist yet (`editor.selected` is a single object). Orbit-around-several needs a selection set or an equivalent. Until then, orbit the single selection.
- The camera state belongs to the editor shell (per open scene; optionally remembered per scene in user settings, not in the scene source).
- This is editor-only presentation: no runtime, scene-format or gameplay change.
- The existing `CameraTests` / camera rig code is for gameplay cameras; reuse the maths where it fits, but keep the editor camera separate from authored Player camera rigs.
- Gates: a scripted exercise that switches modes, flies and orbits, then checks the view matrix and that picking and placement land where the cursor points.

## Notes from the M8 hand test (14 October 2026)

- Dropping objects onto rooftops or other geometry is intended behaviour (the user confirmed it is fine). The real authoring problem was not being able to see the scene from other angles, which the camera TODO above addresses.
- Optional, low priority: an Interact-activated transition, pickup or altar placed far above or below the ground can never be reached. A validation hint in its Inspector ("out of reach of any walkable ground") could help, but this is not requested.
