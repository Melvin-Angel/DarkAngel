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
- New dependencies: RmlUi 6.2 and FreeType are pinned and downloaded but not installed in the build tree. The user approved installing missing pinned packages only if no existing package is removed or rebuilt. Do a dry run first (`vcpkg install --dry-run` with the same arguments the manifest step uses, against the existing installed directory) and proceed only if the plan lists additions alone. If it would rebuild or remove anything, do not proceed; pick another item and record why.

## What to work on (in order; skip an item only for a recorded blocker)

M4/M5 are In progress. M6 starts only when the original M5 gate passes: attack, dodge, stagger, damage and death under host/client authority; acceptance/rejection and replay producing one presentation; no damage from predicted ghosts; late join restoring current state; runtime Health UI.

1. Something that attacks the player: a minimal scripted attacker on the existing combat target (it uses the same kit/ability/WorldSession path; this is shared target behaviour, not the M6 NPC controller or AI). It unlocks verifying player-side flinch, death, dodge invulnerability against a real hit, and the owner pose reaction path in Play.
2. Stagger as gameplay: an effect-driven movement restriction through the motor's existing lock request, a stun loop presentation with the converted Blink `stunned` clip, and the owned Royal flinch bound by default once an attacker exists.
3. Runtime Health HUD through RmlUi with native view models (needs the dependency step above). If the dependency cannot be installed safely, record it and continue with other items.
4. Mask depth: distinct kits per element using existing converted clips, per-mask death timeline and locomotion graph following the active mask, switch feedback.
5. Ranged/projectile abilities (aim camera already exists), block/parry outcomes, combos, lock-on camera using the look target.
6. Host/client qualification over GNS for dodge, switching and reactions.

Animation: use the clips already converted under `content/animation/blink/` and `content/royal_district/clips/`. Do not build a retargeting pipeline now; the user will decide that later.

## How to work

Small coherent vertical increments: native source edit, Undo/Redo, private validation, coordinated Save, complete scene closure, fresh resources, fresh isolated WorldSession Play, one D3D12 capture you actually look at. Extend `tests/native_authoring_tests.cpp` / focused tests and `scripts/verify_effect_reaction.py` as earlier increments did. Presentation never decides gameplay; no local-host authority bypass; failed Save keeps prior source/catalog/playable resources.

For each finished increment: write or extend a report under `docs/implementation/`, prepend a current-state paragraph to `STATUS.md` and `CONTINUE.md`, commit explicit owned files with a clear message ending in the `Co-Authored-By: Claude` trailer the session provides, and push `master`. Report honestly what was not verified. If you are blocked on a genuine product decision, write the question at the top of `CONTINUE.md`, pick the next item, and keep going.

If context gets heavy, finish the current increment, commit, push and stop; the next scheduled run starts fresh from these files.
