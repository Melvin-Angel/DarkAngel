# M6 report

M6 is In progress. Nothing here is Verified in the milestone sense until M6's own gates pass (cell retirement and crossing, no duplicate persistent spawns, navigation invalidation, an agent-authored Luau node without a native rebuild). M6 runs in parallel with the remaining M5 content by user decision of 11 October 2026; see [CLAUDE_AUTONOMOUS_PROMPT.md](CLAUDE_AUTONOMOUS_PROMPT.md).

## Increment 1: native NPC definition (11 October 2026)

Track B item 1, first slice: the source type, its frozen product and its authoring. Placement in the scene and Play come next.

### What exists

- Source type `.danpc` (`kind: "npc"`), a full native source like Player and Mask: catalog identity, drafts, Undo/Redo, private validation, coordinated Save, frozen product (`npc.json`) with a pinned dependency closure.
- Shape: the Player's shared Character reference and the same Actor Loadout structure (`kit`, optional `starting_attributes`, optional four `masks` with `active_mask`), plus `name` (display, 1 to 64 characters) and `faction` (lowercase identifier, 1 to 32 characters). An NPC has no input profile and no cameras; the cooker rejects them. No behaviour is authored on the definition.

```json
{"schema": 1, "kind": "npc", "asset": "...", "name": "Royal Guard", "faction": "royal_guard",
 "character": "<.dacharacter id>", "loadout": {"kit": "<.dakit id>", "starting_attributes": [{"attribute": 2, "value": 60}]},
 "sources": {"character": "characters/royal_human.dacharacter", "kit": "royal_district/combat/player.dakit"}}
```

- Runtime types in `actor_assets.hpp`: `NpcDefinition`, `CookedNpc`, `load_cooked_npc`. Loading resolves the Character, the effective kit (the active mask's kit when masks are equipped, under the same schema/input/rig compatibility rule as a Player), and the starting attributes against the kit's attribute schema. Products are kind-checked: a Player cannot be loaded as an NPC or the reverse.
- The cooker reuses the Player path (`import_actor`, `validate`, `Frozen::load`) with an NPC flag instead of a parallel implementation, so loadout rules cannot drift between the two actor kinds.
- Authoring: `NativeAuthoring::create_npc`; the shared loadout commands (`assign_player_kit`, `equip_mask`, `select_mask`) accept an NPC.
- Editor: a dedicated NPC tab (`Workspace::Npc`, `--npc-workspace`) with its own selection, an NPC-only list, and creation with a name and faction. Its form shows the NPC section (display name, faction), a Behaviour section, then the shared Character and loadout forms (Character, kit, masks, starting attributes). The Player tab lists Players and Masks only and has no NPC controls; input-profile and camera sections never appear for an NPC.
- Owned content: `content/npcs/royal_guard.danpc`, the Royal Guard. It uses the Player's Character (`royal_human`) and the Royal kit with its own starting attributes (MaxHealth and Health 60).

### Decisions made here

- Name `.danpc`, product kind `npc`, importer `native-npc-v1`, following the existing conventions.
- Faction is a free lowercase identifier for now. There is no faction registry or relationship table yet; that arrives with targeting rules for NPCs (controller seam, item 2) and should be decided then rather than guessed now.
- The staged acceptance in the handoff asks for an NPC with the Player's Character but a different kit and attributes. Attributes differ in owned content today; a different kit is proven in the test fixture (the NPC resolves its own active mask's kit while the Player on the same Character resolves another). An owned second kit is content work for the placement slice.
- User decision (11 October 2026, given during this increment): the NPC has its own tab. The Player tab is only for Player-related things; the NPC tab is where an NPC is configured and where its behaviour will be set. The first version of this increment listed NPCs inside the Player tab and was changed before it was pushed. The Behaviour section is a stated placeholder today: nothing is authored there yet.
- Masks are still listed and edited in the Player tab; an NPC equips them through the pickers in its own form. Whether masks deserve their own place is open.

