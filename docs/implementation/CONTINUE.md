# Development continuation

Updated 8 October 2026 in C:\DarkAngel. Work is authorized autonomously through Loopback/GNS. Preserve local work, commit coherent checkpoints and never push; there is no remote. Do not upgrade pins. M3 live EOS remains Blocked on private deployment/policy and two identities. MCP SDK1.32.1 remains the tested 2025-11-25 compatibility profile. M4/M5 are In progress; start M6 only after their required gates pass.

The original requested checkpoints a447242/4175978 are in history. This continuation began at clean 9c22c46. Current verified source checkpoint precedes the pose-composition commit at f6770ae; obtain the exact final HEAD with `git log -5 --oneline` and `git status --short`. No other user's changes were overwritten.

Coherent checkpoints:
- ed81b83: initial M4 gates, committed before M5 began.
- 5ff194f/9c22c46: canonical human rig/skin foundation and bounded cook publication.
- b8ed803: isolated authoritative character history.
- f6b10aa: four real canonical FBX clips/Ozz/root tracks.
- 36b9529/3d821d7: server crate, native collision authoring, qualified queries/sensor ownership and root/native velocity isolation.
- b5447b3: immutable static cave geometry/cook/material/backface/history.
- dea3c8d: translation optimization, retaining authored rotations within the tighter probe gate.
- 7982113: native action timeline/cook/editor transactions and replay cue identities.
- 05185db/58962cc: atomic WorldSession collision streams, network-fed isolated owner reconciliation and distinct observer presentation.
- 90583f4/93b7d88/0169f74: fragment timeout/prepared ACK, local CPU/C++/Jolt allocation measurements and full-axis platform profile.
- f6770ae: wire envelope2 and prepared current-topology client/server control fence.
- Final pose checkpoint: four regular/masked Ozz layers, reusable independent contexts/buffers and uncompressed glTF reference checks; use git log for its hash.

Latest acceptance: **41/41** optimized native tests; separate GNS host/client executes 90 owner commands at x=7.125 and verifies character/dynamic-crate state plus native-prepared collision ACK tick90. Pose-focused build-without-animation checks pass3/3; prior collision/readiness gates pass4/4. These are scoped receipts, not full milestone verification. The GNS fixture is paced at40ms wall time per fixed step and is not a realtime performance proof.

M4 implemented profile: fixed60Hz motor, slopes/steps/stance/jump/impulses/root obstruction and validated teleport, yaw/pitch/roll slow platform support, dynamic server boxes, collision queries/sensors, immutable mesh history and bounded isolated replay. Collision streams have atomic bounded fragments, loss/reorder/duplicate handling, timeout, explicit native-prepared ACK and control fences. Native scene geometry is cooked/validated; runtime prepares immutable Jolt acceleration shapes once and history shares them. Opaque serialized Jolt BVH cache is not implemented. Replay never rewinds live physics or replays authoritative prop impulses.

Local i7-1185G7 measurement: live p99=77.1us, isolated30-command replay p95=1.427ms; four motors/three props/eight-triangle cave/queries/history. Grounded jitter0.000439mm. Replay requests ~9.103MB through Jolt, including8MiB scratch; requested bytes are not resident/peak memory. See M4_REPORT.md for settings, tolerances and limits.

M5 implemented foundation: frozen Ozz744eb9d99f606eda849acb0b1204f7a3dc20bca1, canonical81-bone human, one Body_Legs skin fixture, idle/run/attack/dodge bpy-converted clips, optimized translation keys/retained rotations, root tracks, CPU sampling/blending and native action timelines. Source FBXs differ in rest pose; explicit offline normalization and eight facial rest joints are documented. Runtime retargeting remains excluded. Compression probes are below0.5mm; blended probes0.132-0.224mm. Full modular rendering/visual quality is not qualified.

Required next work:
1. M4 automatic clock-offset/jitter/input lead and redundancy; RTT0/50/150/250ms, loss0/2/5percent and wider coupled-player faults. Complete safe-position cell preload/unload, ownership/lifecycle barriers and bandwidth/full gameplay stress. Current readiness fence covers one region; extreme tilt/angular stress and configurable inherited yaw remain open.
2. M5 immutable typed Animation Graph compile/evaluation plan,1D/2D locomotion/sync/transitions, masks/additive/action slots and optional IK. Bind integer action time to tick socket poses/root requests and motor-achieved movement; add slot/channel arbitration, combo and owned cleanup.
3. Native abilities/effects/tags/attributes behind the World facade: atomic prepare/cost/cooldown/reservations, per-source effect ownership, immunity/stagger/death and all DAE-008 gates. Implement authoritative hit intervals/projectiles, accepted/rejected/included prediction bundles and deduplicated replay/presentation; ghosts never damage; late join restores current state.
4. Native graph/mask/action/combat cook and EditorDocument authoring, full modular human and dedicated nonhuman skin/clip/GPU validation, motion-quality acceptance and native RmlUi health UI. Preserve native theme/edit/Play/undo/save. Follow full DAE-006/007/008/009/010/011/013, not only this summary.
5. Begin M6 world ledger/streaming/Recast/BT.CPP only after required M4/M5 gates. Do not mark them Verified prematurely.

Commands after changes (avoid rerunning unchanged full suites):
- `python scripts/verify_pose_blend.py`: latest integrated pose/clip reference, original/derived hash fences, GNS and3 no-animation checks. Add `--convert` only when regeneration is needed.
- `python scripts/verify_collision_ready.py`: version2 readiness/control, raw premature command rejection,40 loopback inputs, history, GNS and4 no-animation checks.
- `python scripts/verify_platform_axes.py` / `verify_budgets.py` / `verify_actions.py`: scoped extensions with separate receipts.
- Focused builds: `python -c "import sys,subprocess; sys.path.insert(0,'scripts'); from msvc_environment import activate; sys.exit(subprocess.call(['.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build','build/m5-relwithdebinfo','--target','AnimationClipTests','--parallel','2'],env=activate()))"`
- Then `build\m5-relwithdebinfo\AnimationClipTests.exe` (or the changed focused target).

Evidence is under docs/implementation/evidence, retaining earlier M3/initial receipts separately. M4_REPORT.md and M5_REPORT.md retain exact tested settings and unresolved scope. Current uncommitted work should be empty after the final local commit; verify before edits.

Read-only external source: C:\Unity Projects\AshenRootsMP\Assets\thirdparty\3D\Blink. Human source: Art\Characters\LowPoly\FREE_HumanLowPoly\Meshes_Humans\HumanMale_Character.fbx, SHA2560c99d39d08d123a1114c6cf2e19d3e089f14501bea887250fea6748374d2122f. Animation source: Art\Animations\Animations_Starter_Pack. The user also suggested Unity Games/Custom Characters/Bink; inspect actual paths before use. Installed Blender4.5.1LTS at C:\Program Files\Blender Foundation\Blender 4.5\blender.exe. Existing bpy scripts convert into ignored owned .cache/fixtures; never modify originals. Walk content was not found in the inspected starter pack.

Last usage check: five-hour90percent used, weekly36percent; no reset credit consumed. Read fresh usage with get_usage_limits before starting a large increment. Preserve exact commits, completed work and next commands before any limit interruption; do not consume reset credits without explicit per-use human confirmation.
