# Current Royal scene handoff - 8 October 2026

Implementation HEAD: 116c10f (native viewport locomotion). This handoff is the latest continuation summary; earlier CONTINUE entries retain historical evidence.

Completed local commits this run:
- a4cada4: all54 Royal District static conversions/native cooks.
- b6580ac: complete14-part canonical binary skin/cook/load; native45/45.
- 5380bc3: bounded native multi-root packages and atomic conflicting-generation rejection.
- 156a210: four-object native Royal village/GPU skin; exact CPU/GPU rest and deformed attack reference on D3D12/Vulkan.
- bfcb102: eight owned Omni cardinal walk/run clips plus Blink idle; bounded graph catalogue/playback rate; native45/45.
- dd6a0a0: local WorldSession/motor/prediction/achieved-movement graph coordinator; native46/46.
- 116c10f: interactive native viewport controls/follow camera/GPU tick poses;120 fixed ticks on D3D12/Vulkan and native editor regression pass.

Current result:
- content/royal_district/RoyalVillage.dascene contains ground32x32, two huts and the complete skinned canonical human.
- All original Unity FBXs/textures remain read-only; dependencies unchanged. Owned glTF/GLB/.daimport sources are committed.
- Launch: powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1
- Press Play, click viewport. WASD moves, Shift walks, Q/E turns, Esc releases input. Pause/Step/Stop and native editing/undo/save/open remain available.
- Standing cardinal walk/run blending uses nine frozen clips and native graph. Per-session RigPose supplies GPU palettes from fixed ticks; render sampling does not drive the motor.
- Collision is the explicit initial solid rotated box-proxy profile from cooked bounds/current document transforms. Doorways/interiors are blocked. Precise mesh collision is NOT qualified for this scene.
- Native test baseline46/46 at dd6a0a0. Latest scoped GUI checks, no-animation editor compile and GPU reference checks pass. New scene acceptance is local Loopback; independent GNS receipts are retained from prior M4 work, not refreshed or claimed for this scene.
- Attack is only a verified cooked/deformed pose diagnostic (launcher -AttackPose). There is NO playable ability/damage implementation yet. Do not say the requested one attack is complete.
- Omni Idle audit:30Hz, frames1..1183, exceeding the300-frame conversion cap; use the validated104-tick Blink idle. No timing cap or compression budget was widened.

Editor planning update (8 October 2026): read [EDITOR_WORKFLOW_PLAN.md](EDITOR_WORKFLOW_PLAN.md) alongside this handoff. The user requested a native Ashen Roots editor organized into workflow tabs, shared typed import with automatic category routing, and a visual tagged/searchable asset picker. Character/Ability/Composer/Projectile-AOE authoring belongs to M5; world/NPC tools to M6; VFX/audio to M7; save inspection to M8; full authoring qualification to M9. Item and broader Texture/Material painting workflows are proposals with design still open. The update captures scope only; these new workflows are not implemented. Continue M4/M5 with their authority and acceptance gates intact; exact editor layouts need a reviewable blueprint.

Next required work:
1. Read full DAE-007/008/009/010/011 contracts before extending gameplay. Implement native abilities/attributes/effects/tags, atomic prepare/cost/cooldown and per-source ownership behind the World facade. Extend existing WorldSession/shared protocol for action intents, snapshots, acceptance/rejection and late join. No local-host bypass or parallel network runtime.
2. Bind the real52-tick attack clip and native .daaction to Action Composer arbitration/clock, root requests before motor, achieved-transform socket sweeps after physics, bounded crossed hit intervals, cancellation/death and deduplicated presentation. Predicted ghosts never damage. Verify costs/cooldowns/effects/tags and accepted/rejected replay before claiming one ability complete.
3. Native graph assets/cook/EditorDocument authoring, markers/transitions/additive/masks/slots, posture/jump/dodge/stagger/death and visual motion qualification; native RmlUi health UI.
4. Precise terrain/hut collision through native .dacollision authoring/cook; preserve current transforms/pivots. Do NOT parse raw glTF POSITION as Y-up geometry: glTF stores transformed nodes; native RuntimeModel already flattens them correctly.
5. Remaining M4 automatic clock-offset/jitter/input lead/redundancy, RTT0/50/150/250 and loss0/2/5percent matrix, coupled players, cell/lifecycle barriers and measurement gates. Required gates remain open. M3 live EOS still blocked on private deployment/policy inputs and two identities. M6 must wait for required M4/M5 gates.

Focused commands:
- python scripts/prepare_royal_scene.py: reconcile native package for all owned clips/static/human sources; no original conversion.
- python scripts/verify_character_editor.py: scripted120-tick D3D12/Vulkan native viewport and editor regression.
- python scripts/verify_royal_renderer.py: GPU-versus-CPU rest/attack reference and native editor regression.
- python scripts/verify_character_scene.py: coordinator focused checks plus native integration46/46. Repeat full integration only after meaningful changes.
- python scripts/verify_directional_graph.py: owned locomotion/source hashes, graph/clip references and native integration.
- Build editor with the existing MSVC helper: python -c "import sys,subprocess; sys.path.insert(0,'scripts'); from msvc_environment import activate; sys.exit(subprocess.call(['.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe','--build','build/m5-editor-relwithdebinfo','--target','DarkAngelEditor','--parallel','2'],env=activate()))"

Important source notes:
- engine/runtime/character_scene.cpp composes existing WorldSession; static/upright/unscaled/live-human/standing initial profile. Topology replacement is a fresh current baseline after included input, never a rewind of a live world.
- apps/editor/character_preview.cpp builds the explicit initial graph/policy; nine cooked UUIDs are supplied through --character-clips, not read from source assets at runtime. Automatic proxies are a preview fixture policy, not a general physics authoring solution.
- Successful native resource reload stops/reinitializes character Play; failed candidates preserve active state/resources. Original editor path remains when character-clips is omitted.
- M5_REPORT.md has legacy mixed text encoding; preserve bytes with ASCII append or targeted byte replacement.
- AS coordinator permits one WIC owner; no dependency upgrades, no push, originals read-only. No subagents authorized.

Usage handoff: five-hour96percent used, weekly52percent. No reset credit consumed. User has not confirmed using the available reset; do not consume it automatically. Check fresh usage before a large increment. No implementation/build/test process remains running. Source working tree was clean at116c10f before this documentation-only handoff. Verify HEAD/status first; there is no remote.
