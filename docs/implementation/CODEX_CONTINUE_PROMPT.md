Continue authorized DarkAngel development in C:\DarkAngel. This prompt was written by the Claude Code session of 10-11 October 2026 when its usage ran out. Read it fully, then read the reports it names before editing. Treat this as a mature native engine, not a greenfield project.

## 0. Do not trigger a dependency rebuild

The last tool change cost a 40 minute rebuild of every vcpkg package. What happened: a CMake regeneration re-ran the vcpkg manifest install, vcpkg decided every package's ABI hash had changed, removed them and rebuilt all of them from source. Versions and pins were unchanged. The recorded ABI inputs now contain `powershell 7.6.3`; a different PowerShell in the earlier Codex environment is the suspected cause but was never confirmed.

Rules:
- Build only through the pinned environment: `scripts/msvc_environment.py` `activate()` plus `.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe --build build/m5-editor-relwithdebinfo --target <targets>`. The existing `scripts/verify_*.py --build` scripts already do this. Do not invent a toolchain, do not reconfigure with different options, do not delete `build/`, `vcpkg_installed/` or the vcpkg buildtrees.
- A regeneration happens whenever `CMakeLists.txt` or a cooker source under `engine/assets/` changes (their hash feeds the configure step). That is normal and should print "already installed" for every package.
- Before the first build, compare your environment with the recorded one: `pwsh --version` should be 7.6.3 and `C:\Users\bengt\AppData\Local\DarkAngel\vcpkg-buildtrees\zlib\x64-windows-darkangel.vcpkg_abi_info.txt` shows the expected `cmake`, `powershell` and triplet lines. If PowerShell differs, tell the user before building instead of proceeding.
- Watch the first build. If `vcpkg.exe` appears and starts compiling packages (new timestamps under the buildtrees folder, cmake child processes building `protobuf`, `openssl`, ...), you have hit the same problem. Do not interrupt it half way (vcpkg has already removed the old packages); let it finish and report it. Prior binaries may restore from `%LOCALAPPDATA%\vcpkg\archives`, which would make it quick, but that was not tested.
- Untested option if the user wants a hard guard: set `VCPKG_MANIFEST_INSTALL=OFF` in the existing build directory's CMake cache so regeneration never invokes vcpkg. Ask first.
- Never run two editor or build processes against the build directory at once.

## 1. Protect the workspace

Start with `git status`, branch, HEAD and `git log --oneline -15`. `master` is pushed to `origin` at `13eece6` (plus the commit that adds this file). Unfinished camera work is on branch `wip/camera-system` (see section 4).

Never stage, clean, reset or overwrite these untracked user paths: `content/animation/clips/`, `content/characters/new_character.dacharacter`, `content/royal_district/RoyalCombat.dascene.records/`. Stage explicit owned files only. The Unity project `C:\Unity Projects\AshenRootsMP` is a read-only reference and animation/model source; never write to it. Pushing to `origin master` is authorized by the user now. `gh` is not installed.

## 2. Read first

- docs/vision/DarkAngel_Engine_Vision.md, docs/architecture/Actor_Gameplay_Design.md (its final sections record the mask/stance and dodge decisions), docs/architecture/DarkAngel_Implementation_Handoff.md (M0-M9 gates).
- docs/implementation/EFFECT_REACTION_REPORT.md (reactions, real animation sources, death, dodge, suite status), MASK_REPORT.md (masks, switching), M5_REACTION_PRESENTATION_PLAN.md (decision table), STATUS.md and CONTINUE.md headers.
- Older reports: UPPER_BODY_ACTION_REPORT.md, OBSERVED_CHARACTER_POSE_REPORT.md, TAG_GRAPH_REPORT.md.

Core contracts are unchanged: AssetService owns sources/catalog/CAS/frozen generations; candidates validate before publication and failures retain old source/catalog/playable resources; WorldSession owns authority through the shared protocol paths with no local-host bypass; the Jolt motor owns movement; presentation never decides gameplay; generated tags need the regeneration/rebuild fences.

## 3. What this session delivered (all on master, verified as stated in the reports)

1. Effect reactions: an Effect can reference a presentation-only Composer timeline; a stateless selector plays it from the replicated effect-instance clock for owner and observed poses.
2. Animation sources: 15 canonical-rig Blink clips converted into `content/animation/blink/` by `scripts/prepare_blink_clips.py`; owned Royal `flinch` effect/action (GetHit), unbound by default.
3. Death: optional kit death timeline with terminal precedence and a held final pose; late join holds, never replays.
4. Dodge: per user decision an ordinary Ability on an optional kit utility ingress (`CombatSlot::Dodge`, the eight canonical slots are unchanged), with authored `dash` distance/window and `face_movement`. One animation. Owned Royal dodge is bound in `player.dakit` (Keyboard C / gamepad East). Movement input is world-space, so the dodge faces `atan2(x, z)`.
5. Masks: `.dafacemask` native asset owning a kit plus a worn model, tint and head-socket attachment; Players equip four slots and start with one active; the user's HornedMask model is converted and worn; owned Ember/Earth/Water/Air masks, `royal_human` Character and `royal_player`.
6. Mask switching in Play: a mask-select request is an ability intent just past the ability slots, handled inside `AbilityState` loadouts, predicted and reconciled; owner/public snapshots carry the active loadout.
7. Editor: grouped asset browser, Reaction/Dodge/Death/Mask/Player forms, Game readouts.