### Verified

- `NativeAuthoringTests` (the whole binary passes): NPC creation as a pending source, name/faction/starting-attribute edit with Undo/Redo, the shared mask command equipping an NPC, four invalid candidates rejected by private validation without publication (bad faction, empty name, an input profile, Health beyond the schema), coordinated Save, a frozen NPC that shares the Player's Character generation while resolving its own starting attributes (Health 60 against the Player's 100) and its own active mask's kit, kind-checked loading in both directions, failed Save keeping the published source and frozen product, and the owned Royal Guard cooked beside the owned Player.
- Editor on D3D12, `--npc-workspace --authoring-asset <royal guard>`: capture `evidence/npc-form-d3d12.png` inspected. It shows the NPC tab selected, the NPC-only list, creation with a Faction field, and the Royal Guard form with its NPC, Behaviour and Character and loadout sections.
- Full `scripts/verify_effect_reaction.py` (27 gates) and CTest (only the five known unrelated failures) passed on the build before the NPC tab was split out of the Player tab. After that split only the editor target was rebuilt and only the NPC form capture was re-run, at the user's direction to test less often; the receipt in `evidence/effect-reaction.json` therefore predates the tab change. One later full run stopped at `mask-worn-d3d12` with "PNG capture failed" (a file write error); that gate passed when re-run alone.

### Not done, not verified

- The NPC is not placed in a scene and nothing in Play uses it yet. The combat target still takes the scene kit and attributes directly, and the scripted attacker is still hard-coded in `CharacterSceneSession`.
- No NPC presentation in Play (mask worn on the NPC, name plate).
- No behaviour, sensing, navigation or faction rules.
- Physical pointer interaction with the form; the capture is a scripted editor run. "Create NPC" in the tab is exercised through the same authoring command in the test, not by clicking.

## Increments 2 and 3: NPC on the combat target and the controller seam (11 October 2026)

Built as one chunk and verified once, per the user's direction on testing.

### What exists

