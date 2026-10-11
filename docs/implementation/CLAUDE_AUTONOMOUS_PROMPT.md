Autonomous DarkAngel continuation for Claude Code. Written 11 October 2026 at the user's request: keep pushing the project toward M6 across usage windows, in fresh sessions, without waiting for input unless a real product decision is needed.

## Before doing anything

1. Check usage with the `mcp__ccd_session_mgmt__get_usage` tool (load it with ToolSearch `select:mcp__ccd_session_mgmt__get_usage`). If the 5-hour window is 88% used or more, or the weekly window is 92% used or more, stop immediately with a one-line note; the next scheduled run will continue. Re-check after each committed increment and stop cleanly (committed, pushed, CONTINUE header updated) once the 5-hour window reaches about 90% used.
2. Check nothing else is building or running the editor: `tasklist | grep -i -E "ninja|cl.exe|DarkAngelEditor|NativeAuthoringTests|vcpkg"`. If something is, another session is working; stop with a one-line note.
3. In `C:\DarkAngel`: `git status`, `git log --oneline -8`. Work on `master`; pushing to `origin master` is authorized. Never stage or touch the untracked user paths `content/animation/clips/`, `content/characters/new_character.dacharacter`, `content/royal_district/RoyalCombat.dascene.records/`. If `content/players/royal_player.daplayer` shows an uncommitted `cameras` edit, it is the user's; leave it uncommitted.
4. Read `docs/implementation/CODEX_CONTINUE_PROMPT.md` (rules, contracts, M5 remaining list), then the top entries of `docs/implementation/CONTINUE.md`, and `CAMERA_REPORT.md`, `MASK_REPORT.md`, `EFFECT_REACTION_REPORT.md` as needed for the item you pick. Trace the code you will change before editing; the sources are dense single-line C++.

## Build rules (the user was burned twice)

