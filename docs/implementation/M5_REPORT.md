# M5 animation and combat

Updated 8 October 2026. **In progress.** M5 began after the local M4 checkpoint `ed81b83` and its initial handoff gates. This increment verifies the canonical-human rig/skin/Ozz foundation; it does not verify the full animation, combat or runtime UI milestone.

| First increment | Result |
|---|---|
| Frozen Ozz build | Revision 744eb9d99f606eda849acb0b1204f7a3dc20bca1 checked by CMake; static runtime and offline builders use /MD. Samples, upstream converters/tests and vendor FBX SDK are disabled. No pin upgrade |
| Canonical human | Actual Blink HumanMale_Character FBX has 81 bones. Native [canonical source](../../content/animation/canonical_human.daskeleton) records named joints, preceding/depth-first parent order, normalized metre/Y-up rest transforms and validated weapon/head/foot socket keys |
| External source protection | Original FBX SHA-256 is 0c99d39d08d123a1114c6cf2e19d3e089f14501bea887250fea6748374d2122f before/after conversion and final verification. Only the local ignored fixture is exported. No original FBX or Unity asset changed |
| Native source/cook | Embedded skeleton UUID and owned runtime product UUID; source sidecars only for external glTF. Existing AssetService catalog, SHA-256 CAS and typed registry package flow. Human root captures a frozen coherent skeleton/skin/archive closure, so failed reimport cannot redirect an old generation to a newer incompatible rig |
| glTF skin validation | cgltf validates one skin, exact named hierarchy and canonical rest transforms, inverse bind pose, four bounded influences and explicit skin-index to cooked Ozz-order remap. The actual Body_Legs fixture has 1,420 exported vertices. Matching names with changed rest pose is rejected |
| Ozz runtime | SDK-private archive decode and CPU local-to-model rest pose, independent instance buffers. A CPU skinning reference checks the cooked inverse-bind palette against the real mesh within 0.5 mm. Repeated archive/cook/index rebuilds preserve IDs and byte-identical manifests; oversized derived skin work rejects before publication and retains the prior coherent generation |
| Authoring | Owned skeleton source edits use existing EditorDocument prepare/commit, native workspace history/origin, undo/redo, dirty fingerprint and journaled save/Open. Invalid socket references reject before commit. No parallel editor state/history |
| Regression | Integrated optimized **31/31** native tests; separate GNS processes still agree on 90 commands/x=7.125. The build without animation/Ozz also passes the three affected asset/editor/collision checks |

Reproduce with `python scripts/verify_m5.py`. [Receipt](evidence/m5-initial.json) records exact source/binary hashes, original input and conversion hashes, base M4 commit, commands and timings. [Rig test](evidence/m5-rig.log), [CTest](evidence/m5-ctest.log), [conversion](evidence/m5-human-conversion.log) and [build without animation regression](evidence/m5-no-animation-regression.log) are current. Existing M4 receipts remain historical checkpoint evidence, rather than being overwritten by the M5 build.

The importer follows the [Khronos glTF skin contract](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html): skinned-node transforms cancel out, and joint transforms plus inverse bind matrices determine the result. The initial fixture requires baked bind-space metre positions. Nonuniform/mirrored joint scale, morph targets, more than four influences, additional skins, animation channels and required glTF extensions are rejected explicitly. Material/texture rendering is not implemented by this binding increment. The fixture is a representative original body part, not a claim that the full modular human is rendered.

The subsequent clip foundation cooks real Blink idle, run, one-handed attack and forward-dodge FBXs, converted read-only with [Blender bpy](../../scripts/convert_animation_fixture.py) into local normalized GLBs. The animation FBXs have 85 joints and a different authored tpose basis from the 81-joint character. Explicit offline `--normalize-rest` bakes same-key local pose deltas onto canonical rest transforms, omits 12 auxiliary joints and fills eight missing brow/eyelid joints with canonical rest. Required body/socket hierarchy differences reject. Source rest-length differences reach 77.6 mm; this is a content conversion, not evidence of visually faithful final animation. All adjustments and original/derived SHA-256 values are in [conversion and acceptance receipt](evidence/m5-clips.json). Runtime has no mapping asset or retargeting.