- **NPC on the combat target.** `CharacterSceneCombat::npc` (`CharacterSceneNpc`: kit, input semantics, abilities, resolved starting attributes, name, faction, generation). When set, the target's ability state uses the NPC's attributes and is equipped with the NPC's own kit; its public observer and effect views use the same schema. Its abilities must be in the prepared scene unions, which the session checks.
- **Play preparation.** `load_character_preview` takes an optional target NPC: it loads the frozen NPC, requires its Character skin to match the scene's placed skin and its kit to share the Play kit's attribute schema and tag registry, adds its abilities, effects and clips to the prepared unions, and the target object starts from the NPC's Health. The NPC generation is part of the session fingerprint.
- **Editor.** The NPC tab has "Place on the combat target for next fresh Play" (and its removal). The choice is editor state (`Controller::authoring_npc`, CLI `--target-npc <id>`), included in the Play roots. The HUD target panel shows the NPC's display name.
- **Controller seam.** `actor_controller.hpp`: `ActorController::decide(ControllerView) -> ControllerCommand`. The view is a bounded copy of authoritative state (tick, own motor state, alive, busy, the opponent's motor state and alive). The command can ask for exactly what a player intent carries: a movement input, a facing, and a press of one kit slot. The session bounds-checks the command and runs it through the ordinary motor step and the server's checked ability request path. A press of a slot the actor's kit does not assign is refused as `Unassigned`; nothing a controller returns can bypass costs, cooldowns, requirements or hit resolution.
- **The scripted attacker is now a controller.** `make_scripted_attacker` implements the old schedule behind the seam; `CharacterSceneCombat::attacker` remains as shorthand for it. The hard-coded `drive_attacker` is gone. No decision logic was added.

### Decisions made here

- Placement is an editor/Play setting, not scene data. Binding an NPC to a scene object belongs with spawn points and encounter volumes (later M6 scope), where it needs a scene schema decision.
- The controller sees one opponent (the player). Multiple actors, perception and factions come with sensing in item 4.
- A facing requested together with a press applies only if the press is accepted, which keeps the scripted attacker's behaviour bit-for-bit (it only turned when its attack started).

### Verified (one pass for the chunk)

- `CharacterCombatSceneTests`: an NPC target with its own kit (no Heavy) and attributes starts at Health 60 in authoritative and public state, takes three Light hits to 35, 10, 0; the scripted controller on the NPC's kit hits the player once (75) with no prediction disturbance; a scripted attack on a slot the NPC kit does not assign is rejected at construction; a test controller walks the NPC from 6 m to about 1.07 m using move and face requests and then attacks through the ordinary ability path, while its presses of the unassigned Heavy slot are refused by authority; owner prediction stays exact with no resynchronisation; an out-of-bounds controller command is rejected. All earlier scripted-attacker cases in this binary still pass unchanged.
- `HudTests`: target name shown, generic caption when empty.
- Fresh editor Play on D3D12 with `--target-npc <royal guard> --target-attacks light`: `Editor attacked tick=40 player_health=75 reaction_tick=14 ... hud_health=75/100 hud_target=60/60 hud_target_name=Royal_Guard`; capture `evidence/npc-target-d3d12.png` inspected (HUD shows "Royal Guard 60 / 60", the player is staggered at 75).
- `python scripts/verify_effect_reaction.py`: 28 gates pass on this build. CTest: only the five known unrelated failures.

### Not done, not verified

- The owned Royal Guard still uses the Royal player kit; a distinct owned NPC kit is content work. The different-kit case is proven with a test kit.
- An NPC with equipped masks as the target: the cooker and loader support it, Play preparation does not wear the mask model on the target or switch its loadouts.
- The "Place on the combat target" button is exercised through the CLI path, not by clicking.
- Controllers are native C++ only; nothing is authored. No navigation: the test controller walks in a straight line on flat ground.
- Host/client over GNS (deferred with the rest of that qualification). The controller is server-side only by construction.

## Increment 4: navigation (11 October 2026)

Track B item 3, built in one chunk with crosshair aim and verified once.

### Dependency

`recastnavigation 1.6.0` was not installed in the build tree. Following the standing rule, a dry run with the manifest step's arguments (`x64-windows-darkangel`, the overlay ports, the build tree's `vcpkg_installed`, the recorded buildtrees root, features `m1;m2;m3;m4;m5-ui` plus a new `m6-nav`) listed every existing package as already installed and exactly one package to build. Only then was it installed (14 seconds; the log shows that one build). `vcpkg.json` gained the small `m6-nav` feature, and the `m5-editor-relwithdebinfo` preset and the build directory's cached feature list name it so the records agree with the tree. `VCPKG_MANIFEST_INSTALL=OFF` is still set. CMake looks for the package quietly and builds navigation only where it is installed.

### What exists

- `navigation.hpp` / `navigation.cpp`, library `DarkAngelNavigation`: `NavigationMesh` builds one Recast/Detour mesh from a `CollisionDefinition`'s static boxes and triangle meshes (moving, dynamic and sensor boxes are left out) with agent settings that fit the standing motor, and answers `path(from, to)` with the corner points of a complete path, or nothing when an end is off the mesh or no complete path exists.
- `make_path_follower`: a controller behind the seam that asks the mesh for a path to its opponent every 15 ticks, walks it corner by corner with ordinary move and face requests, and in reach presses its attack slot no more often than the attack interval. Movement and collision stay with the motor; the path is advice.
- Editor: `--target-navigates` builds the mesh from the collision the Play session will actually simulate and gives the target this controller.

### Verified