- `VCPKG_MANIFEST_INSTALL=OFF` is set in `build/m5-editor-relwithdebinfo/CMakeCache.txt`. Keep it. Build only with `scripts/msvc_environment.py` `activate()` and `.tools/cmake/cmake-4.4.4-windows-x86_64/bin/cmake.exe --build build/m5-editor-relwithdebinfo --target <targets>`. Never delete `build/`, `vcpkg_installed/` or vcpkg buildtrees. Never run two builds or editors at once.
- The user asked for fewer rebuild/test cycles. Batch edits; build only the targets you touched; run the focused test once per increment; run the full rebuild plus CTest once before a push that changes shared runtime, wire or prediction code. Expected CTest baseline: all pass except five known unrelated failures (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`).
- Changing a header under `engine/runtime/include` rebuilds almost everything (about ten minutes). Plan header changes once.
- Dependencies: RmlUi 6.2 and FreeType were installed into the build tree on 11 October 2026 (manifest feature `m5-ui`; see HUD_REPORT.md). For any further missing pinned package the same rule holds: the user approved installing missing pinned packages only if no existing package is removed or rebuilt. Do a dry run first (`vcpkg install --dry-run` with the same arguments the manifest step uses, against the existing installed directory) and proceed only if the plan lists additions alone. If it would rebuild or remove anything, do not proceed; pick another item and record why.

## What to work on (in order; skip an item only for a recorded blocker)

Update 11 October 2026: items 1, 2 and 3 below are done (SCRIPTED_ATTACKER_REPORT.md, STAGGER_REPORT.md, HUD_REPORT.md). Start from the CONTINUE.md header.

M4/M5 are In progress. M6 starts only when the original M5 gate passes: attack, dodge, stagger, damage and death under host/client authority; acceptance/rejection and replay producing one presentation; no damage from predicted ghosts; late join restoring current state; runtime Health UI.

User direction, 11 October 2026 (supersedes the earlier order): items 1-3 of the first list (scripted attacker, stagger as gameplay, runtime Health HUD) are done. Host/client qualification over GNS is postponed for now; do not build the two-session harness yet. A "multiplayer Play" option in the Game tab (Play launches two instances that connect) belongs to a later milestone together with a bootstrapper and a host/join flow; record it, do not build it.

1. Player targeting module. This is the foundation for ranged and projectile abilities and comes first. Three modes, one module, authored per ability:
   - Soft lock for melee: when a melee ability starts, pick the best target near the player (range and facing cone toward movement/camera intent) and warp the attack motion toward it (turn to face, close or hold distance) during an authored window of the action. No hard lock-on. The motor still owns movement; express the warp as facing plus root request through the existing motor request path, on the authoritative, predicted and replayed ticks alike, as dodge facing and dash already do.
   - Crosshair aim for ranged mode: while the aim camera is live, a screen-centre crosshair defines an aim ray from the camera; the ability's target or direction comes from that ray. Camera presentation never validates hits: the client sends aim intent, the server validates against its own state.
   - AOE aim: same crosshair, but the aim point snaps to the ground under the crosshair (ray against collision, clamped to an authored maximum range) with a ground marker.
   Design it before coding: where targeting mode and its parameters live on the Ability asset, what the intent carries on the wire (target identity and/or aim direction/point, bounded and validated), how the server re-validates, how prediction and replay reproduce the same warp, and what the HUD shows (crosshair, soft-lock indicator, ground marker). Write the design at the top of a TARGETING_REPORT.md first, then implement in vertical slices: soft lock, then crosshair aim, then ground-snapped AOE aim. The scripted attacker and the training target are the test subjects.
2. Ranged and projectile abilities built on the targeting module (authoritative spawn and hit, effects through the existing bindings), using existing converted clips (bow-shot, casting, punch).
3. Mask depth: distinct kits per element using existing converted clips, per-mask death timeline and locomotion graph following the active mask.
4. Block/parry outcomes, combos, lock-on camera using the look target.
5. Deferred until the user says otherwise: host/client qualification over GNS; multiplayer Play in the Game tab.

### M6 track (user decision 11 October 2026: begin M6 in parallel)

User decision (11 October 2026): M5 is accepted on single-process (Loopback) evidence so that M6 can begin in parallel. This is not a claim that the original M5 gate passed: host/client qualification over GNS for dodge, mask switching, the scripted attacker, stagger locks and death, and late join with a real second client, are deferred, as are the remaining M5 content items (targeting module, ranged/projectile, mask depth, block/parry, combos). M4's open network/fault/performance matrices are unchanged and still open. Live EOS remains externally blocked.

Alternate increments between the M5 list above (track A, targeting module first) and this M6 list (track B). Finish and push one increment before starting the next; do not interleave half-done work from both tracks. Read the M6 row and the "M6 World state and NPCs" extension row in docs/architecture/DarkAngel_Implementation_Handoff.md and the NPC / AI Designer section of the vision before the first M6 increment. Record every M6 increment in an M6_REPORT.md and keep STATUS honest: M6 is In progress, nothing in it is Verified until its own gates pass.

1. Native NPC definition (`.danpc` or the name the existing conventions suggest): references a shared Character and the same Actor Loadout structure as Player (kit, starting attributes, optional masks), plus faction. Full native source type like Mask (catalog, drafts, Undo/Redo, validation, Save, frozen product). Staged acceptance from the handoff: an NPC using the same Character as the Player but a different kit/attributes, both running through the same native ability execution. Place it in the Royal scene as the combat target so the scripted training attacker becomes that NPC's first, temporary controller. NPC workspace/tab in the editor using the shared forms.
2. NPC controller seam: replace the hard-coded scripted attacker with a small server-side controller interface that requests the same approved operations a player intent does (move, face, activate slot). No decision logic beyond what the scripted attacker already does yet.
3. Navigation: Recast/Detour adapter and a cooked navmesh for the Royal scene collision, a path query, and an NPC that walks to the player. `recastnavigation` and `behaviortree-cpp` are in vcpkg.json; check whether they are installed in the build tree and apply the same dry-run rule as for RmlUi (additions only, nothing rebuilt or removed) before installing.
4. BT.CPP executor adapter with JSON tree topology and a first tree (acquire target, approach to preferred range, attack, recover), blackboard and sensing kept native; decisions never bypass ability validation.
5. Later M6 scope, not to start without finishing the above: regions/cells and the world ledger, splines/scatter, encounter volumes and spawn points, Luau leaves.

Do not import the Unity YAML behaviour-tree runtime. The Unity docs remain a read-only reference for behaviour intent only.

Animation: use the clips already converted under `content/animation/blink/` and `content/royal_district/clips/`. Do not build a retargeting pipeline now; the user will decide that later.

## How to work

Small coherent vertical increments: native source edit, Undo/Redo, private validation, coordinated Save, complete scene closure, fresh resources, fresh isolated WorldSession Play, one D3D12 capture you actually look at. Extend `tests/native_authoring_tests.cpp` / focused tests and `scripts/verify_effect_reaction.py` as earlier increments did. Presentation never decides gameplay; no local-host authority bypass; failed Save keeps prior source/catalog/playable resources.

For each finished increment: write or extend a report under `docs/implementation/`, prepend a current-state paragraph to `STATUS.md` and `CONTINUE.md`, commit explicit owned files with a clear message ending in the `Co-Authored-By: Claude` trailer the session provides, and push `master`. Report honestly what was not verified. If you are blocked on a genuine product decision, write the question at the top of `CONTINUE.md`, pick the next item, and keep going.

If context gets heavy, finish the current increment, commit, push and stop; the next scheduled run starts fresh from these files.