The existing AssetService now supports typed clip adoption, sidecar-owned UUIDs, four-product frozen clip/rig/archive closure, warm cook and failed-reimport preservation. Import settings participate in the recipe, including loop policy. Initial interchange is deliberately bounded: one GLB animation, complete canonical translation/rotation channels, unit-scale exact rest, LINEAR keys normalized on the 60 Hz grid, at most 600 ticks/50,000 joint keys, no skin/morph/required extension or external buffer. STEP and CUBICSPLINE reject before publication. JSON work is checked before cgltf structure allocation. Ozz AnimationBuilder produces compressed immutable archives, with per-RigPose sampling contexts and buffers, coherent signature checks and live generation retention.

Translation/yaw are stripped from poses and stored as independent cumulative fixed-tick root tracks. Forward root sampling supports fractional tick phases and composes translation with unwrapped yaw across loops; one request is limited to eight ticks. Fixed-tick reference checks measure all authored sockets and half-metre weapon-tip probes against normalized source transforms:

| Clip | Ticks | Ozz bytes | Maximum probe error | Root track extent |
|---|---:|---:|---:|---:|
| Idle | 104 | 155,217 | 0.450 mm | 1.3 mm |
| Run | 40 | 61,655 | 0.326 mm | 111.8 mm |
| Attack | 52 | 79,210 | 0.470 mm | 120.0 mm |
| Dodge | 92 | 137,680 | 0.455 mm | 965.5 mm |

These are compression measurements against the offline normalized clips, not visual quality or combat/motor acceptance. Tests also cover independent contexts, exact loop boundaries, rotating loop accumulation, incompatible signatures, changed loop settings, STEP/CUBICSPLINE rejection and a failed canonical reimport retaining the previous frozen closure. Reproduce with `python scripts/verify_clips.py --convert`; [focused results](evidence/m5-clips-focused.log) and [integrated suite](evidence/m5-clips-ctest.log) are separate from first-increment receipts. Integrated **33/33**, independent GNS movement and build-without-animation **3/3** pass. Ozz optimization, walk content, root-driven action integration, visual/GPU skinning and action/combat gates remain open.

Next required M5 work, still unverified:

- Complete walk content, motion-quality visual acceptance and offline optimization/error budgets. The four-clip foundation above does not close all clip/content gates.
- Animation Graph nodes, synchronized locomotion, masked/additive/action layers and optional IK; CPU pose and Diligent GPU skinning. Animation state and required gameplay sockets must advance at ticks even with rendering/culling disabled.
- Action Composer fixed-resolution clocks, stable block/event identities, crossed hit intervals, loop root accumulation, cancellation/combo/hitstop, preview scrubbing and achieved-motor feedback. Native motion requests must remain collision-constrained and replayable.
- Native abilities/effects/tags/attributes: atomic costs/cooldowns/action preparation, owner tokens/reservations, per-source Burn, slow, dodge immunity, stagger/death, equipment modifiers and cleanup. Follow DAE-008's simultaneous, duplicate, deferred, suppression, cleanse, source-destruction and MaxHealth gates.
- Extend the same WorldSession protocol for action/ability correction, authoritative hit windows/projectiles, acceptance/inclusion/rejection receipts, dependent combo rejection and deduplicated presentation. Predicted ghosts must never apply authoritative damage. Verify host/client attack, dodge, stagger, damage/death and current-state late join.
- Native asset/cook and EditorDocument transactions for all supported graph/action/combat authoring, schema/capability discovery and preview controls. Native RmlUi health UI and coherent health view models; no ImGui substitution for game UI.
- Complete detailed DAE-007/008/009/010/011/013 gates, fault traces, 30/60/144/headless combat equality, nonhuman rig validation and measured CPU/allocation/clip/skinning costs. Neither this increment nor compilation establishes those results.

M4's broader collision/query/dynamic/streaming qualifications remain in M4_REPORT.md. M3 live EOS remains blocked on private deployment/policy and two identities; Loopback/GNS development is explicitly authorized. The MCP pin still uses tested 2025-11-25 compatibility. No milestone with remaining required gates is marked Verified.

M4 static cave geometry qualification now passes 36/36 integration, independent GNS and 4/4 without animation. It supplies immutable historical geometry and authored query material keys for future action sweeps. M5 clip/graph/combat gates remain independent. M6 is authorized only after the required M4/M5 gates.