- `NavigationTests` (CTest `M6.navigation`): on a floor with a wall across the middle the path goes around the wall end (5 corners, 11.1 m against a 6 m straight line) and no corner lies inside the wall; an unobstructed path is the straight line; points off the mesh give no path; a scene without static collision, and moving collision, are rejected. In a session, a straight-line controller stays behind the wall (control) while the path follower walks around it (widest x 4.4 m), arrives at tick 119 and lands one authoritative Light hit at tick 138, with owner prediction undisturbed.
- Fresh editor Play on D3D12 with the Royal Guard as target and `--target-navigates`: `Editor attacked tick=60 player_health=75 ... nav_polygons=52 hud_health=75/100 hud_target=60/60 hud_target_name=Royal_Guard`. The mesh is built from the Royal scene's cooked mesh collision (52 polygons).
- `python scripts/verify_effect_reaction.py`: all gates pass on this build (31, including `navigation`, `ranged-d3d12` and `npc-navigates-d3d12`). CTest: only the five known unrelated failures.

### Not done, not verified

- The mesh is baked at Play preparation, not cooked as an asset product, and has no generation identity. The M6 gate "unloaded nav invalidates queries" needs a cooked, per-cell product; this is one static mesh for the whole scene.
- In the Royal scene the target starts next to the player, so the editor run proves the mesh builds from real scene collision and the controller attacks, not a long walk. The walk around an obstacle is proven in the test scene.
- No dynamic obstacles, no off-mesh links, no crowd avoidance, no vertical/multi-level check beyond what Recast gives by default.
- One agent profile; settings are not authored.

## Increment 5: behaviour trees (11 October 2026)

Track B item 4, built as one chunk and verified once.

### Dependency

`behaviortree-cpp 4.9.1` was not installed. The dry run with the manifest step's arguments listed every existing package (now including Recast) as already installed and four packages to build: `behaviortree-cpp` and its dependencies `tinyxml2`, `minitrace` and `flatbuffers`, all at their pinned versions, nothing rebuilt or removed. They were then installed (the log shows exactly those four). `vcpkg.json` gained the `m6-bt` feature; the preset and the cached feature list name it; `VCPKG_MANIFEST_INSTALL=OFF` is unchanged. Licence notices for all four were already under `third_party/notices`.

### What exists

- **Behaviour source** `.dabehavior` (`kind: "behavior"`): a full native source (catalog, drafts, Undo/Redo, private validation, coordinated Save, frozen `behavior.json`). It holds a display name and a JSON tree over a closed native node set:
  - composites `sequence`, `selector` (one to eight children);
  - conditions `opponent_alive`, `in_range {range}`, `idle`;
  - actions `face`, `approach {range}`, `attack {slot}`, `recover {ticks}`.
  Shape is bounded (depth 8, 64 nodes) and validated by one decoder shared by source validation, the cooker and the runtime (`behavior.hpp`), which has no BT.CPP dependency.
- **NPC reference**: an NPC definition can reference one behaviour (`behavior` plus its source locator); it joins the NPC's frozen closure, and `CookedNpc::behavior` carries the decoded tree.
- **Executor adapter** (`behavior_tree.hpp`, library `DarkAngelBehavior`): `make_behavior_controller` turns a decoded tree into a BT.CPP tree and runs it as an `ActorController`. The blackboard is native (the controller view in, the controller command out); leaves are native nodes. The tree is ticked once per authoritative tick. `approach` follows the navigation mesh when one is supplied and walks straight otherwise. Leaves can only fill the controller command, so a tree cannot bypass ability validation: `attack` is a press request authority may refuse.
- **First tree**, owned as `content/npcs/guard_melee.dabehavior` and referenced by the Royal Guard: if the opponent is alive, in reach and the NPC is idle, face it, attack with Light and recover for 90 ticks; otherwise approach to 1.4 m.
- **Editor, NPC tab**: the Behaviour section has a behaviour picker, "Edit behaviour", "Clear behaviour" and "Create behaviour" (seeded with the first tree and assigned). Behaviours are listed in the NPC tab beside NPCs. Opening one shows its name and an indented outline of the tree with its parameters editable (ranges, recover ticks, attack slot).
- **Play**: a placed NPC with a behaviour runs it. The navigation mesh is built from the session's collision for its approach leaf.

