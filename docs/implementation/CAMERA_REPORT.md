# Player virtual cameras: free-look and aim - 11 October 2026

User request: Cinemachine-inspired virtual cameras on the Player that blend; free-look orbit during normal movement and an over-shoulder aim camera while the ranged input is held; follow and look targets for later lock-on, dialogue and cutscenes; authoring with a preview in the Player tab; room for screenshake. M4/M5 remain In progress.

Work was split across three sessions: Claude Code wrote the runtime, cooker, editor integration and tests; a second agent session enabled the build guard, applied the follow-up patch, made the camera clock deterministic for scripted runs and added the scripted mouse-look check; Claude Code then inspected the captures, moved the preview in the form, re-verified and merged.

## Delivered

- **Runtime** (`engine/runtime/camera.hpp/.cpp`, presentation only): `VirtualCameraDefinition` (third-person orbit body: distance, pivot height, shoulder offset, field of view, pitch range, blend-in time, follow smoothing, facing mode, optional look-target framing), `CameraRigDefinition` (one to eight cameras, validated bounds, at least one unconditional default) and `CameraDirector`, the blending brain. The live camera is the eligible one with the highest priority. A change eases from the previous output pose over the new camera's blend time. Follow smoothing is exponential and snaps on a large jump. Shake is an additive, decaying, bounded displacement of the camera position (`CameraDirector::shake`); nothing triggers it yet.
- **Activation** is decoupled from the cameras themselves: each camera is live "always", "while a semantic input action is held" or "while a gameplay tag is present on the owner" (owner tag snapshot). The built-in rig is FreeLook plus Aim on `combat.ranged` (Left Alt / gamepad left trigger).
- **Targets**: every update takes a follow target and an optional look target. The player supplies the follow target today. A camera with "frame the look target" orbits to keep the follow target framed toward a supplied look target; that is the hook for lock-on and dialogue and is covered by unit tests only.
- **Player asset**: optional `cameras` array on `.daplayer`, validated and frozen by the actor cooker; a Player without it uses the built-in rig, and kit-only Play does too.
- **Game**: the view and field of view come from the director. Mouse (while the viewport has control) and the `look.turn` stick axis orbit the camera. Movement is camera-relative. Under a camera whose facing is `movement` the character turns toward its movement direction and keeps its facing when idle; under `camera` facing (aim) it faces with the camera and strafes. The Game profiler shows the live camera.
- **Authoring**: Player tab, Cameras section. "Customize cameras" writes the built-in rig into the draft; each camera has name, priority, "Live when" (always / held input from the Player's input profile / registered gameplay tag), the body values, facing and look-target framing; add, remove and "Use built-in rig". "Preview this camera" renders the scene from that drafted camera around the scene's player placement, with orbit and pitch sliders, at the top of the section. Edits go through the existing draft history, private validation and coordinated Save.

## Decisions

- Aim is selected by the held ranged input, not by new `Camera.Aim` / `Camera.FreeLook` tags. Registering tags needs the generated-tag rebuild fences and the tag would have to be granted authoritatively; aim is local presentation intent today. Tag conditions are supported, so an aim Ability can grant a tag later and cameras can switch on it without code changes.
- Camera look reads raw mouse delta because the input system has no mouse-axis control. Stick yaw uses the existing `look.turn` action; there is no `look.pitch` action in the Royal input profile, so a gamepad cannot pitch yet.
- Scripted editor runs advance the camera on the fixed simulation step so blends are reproducible; interactive runs use frame time.
- Free-look now turns the character toward movement instead of strafing, and Q/E / right stick rotate the camera rather than the character.

## Verification

`python scripts/verify_effect_reaction.py --build`: build plus seventeen gates pass ([receipt](evidence/effect-reaction.json)).

- `CameraTests`: rig validation; selection by held input and by tag with priority and default fallback; orbit geometry and pitch clamps; over-shoulder offset to the actor's right; monotonic blend in and back out; follow smoothing and teleport snap; look-target framing only on tracking cameras; bounded, decaying shake and its budget.
- `NativeAuthoringTests`: Player camera rig authored with Undo/Redo; a rig without a default, an out-of-range field of view and a duplicate name rejected by private validation with the valid draft retained; coordinated Save; the frozen Player carries three cameras with their conditions; Play resources carry them and kit-only Play keeps the built-in rig; the authored conditions drive the director.
- D3D12 editor, each capture inspected: [free-look](evidence/camera-free-look-d3d12.png) after a scripted right/up mouse delta (yaw and pitch equal the expected values, camera still FreeLook); [aim](evidence/camera-aim-d3d12.png) with Left Alt held from frame 8 (live camera Aim, blend complete, field of view 55, camera 2.78 m from the player and over the right shoulder, character facing with the camera, prediction pending 0, scene unchanged); [Player-tab preview](evidence/camera-preview-d3d12.png).
- The existing reaction, death, dodge and mask D3D12 exercises still pass with camera-relative movement.
- Full editor-build CTest after the final change: 82 of 87 pass (CameraTests is the new entry); the five failures are the same unrelated older ones listed in EFFECT_REACTION_REPORT.

Not verified: a physical mouse or gamepad (the look check is a scripted delta), Vulkan, the tag-driven camera in Play (unit and authoring tests only), look-target framing in Play, shake in Play, older verify scripts other than this one (any that script lateral movement now turn the character instead of strafing).

## Not delivered

Camera collision/occlusion, lock-on and dialogue cameras, cutscene or fixed/dolly bodies, per-camera blend curves or custom blends between specific pairs, recentering, aim assist, shake authoring or triggers, a `look.pitch` stick action, invert/sensitivity settings, cameras for non-player actors.