Reproduce: `python scripts/verify_effect_reaction.py --build` (build plus fourteen gates passed at `c85e4c4`; mask switching was verified afterwards by NativeAuthoringTests and a full CTest run but that script was not re-run after it). Full CTest in `build/m5-editor-relwithdebinfo`: 81 of 86 pass. The five failures predate this work and were left alone: `M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure` (staging write fails under the long-named untracked scene-records folder, probably Windows path length), `M5.composer_preview` ("Tagged graph requires an explicit actor tag snapshot"), `M4.royal_mesh_collision` (its CMake target does not link; it omits `apps/editor/native_authoring.cpp`).

## 4. In progress: camera system (branch `wip/camera-system`, NOT verified)

User request: Cinemachine-inspired virtual cameras on the Player that blend; two gameplay modes now (free-look orbit, over-shoulder aim while the ranged input is held); follow and look targets for later lock-on/dialogue/cutscenes; configurable in the Player tab with a preview; room for screenshake.

State of the branch:
- `engine/runtime/include/darkangel/camera.hpp`, `engine/runtime/camera.cpp`: `VirtualCameraDefinition`, `CameraRigDefinition`, `validate_camera_rig`, `default_camera_rig` (FreeLook + Aim on `combat.ranged`), `CameraDirector` (shared orbit angles, selection by held input action or owner gameplay tag with priority, eased blending from the previous output, follow smoothing with teleport snap, optional look-target framing, bounded decaying shake). `tests/camera_tests.cpp` covers it. Written, never compiled.
- Integration applied to the tree but the build had not finished when usage ran out, so assume compile errors: `CMakeLists.txt` (camera.cpp in DarkAngelRuntime, `CameraTests`), Player `cameras` array in `engine/assets/actor_cooker.cpp` (`decode_camera_rig`, `PlayerDefinition.cameras`), `CharacterPreviewResources.cameras`, the Player form "Cameras" section with a preview image in `apps/editor/editor_authoring.cpp`, and `apps/editor/main.cpp` (director-driven Game view and FOV, camera-relative movement, character turns toward movement under free-look and faces the camera under aim, mouse look from ImGui mouse delta while the viewport has control, Player-workspace preview view).
- `scripts/handoff/camera_followup_patch.py` (with `ed.py`) is the not-yet-applied second half: NativeAuthoringTests camera authoring block, `--exercise-aim` and `--exercise-camera-preview` editor checks, and verify-script gates. Apply it only after the first half builds; it is a string-patch script and may need adjusting.

Decisions already made, keep them: camera activation is by held semantic input or by gameplay tag (both supported) instead of new `Camera.Aim` tags, because registering tags needs the generated-tag rebuild fences and aim is local presentation intent today; an aim Ability could grant a tag later without changing cameras. Camera look uses raw mouse delta because the input system has no mouse-axis control; gamepad yaw uses `look.turn`, and there is no `look.pitch` action in the Royal profile yet. No camera collision yet.

Finish it: get it compiling, run CameraTests and NativeAuthoringTests, check the existing D3D12 exercises still pass (movement is now camera-relative and free-look turns the character toward movement, which can change older scripted captures that strafe), verify the shoulder side and mouse directions by capture, write CAMERA_REPORT.md, update STATUS/CONTINUE, merge to master and push.

## 5. M4/M5 status and what blocks M6

M4 (character simulation) and M5 (animation and combat) are both In progress. M3 live EOS is externally blocked. M6-M9 are not started. M6 may begin only when the original M5 gate passes: attack, dodge, stagger, damage and death under host/client authority; acceptance/rejection and replay producing one presentation; no damage from predicted ghosts; late join restoring current state; runtime Health UI.

Done toward that gate: attack, damage, authoritative effects, flinch presentation, death presentation, dodge, masks and switching, public observed poses, late-join rules for reactions and death.

Remaining for M5, roughly in priority order:
1. Camera system (section 4).
2. Runtime Health HUD through RmlUi with native view models (required by the M5 gate; RmlUi is acquired but has no renderer integration yet, so start with a focused adapter gate).
3. Stagger as gameplay, not only a flinch pose: motor restriction/knockback policy from effects; stun loop presentation (the Blink `stunned` clip is converted).
4. Something that attacks the player, so player-side flinch, death and dodge invulnerability can be verified in Play. This is shared combat-target behaviour, not the M6 NPC controller.
5. Mask depth: distinct kits per element, equipped versus selected grants, per-mask death timeline and locomotion graph on switch, switch feedback.
6. Ranged/projectile abilities, block/parry outcomes, combos, lock-on.
7. Retarget profile for the non-Blink Unity clips (user said this is not immediately necessary).
8. Host/client qualification: the dodge, switching and reaction paths over GNS specifically, plus the M4/M5 fault/latency/performance matrices; Vulkan for the new presentation.

Still open user decisions: final armour/poise/damage formulas; whether to add a dedicated `face_mask` socket to the canonical skeleton (doing so invalidates every converted clip and skin hash).

## 6. Working rules

Small coherent vertical increments. For each: source edit, Undo/Redo, private validation, coordinated Save, complete scene closure, fresh resources, fresh isolated WorldSession Play, one D3D12 capture that you actually inspect. Use copied disposable fixtures. Report honestly what was not verified. Update STATUS, a report and the CONTINUE header for substantive work; preserve historical receipts. History stays MVP. No new dependencies, no subagents, no edits to external assets.