### Decisions made here

- The tree is authored as JSON and converted to BT.CPP's own format in memory; no BT.CPP XML is stored or authored. The handoff asks for JSON topology.
- A placed NPC's behaviour takes precedence over the training options (`--target-attacks`, `--target-navigates`). Those remain for a target without a behaviour.
- The node set is deliberately small and closed. Luau leaves and scorers are later M6 scope.

### Verified (one pass for the chunk)

- `BehaviorTreeTests` (CTest `M6.behavior_tree`): the default tree decodes; seven invalid trees are rejected (unknown node, empty composite, zero range, non-canonical slot, zero recover ticks, unknown field, depth beyond the bound). Against scripted views the controller asks to walk toward a distant opponent, stops moving in range, faces and presses Light when idle, does not press again during recovery or while its action runs, attacks again after recovering, and does nothing against a dead opponent. The owned Royal Guard cooks with its frozen behaviour, and behaviour products are kind-checked. In a session with a wall between them, the tree-driven Royal Guard walks around the wall, hits the player at tick 139, recovers and hits again at tick 230 (Health 50), keeps its own Health 60, and leaves owner prediction undisturbed.
- `NativeAuthoringTests`: behaviour creation, assignment to an NPC with Undo/Redo, clearing, rejection of a non-behaviour asset, an invalid tree rejected by private validation, coordinated Save and the frozen NPC carrying its tree.
- Fresh editor Play on D3D12 with the Royal Guard placed and no training option: `Editor attacked tick=60 player_health=75 ... nav_polygons=52 behavior=Guard_Melee ... hud_target=60/60 hud_target_name=Royal_Guard`.
- NPC tab capture `evidence/npc-form-d3d12.png` inspected: the Behaviour section shows the assigned `guard_melee.dabehavior` with its edit, clear and create controls, and the list shows a Behaviours group.
- `python scripts/verify_effect_reaction.py`: all gates pass (33). CTest: only the five known unrelated failures.

### Not done, not verified

- The behaviour form itself (the tree outline with parameter fields) was not captured or looked at; only the NPC form was. Tree structure (adding, removing, reordering nodes) cannot be edited in the UI yet, only parameters.
- Because the owned Royal Guard now has a behaviour, the editor gates `npc-target-d3d12` and `npc-navigates-d3d12` also ran the tree (their logs show `behavior=Guard_Melee`). The scripted attacker and the plain path follower are still covered by `CharacterCombatSceneTests` and `NavigationTests`, but no longer by an editor run with an NPC.
- No sensing beyond the one opponent in the controller view, no blackboard visualisation, no decision inspection in the Game tab.
- No Luau leaves; a new node kind still needs a native rebuild, which is what the M6 gate's "new Luau node without a native rebuild" is about.

## Increment 6: NPC workflow in the NPC tab (11 October 2026)

One chunk, verified once.

### What exists

- **Tree structure editing.** The behaviour form in the NPC tab shows the tree as an indented outline. Every node except the root has Up, Down and Remove (disabled where they do not apply: first, last, only child); every composite has an "Add child..." list of the nine node kinds, disabled at eight children. New nodes start with valid defaults. Each change is an ordinary draft edit, so Undo/Redo, private validation and coordinated Save apply, and the form shows the decoder's message when a tree is invalid.
- **Decision readout.** `ActorController::status()` gives a short description of the latest decision; `CharacterSceneSession::controller_status()` exposes it. The Game tab's Gameplay Inspector shows "Target controller" with the behaviour's name and the current decision (for a tree: Idle, Acting, Approach, Approach along the navigation mesh, Attack, Recover with ticks left, Defeated; the scripted attacker and the path follower report their own states). Presentation only.

### Verified

- `BehaviorTreeTests`: the readout names the approach, the attack and the recovery at the ticks where the controller makes those decisions, and a session reports a non-empty status.
- Fresh editor Play on D3D12 with the Royal Guard placed: `Editor attacked tick=60 player_health=75 ... behavior=Guard_Melee decision=Recover_(31_ticks_left) ...`.
- Behaviour form capture `evidence/behavior-form-d3d12.png` inspected: "Guard Melee" with its selector, two sequences, parameters and the Up/Down/Remove and Add child controls, with the first and last items' buttons disabled as intended.
- `python scripts/verify_effect_reaction.py`: all gates pass (34). CTest: only the five known unrelated failures.

### Not verified

- Follow-up, same day: the structure edits now go through an authoring command (`NativeAuthoring::edit_behavior_tree`, one history entry per edit) and `NativeAuthoringTests` covers reorder, add, remove, Undo/Redo and six rejected edits (moving or removing the root, moving past either end, adding to a leaf, an unknown node kind, a path that does not exist). The buttons call that command. What remains unverified is only the click itself; after this follow-up the editor and `NativeAuthoringTests` were rebuilt and that test re-run, not the full gate set.
- The Gameplay Inspector readout was checked through the exercise marker, not by looking at the inspector panel.

## Increment 7: NPC tuning (11 October 2026)

One chunk, verified once.

### What exists

- An NPC definition can carry optional `tuning`: `preferred_range` (metres, up to 60) and `recovery_scale` (0.25 to 4). Defaults are 1.4 m and 1.
- A tree's `in_range` and `approach` nodes may set `"range": "preferred"` instead of a number; they then use the NPC's preferred range. Every `recover` node's ticks are multiplied by the NPC's recovery scale. One tree therefore serves differently tuned NPCs.
- NPC tab: "Tune this NPC" with the two fields in the Behaviour section. Behaviour form: each range node switches between a fixed distance and "NPC preferred range".
- Owned content: Guard Melee uses the preferred range for both of its range nodes; the Royal Guard carries explicit tuning (1.4 m, scale 1), so its behaviour in Play is unchanged.

### Verified

- `BehaviorTreeTests`: "preferred" decodes and an unknown keyword is rejected; with the same tree, an NPC at 2.5 m approaches under the default range and attacks at once with a 3 m preferred range; recovery scale 0.5 halves the 90 tick recovery (next attack 45 to 49 ticks later); out-of-bounds tuning is rejected; the owned Royal Guard freezes with its tuning.
- `NativeAuthoringTests`: tuning edit with Undo/Redo, out-of-bounds tuning rejected by private validation, frozen NPC carries the tuned values.
- Editor: NPC tab capture `evidence/npc-form-d3d12.png` inspected (Tune this NPC, Preferred range 1.400, Recovery scale 1.000). Fresh Play with the placed Royal Guard still lands its hit and reports its decision.
- `python scripts/verify_effect_reaction.py`: all gates pass (34). CTest: only the five known unrelated failures.

### Not verified

- The behaviour form's "Use NPC preferred" / "Use fixed" switch was not captured after this change; the earlier behaviour-form capture predates it.
- Tuning beyond these two numbers (strafe, retreat, aggression curves from the vision) does not exist.

## Increment 8: navigation bake tool (11 October 2026)

User direction: navmesh baking gets a window under Tools to trigger it, to be expanded later.

### What exists

- **Tools > Navigation** (`ToolView::Navigation`, CLI `--navigation-tool`): the agent and voxel settings (cell size and height, agent height and radius, step height, walkable slope) with "Use defaults", a "Bake navigation mesh" button, "Clear bake", and the result: polygons, source triangles, bake time, how many Play sessions used the bake, and whether it is current, stale (the scene collision changed) or behind the settings.
- **Play reuses the bake.** A Play session whose collision matches the baked collision uses the baked mesh; otherwise it builds one on demand with the tool's current settings, as before. The match is by collision content (mesh identities and signatures), so reloading the same scene keeps the bake and changing collision invalidates it.

### Verified

- Editor on D3D12, `--exercise-navigation-bake`: `Navigation bake polygons=52 source_triangles=1320 play_reuses_bake=1 on_demand_for_other_collision=1`; capture `evidence/navigation-tool-d3d12.png` inspected (settings, Bake and Clear, 52 polygons, 1320 source triangles, 13.2 ms, "Current: the next Play session uses this bake").
- This was an editor-only change. Only the editor was rebuilt and only this gate was run; the last full pass (34 gates, CTest with the five known failures) was on the previous commit.

### Not done, not verified

- The bake lives in the editor session. It is not saved, not a cooked asset product and has no generation identity, so it is redone after a restart. Making it a cooked product is the step that connects to the M6 cell/invalidation gate.
- No visualisation of the mesh in the viewport.
- The Bake button is exercised through the function it calls, not by clicking.
- One agent profile.

### Next step (track B)

Waiting on the user's answers (asked 11 October 2026) about spawn points, several NPCs in one Play session, and whether to start cells and the ledger now. Recommended order if unanswered: spawn points placed in the Scene tab, then several NPCs per session, then cells.

## Combat sandbox stage 1: multi-entity combat and NPC spawn points (11 October 2026)

Stage 1 of [COMBAT_SANDBOX_PLAN.md](COMBAT_SANDBOX_PLAN.md), delivered as one chunk and verified once.

### The three assumptions checked first

- **Scene placement to SpawnPlan.** `engine/runtime/assembly.cpp` reads each scene entity's optional `model` field into `SpawnPlan::models`. A `spawn` field now rides the same path into `SpawnPlan::spawns` (object identity to NPC asset). It is serialized only when present, so plans without spawn points keep their fingerprint.
- **StableId.** A 128-bit value (two 64-bit halves). A placed object's identity is the first 32 hex digits of the SHA-256 of its placement path. `spawned_actor(spawn point)` hashes `spawn/<point>/actor` the same way, so no reserved namespace is needed and the same spawn point gives the same actor identity on every Play and on client and server.
- **Scene viewport preview.** The viewport drew only `plan.models`. It now also draws the player's skin at every spawn point while authoring and at every spawned actor in Play. A marker was not needed.

### Runtime

- `CharacterSceneCombat::actors` (at most 16; that needed the session capacity lifted, which was done the same day, see "Session capacity and no friendly fire" in TARGETING_REPORT.md) replaces `target`, `target_attributes`, `npc`, `attacker` and the single `controller`. A `CharacterSceneActor` carries its scene identity, optional kit, input semantics, abilities, resolved attributes, name, faction, generation and optional controller. A training dummy is an actor with no controller. The player keeps its own role because it alone is predicted.
- `CharacterSceneSession` keeps one record per actor (motor, ability owner, controller receipt, chosen target, observed pose) in ascending network identity and steps them in that order. The order the caller lists actors in does not change the simulation.
- `ControllerView` lists every other actor (identity, faction, motor state, alive) instead of one opponent. `nearest_hostile` stands in for target selection until sensing exists; different factions are hostile and the player's faction is `player`. `ControllerCommand::target` records the actor a decision is aimed at; the session checks it names an actor in the view and exposes it as `controller_target`.
- Soft-lock candidates are every live actor hostile to the player. `soft_lock()` returns the actor it chose.
- Accessors take an identity: `actors()`, `controller_receipt(id)`, `controller_status(id)`, `controller_target(id)`; `pose`, `observer`, `reaction`, `death` and `effects` already did.
- A scripted attacker pressing a slot its kit does not assign is now refused by authority at the press (`Unassigned`) instead of being rejected when the session is built, because a controller is opaque to the session.

### Spawn points and Play

- A spawn point is a scene object with a transform and `"spawn": {"npc": "<id>"}`. `respawn` accepts only `"never"` and `encounter` only null; both are reserved.
- Scene tab: the World Outliner marks spawn points and has "Add NPC spawn point"; the Inspector of a spawn point picks its NPC and removes it. These are structural edits of the scene source through the document, so they share Undo and Save.
- Play preparation loads every spawn point's NPC, unions abilities, effects and clips, and builds one actor per spawn point with the derived identity and the NPC's own Health. The spawn point object itself is not sent to the session. Spawn point identities and NPC and behaviour generations are part of the session fingerprint. A spawned NPC with a behaviour runs its tree on one navigation mesh shared by the session; one without stays passive.
- All actors must share the player's skin and rig for now.
- HUD target is a separate presentation choice: the soft-locked actor, else the nearest live hostile in front of the camera, and it stays shown until another qualifies. Explicit lock-on is not built.
- The Gameplay Inspector's second tab is now "Actors": Health, faction, behaviour, decision, chosen target, last request, reaction and effects per actor.

### Removed

`--combat-target`, `--target-npc`, `--target-attacks`, `--target-navigates`, `Controller::authoring_npc`, the "Place on the combat target" button, the "Target attacks the player" training option, `CharacterSceneAttacker` and `CharacterSceneNpc`. `make_scripted_attacker` and `make_path_follower` remain as controllers used by tests.

### Content

- `content/royal_district/RoyalCombat.dascene`: two Royal Guard spawn points, 6 to 7 m from the player.
- `content/royal_district/RoyalTraining.dascene` (new): the same layout with one passive Training Dummy where the training target stood.
- `content/npcs/training_dummy.danpc` (new): the Royal Character and kit, default attributes, no behaviour.
- `scripts/prepare_royal_scene.py` writes both scenes with spawn points. `scripts/open_royal_scene.ps1` opens the combat scene, or the training scene with `-Training`.

### Verified (build of 11 October 2026)

- CharacterCombatSceneTests: every earlier two-actor case through a one-actor list, plus two guards on their own schedules with one defeated, nearest-of-two soft lock, an allied actor never soft-locked, list-order independence, three guards closing in, and bounds (missing object, duplicate, no attributes, the player as an actor, more actors than the limit, no actors).
- NativeAuthoringTests on the training scene with the spawned dummy; NavigationTests, BehaviorTreeTests, ObservedCharacterPoseTests, ReactionPresentationTests, RoyalHeavySceneTests.
- `python scripts/verify_effect_reaction.py`: 35 gates pass. New or changed: `attacked-flinch`, `attacked-death`, `attacked-light` and `npc-target` now use a Royal Guard spawned in a duel scene derived in the fixture; `npc-spawn-points-d3d12` plays the owned combat scene (2 actors, both guards reach and hit the player, player Health 25 at tick 260); `spawn-points-scene-d3d12` captures the Scene tab. Both captures were looked at.
- CTest: only the five known unrelated failures (M5.actor_assets, M5.attribute_authoring, M5.composer_structure, M5.composer_preview, M4.royal_mesh_collision).

### Not verified, and known limits

- (Fixed the same day: no friendly fire, see TARGETING_REPORT.md.) In this build hits did not check faction. In the two-guard capture one guard is at 35/60 without player input: a guard's swing hit the other guard. Whether NPCs of one faction can hurt each other is a product decision.
- The Scene tab buttons (add, retarget, remove a spawn point) were not clicked by a gate; only loading, drawing and Play of authored spawn points were exercised.
- The 26 older `scripts/verify_*.py` were switched to the training scene and lost `--combat-target` mechanically; none was re-run. Those that package their own registry (for example `scripts/verify_royal_combat.py`) will need the Training Dummy NPC in it.
- A startup registry that lacks a spawn point's NPC (for example `.cache/royal-scene/registry.json` used by `open_royal_scene.ps1`) loads without that actor and logs a warning; Play with a project mount packages it. This path was not run.
- RoyalCollisionTests was updated for the new signature but still does not link (known).
- No respawn, no encounter ownership, one rig per scene, no GNS run.
