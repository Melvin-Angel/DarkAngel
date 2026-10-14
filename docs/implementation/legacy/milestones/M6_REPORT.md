# M6 report

M6 is In progress. Nothing here is Verified in the milestone sense until M6's own gates pass (cell retirement and crossing, no duplicate persistent spawns, navigation invalidation, an agent-authored Luau node without a native rebuild). M6 runs in parallel with the remaining M5 content by user decision of 11 October 2026; see [CLAUDE_AUTONOMOUS_PROMPT.md](../prompts/CLAUDE_AUTONOMOUS_PROMPT.md).

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

Stage 1 of [COMBAT_SANDBOX_PLAN.md](../plans/COMBAT_SANDBOX_PLAN.md), delivered as one chunk and verified once.

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

## Level design: Place panel and deleting scene objects (11 October 2026)

User request of 11 October 2026: place and delete game objects in the Scene tab without hunting through a project browser. Editor-only.

### What exists

- **Place panel**, docked under the scene viewport. Its items come from the asset catalogue and sort themselves into categories: "NPCs" for every NPC definition, and one "Environment: <folder>" category per folder of imported static models (a model counts when its import sidecar names the static model importer). A search box and category chips narrow the list. There is no hand-kept list: a new NPC or imported model appears on its own.
- **Placing.** Drag a tile into the viewport: the drop position is turned into a ray through the scene camera and the object lands where that ray meets the ground plane (the "Ground height" field, 0.5 m by default, which is the Royal ground). Double-click a tile to place it beside the selected object. An NPC becomes a spawn point; a model becomes a model object. A model that is not in the loaded resources is cooked and packaged with the scene first, and the placement is undone if that fails.
- **Deleting.** The Delete key in the viewport or the World Outliner, "Delete" in an outliner row's right-click menu, and "Delete object" in the Inspector remove the selected object. Objects that belong to a mounted assembly are refused with a message.
- Placing and deleting are single edits of the scene's own source through the document, so the Inspector's Undo and Redo cover them and Save writes them.
- Categories for encounters, altars and items do not exist yet because those asset kinds do not exist; `Shell::refresh_place_items` is the one place a new placeable kind is added.

### Verified

- `place-panel-d3d12` (`--exercise-place`): the catalogue lists 58 items in 4 categories on the fixture; an NPC and a model are placed through the panel's own commands at a requested ground position; the model is deleted, the delete is undone and redone; deleting an object that is not in the scene is refused. Capture looked at: the panel shows its categories and tiles under the viewport, and the placed spawn point is in the outliner and the viewport.
- Only this gate was run. The full gate script (which now includes it) and CTest were last run on the stage 3 build.

### Not verified

- The mouse paths: an actual drag from a tile into the viewport, the double-click, the Delete key and the right-click menu were not driven by a run. The gate calls the same `place` and `remove_object` commands those paths call. The drop-position ray was therefore not exercised either.
- Placing a model that is not already in the loaded resources (the cook-and-package path).
- Objects are placed on a flat plane at the ground height, not on the collision under the cursor.

## Level design: placing on the collision under the cursor (11 October 2026)

User request of 11 October 2026. Editor-only change; it needed only its own gate, and the full gate script and CTest were not re-run for it.

- A viewport drop from the Place panel casts the ray through the cursor against the scene's cooked static collision and puts the object at the first point it meets (position and height). Where the ray meets no collision it falls back to the ground plane at the Place panel's ground height, as before.
- Placements that give only a ground position (double-click, "Add NPC spawn point", the exercise) land on the collision below that position (`Shell::surface_height`), or at the ground height where there is none.
- The editor answers these rays from a query-only `PhysicsWorld` loaded with the scene's cooked collision (`Shell::place_surface`, set in main.cpp), rebuilt when the collision changes. Runtime code is unchanged.
- Limits: the surface is the cooked collision the session was started with. Models placed since the last cook are not part of it, so one newly placed model cannot be stacked on another until the scene is prepared again. The object's origin goes on the surface; nothing aligns it to the surface normal.

**Verified:** `place-panel-d3d12` passes with the new marker `on_collision=1 surface_y=0.5`: the placed spawn point's height equals the collision under it, and a slanted ray like a viewport drop lands at the same height. The capture was looked at: the placed NPC and hut stand on the ground.

**Not verified:** a real mouse drag (the gate calls the placement command and the surface query, not the drop target); landing on a raised surface such as a roof or a slope (the training scene's ground is flat at 0.5 m where the gate places).

## Level design: a gate that drives the real mouse paths (11 October 2026)

User request of 11 October 2026. Editor-only; it needed only its own gates, and the full gate script and CTest were not re-run for it.

- The hidden editor can now be driven with real input. `Shell::probes` records the screen centre of a few named controls as they are drawn (each Place item, each outliner row, the outliner menu's Delete entry, the viewport drop point, the Mask Editor's Display name field), and an exercise queues ordinary ImGui mouse, key and text events at those points, one step a frame. Nothing calls the commands behind the controls.
- `place-mouse-d3d12` (`--exercise-place-mouse`): press on an NPC item in the Place panel, drag into the viewport and release (a spawn point appears at 4.94, 0.5, 6.06, on the collision under the cursor, and is selected); double-click the item (a second spawn point appears beside the selection); click the second object's outliner row and press Delete (it is removed, the first stays); right-click the first object's row and click Delete in the menu (it is removed). Object counts are checked after each step.
- `mask-editor-field-d3d12` (`--exercise-mask-field`): with the Mask Editor open over the Player tab, click into Display name and type one character; the mask draft's name gains it, the edit is an undoable draft edit, and Undo restores the name.
- Found while building it: an active text field keeps its own copy of the text and writes it back, so an Undo issued while the field still has focus is typed over on the next frame. The exercise releases focus first. In the editor this means Undo (button or shortcut) while a text field is still active may appear not to work for that field; not fixed here.

**Verified:** both gates pass. The `place-mouse-d3d12` capture was looked at: it shows the end state (both placed objects removed again, five objects in the outliner), which matches the markers but cannot show the drag itself.

**Not verified:** the Delete key with the Viewport window focused (the gate presses it with the outliner focused); dragging a model rather than an NPC; the remaining Mask Editor fields (the pickers and numbers); OS-level mouse input (events are queued into ImGui, not sent through Windows).

**Question for the user:** which new placeable kinds come first: encounters, altars or items? None of those asset types exists yet, so each needs a definition before it can be placed.

## Scene viewport selection and File > Save All (11 October 2026)

User requests of 11 October 2026. Editor-only; each has its own gate, and the full gate script and CTest were not re-run.

- **Selecting in the viewport.** A left click in the Scene viewport selects the object under the cursor (`Shell::pick_object`, set in main.cpp): the nearest object whose bounds the ray through the cursor meets. A model uses its cooked bounding box under the object's transform; a character or an NPC spawn point uses a standing body's box. A click on the gizmo, or on nothing, keeps the selection. Not active during Play.
- Limits: bounds are boxes, not meshes, so a click beside a round hut but inside its box selects the hut, and a click on open ground selects the ground object. There is no box selection, multi-selection or click-through to the next object behind.
- **File > Save All.** Saves every gameplay draft in every workspace through the coordinated Save (the same path as "Save gameplay changes"), then the scene. Disabled during Play. If the gameplay Save fails validation the scene is not saved either, and the error goes to the console.

**Verified:** `place-mouse-d3d12` now ends with a real click on a spawn point's body in the viewport and requires it to become the selection (`viewport_select=1`). `save-all-d3d12` makes a mask draft edit, opens File and clicks Save All with the mouse, and requires the edit to be in the source file with nothing left dirty.

**Not verified:** selecting a model (the gate clicks a spawn point); a scene with unsaved changes in both the scene and gameplay at once; no capture was looked at for these two (the result is a selection and a file, not a picture).

## Assets: cooking what is in the project (11 October 2026)

User note of 11 October 2026: "assets imported into the project should get cooked. This should happen periodically in the project or once during import." Editor-only; it needed only its own gate.

- **What was already true.** Import through the editor (File > Import asset, the Assets tab's Import) validates a full cook before it publishes and cooks on commit (`AssetService::prepare_import`, `commit_import`), so an imported asset has a cook generation at once.
- **What was missing.** A source that reaches the project folder any other way (copied in, created by a tool, pulled from version control, or left over from another cache) stayed "Not cooked here" until a Play preparation happened to need it.
- **Built.** `Controller::cook_pending`: sources the cache has not cooked are cooked in the background, one every half second while a visible editor is not in Play. A source that fails is reported once in the console and tried again only after its file changes. The Assets tab shows how many sources are still not cooked and how many of them failed (hover for the errors), and has a "Cook all now" button. Placement: the Assets tab, because it is the asset list's own state.
- Hidden gate runs do not sweep, so fixtures stay as they are.
- Limits: the sweep cooks sources that were never cooked in this cache. It does not watch for a cooked source whose file changed on disk outside the editor; the asset service's scan decides what a changed source means, and that was not touched. Cooking happens on the main thread, so a large model can stall a frame.

**Verified:** `cook-sweep-d3d12`: a Character source written straight into the project folder is listed with no cook generation, the sweep cooks it, and a second sweep neither cooks anything again nor retries a failure. The capture was looked at: the Assets tab shows "1 source(s) not cooked yet, 1 of them failed" for a fixture source that cannot cook, with "Cook all now" disabled because nothing else is waiting.

**Not verified:** the half-second cadence in a visible editor (the gate calls the sweep directly); "Cook all now" by click; a large model.

## Luau behaviour leaves: design (12 October 2026)

Written before any code. Goal from the M6 row: an agent (or a designer) authors a new behaviour node without a native rebuild.

### What it is

One new leaf kind in the behaviour tree's closed set: `script`. Its Luau source is inline in the `.dabehavior` source:

```json
{"type": "script", "name": "Keep distance", "source": "return function(view, state) ... end"}
```

Inline, not a separate asset, for this first step: the behaviour's source is already the unit of drafts, Undo, validation, Save, cooking and the generation hash, so a script leaf gets all of that with no new asset type and no new dependency edge. A shared script asset referenced from several trees can come later without changing the leaf's contract. Bounds: name 1 to 48 characters, source at most 4,096 bytes, at most 8 script leaves in a tree.

### Contract

The chunk returns one function, called once on each tick the tree reaches the leaf:

- `view` (a fresh read-only table each call): `tick`, `alive`, `busy`, `preferred_range`, `self = {x, y, z, yaw}`, `opponent` (nil, or `{x, y, z, distance, visible}` for the nearest live hostile the NPC detects, the same one the native leaves use) and `lost` (nil, or `{x, y, z}` for the last known position of the hostile it lost most recently).
- `state`: a table that belongs to this leaf of this NPC and persists between ticks.
- It returns a status, `"success"`, `"failure"` or `"running"`, and optionally a command table: `move_x`, `move_z` (world space, length at most 1), `face` (yaw in radians), `press` (a slot name such as `"light"`), `say` (text for the decision readout).

The leaf writes the command into the same blackboard the native leaves write, so a script can only ask for move, face and a slot press: the controller seam, ability validation, costs and cooldowns are unchanged. It cannot name a target (the tree's opponent stays the target), read another actor that the NPC does not perceive, or reach the world, the session or any other script.

### Sandbox and faults

- One Luau VM per controller (one NPC), created with the standard sandbox (`luaL_sandbox`), without `os` and with a fixed random seed so a run is repeatable. Memory limit 1 MB per VM; a call that runs longer than 2 ms is interrupted.
- Any fault (compile error at runtime, error raised, time or memory limit, a return that is not a known status, a command out of bounds) makes the leaf fail and disables that leaf for that NPC; the readout shows the script's name and the message. The tree goes on with its other branches. A script fault never throws into the session.
- Decoding compiles every script leaf, so a syntax error is refused by validation and Save, like any other invalid tree. Runtime faults cannot be found at Save and are handled as above.

### Server-only

Scripts run inside the controller, which the session runs on authority only. Nothing about a script is replicated.

### Editor

Placement: the existing behaviour form (NPC tab), because a script leaf is a node of the tree. "Add child" gains "Script (Luau)"; the node shows its name and a multi-line source box. No separate script editor now.

### Tests

BehaviorTreeTests: decode and rejection (syntax error, size, unknown field), a script leaf deciding through the controller (move, face, press, running and state kept between ticks), fault isolation (error, endless loop, bad return: the leaf fails, the tree's next branch acts, later ticks keep working), and a session scene where an NPC whose tree has a script leaf keeps its distance from the player through authority.

### Not in this step

No owned NPC is switched to a script leaf (the owned trees stay native so the existing gates keep their meaning). No coroutines or waiting inside a script (return `"running"` and keep state instead). No shared script assets.

## Luau behaviour leaves: built (12 October 2026)

Built as designed above. Where things are:

- `engine/runtime/behavior.cpp`: the `script` node kind (name, source), its bounds, `compile_behavior_script` (decoding compiles every script leaf, so validation and Save refuse source that does not compile) and `behavior_slot`.
- `engine/runtime/behavior_tree.cpp`: `ScriptHost`, one sandboxed Luau VM per controller, and the `DaScript` leaf. The VM is made only for a tree that has a script leaf.
- Editor: "Script (Luau)" in the behaviour form's "Add child", with a name field and a source box on the node (NPC tab, behaviour form). Adding one inserts a small working script.

One difference from the design: scripts cannot write globals at all (the sandbox makes them read-only), so a script keeps everything in locals and in its `state` table. The design did not say; the tests fix it.

### Verified

BehaviorTreeTests: decoding and rejection (does not compile, empty or oversized source, empty name, unknown field, nine script leaves); a script deciding move, face and a slot press through the controller seam; state kept between ticks and separate per NPC; the tuning value in the view; no `os`; ten kinds of fault (error, endless loop, wrong return type, unknown status, move longer than 1, unknown slot, chunk that returns no function, global write, write to the view, memory limit), each failing the leaf alone with the script's name and reason in the readout while the tree's next branch decides on that tick and the next; a session scene where an NPC driven by a script leaf backs away from the player from 2 m to 4.4 m through authority with owner prediction undisturbed.

### Not verified

- The editor's script node (name field, source box) was not looked at in a capture and has no gate: no fixture behaviour has a script leaf. It compiles and uses the same draft path as the other node fields.
- No owned NPC uses a script leaf, so no Play gate runs one.
- The 2 ms time budget is wall-clock: a script near the limit can be interrupted on a loaded machine and is then disabled for that NPC until the next Play.

## Navigation mesh overlay: design (12 October 2026)

Written before any code. Goal: see the baked navigation mesh over the scene, to judge where NPCs can walk.

- **Runtime.** `NavigationMesh::outline()` returns the mesh's polygon edges as pairs of points, each edge once, with a flag for edges on the mesh's border (no neighbour). Read-only; no query state.
- **Editor.** Placement: a checkbox "Show in the viewports" in Tools > Navigation, next to the bake, because the overlay shows that bake (the user's earlier decision put baking there). It draws through the existing viewport overlay that already draws senses and encounter volumes: inner edges thin, border edges brighter, lifted a few centimetres off the surface. It shows the current bake; without a bake the checkbox says so and nothing is drawn.
- **Gate.** `navigation-tool-d3d12` bakes already; it also turns the overlay on and requires that edges were drawn in the Scene viewport. Editor-only change apart from the read-only accessor.
- **Not in this step.** No filled polygons, no path preview, no per-area colours, no off-mesh links (the mesh has none).

## Navigation mesh overlay: built (12 October 2026)

Built as designed. `NavigationMesh::outline()` in engine/runtime/navigation.cpp; the checkbox "Show in the viewports" in Tools > Navigation; drawing in the viewport overlay of apps/editor/main.cpp (Scene and Game viewports). The outline is taken once at bake time.

**Verified:** gate `navigation-overlay-d3d12` (`--exercise-navigation-overlay`) on the training scene: 52 polygons, 154 edges of which 104 on the border, all drawn. The capture was looked at: a bright border runs round the walkable ground and round each of the two huts, with thin inner edges between polygons across the ground. NavigationTests passes. This is an editor-only change apart from the read-only accessor, so only its own gate was run, standalone after CTest with the verify script's common arguments; the full verify script and CTest were not re-run for it.

**Not verified:** the checkbox itself was not clicked by a gate (the exercise sets the same flag); the Game viewport was not captured with the overlay; a stale bake still draws (the tool already says when a bake is stale).

## Interaction foundation: design (12 October 2026)

Written before any code. Goal from the M6 row: "interaction/world-event foundations". The world-event half exists (events, tags, trigger volumes, encounters). This adds the other half: the player does something to a placed object on purpose, and the world reacts.

### What an interactable is

A scene object with an `interactable` component, the same shape of thing as a trigger volume:

```json
"interactable": {"prompt": "Open the gate", "event": "gate.opened", "radius": 2.0, "once": true, "requires_tag": "gate.unlocked"}
```

- `prompt`: what the HUD offers (1 to 48 characters). `event`: the world event a successful use emits. `radius`: how near the player's position must be (0.5 to 10 m, default 2). `once`: used up after the first success (default true). `requires_tag` (optional): a world tag that must be held, so a lever can be locked until an encounter completes.
- It only emits a world event. What happens next is whatever listens: an encounter starts on it today; doors, pickups and altars will be listeners or richer interactable kinds later. Keeping the first slice to "emit an event" means no new gameplay state has to be replicated.
- Definition, instance, entity: the component is the definition on the scene instance; the session holds a `WorldInteractable` per instance (used-up state). There is no separate asset type yet.

### Authority (the user's rule: a client triggers world events through the server)

- The client sends an interaction request naming the object (new wire kind, reliable, client to server). It carries no position and no claim of success.
- The server checks that the sender owns a character, and the scene session resolves the request in its world step from its own state: the object exists, the player is alive, the player's authoritative position is within the radius, the interactable is not used up, the required tag is held. Then it emits the event. Every request gets a receipt (accepted, unknown object, out of reach, unavailable, dead), sent reliably to that client.
- With several players each request is resolved for the player that sent it; nothing here assumes one player.
- NPCs do not interact (the user's rule that NPCs emit no world events).
- World tags stay server state, so a client cannot know that a tagged interactable is locked: it offers the prompt and the receipt says "unavailable". That is deliberate; it leaks nothing and the HUD can say why.

### What the client shows

`CharacterSceneSession::interaction_prompt()`: the nearest interactable within its radius of the owner's predicted position that this client has not seen used up (from receipts). Presentation only. The editor's Play view puts it on the HUD and sends the request on the existing `interact` input action.

### Slices

1. Runtime: `interaction.hpp/.cpp` (definition, decoding, `WorldInteractable`), the request and receipt on the wire, the scene session (request, resolution in the world step, prompt), EncounterTests-style unit tests and a scene test: a lever starts an encounter; out of reach, used up and locked are refused; a forged request for an unknown object is refused.
2. Editor: the scene component in the spawn plan, Play preparation, the Place panel's Gameplay category, Inspector fields, the HUD prompt, the Gameplay Inspector's World tab, and a Play gate.

### Not in this step

Hold-to-interact, interaction animations, inventory and keys as items, doors that move collision, NPC interaction, persistence of used-up state across sessions (that is the world ledger, deferred).

## Interaction foundation: built (12 October 2026)

Both slices of the design above are built.

### Where things are

- `engine/runtime/interaction.hpp/.cpp`: `InteractableDefinition`, `decode_interactable`, `WorldInteractable::use` (the authority's answer), `InteractionResult`.
- `engine/runtime/world_session.cpp`: wire kinds `Interact` (client to server) and `InteractReceipt` (server to client), both reliable. The server accepts a request only from the peer that owns the named character, holds at most sixteen unresolved requests and refuses the rest with a receipt.
- `engine/runtime/character_scene.cpp`: `CharacterSceneInput::interact` sends the request; the world step resolves every request for the player that sent it from authoritative position and state and answers it; `interaction_prompt()`, `last_interaction()`, `interactable_used()`.
- Scene: the `interactable` component on a scene object (it may also be a placed model, so a lever mesh can be the interactable), carried in the spawn plan and so in the session fingerprint. Plans without interactables serialize exactly as before.
- Editor. Placement and why: "Interactable" in the Place panel's Gameplay category, beside encounters and trigger volumes, because it is the same kind of placed gameplay object; its fields in the Inspector (prompt, world event, radius, only once, required world tag); its reach drawn as a ring with a marker box in the viewports and clickable there; the Gameplay Inspector's World tab lists every interactable's use count and the latest request's answer. In Play the HUD shows the prompt of the nearest interactable in reach and the `interact` input action (F, gamepad north) sends the request.

### Differences from the design

None in behaviour. The prompt is offered from the owner's predicted position, the server decides from the authoritative one; in loopback they agree.

### Verified

- EncounterTests: decoding and nine rejections; out of reach, dead player, single use, the required tag, repeatable use.
- CharacterCombatSceneTests: a lever starts the encounter that listens for its event; a request from out of reach, for an unknown object, for a used-up lever and for a locked vault is refused with the right answer and changes nothing; the used lever is no longer offered and the next interactable is; owner prediction undisturbed.
- Gate `interaction-d3d12` (new, 49 gates now): in Editor Play on a scene derived from the training scene, the player walks to a lever, the prompt is offered, the scripted F press sends one request, the server accepts it and the encounter starts. Its capture was looked at: it is taken after the lever was pulled and shows the encounter's Royal Guards in the scene, one as the HUD target, and `interact: Pressed / Released / Tapped` in the Input Monitor.

### Not verified, and limits

- The HUD prompt text itself was not seen in a capture: the gate's capture is taken after the single-use lever is used up, when no prompt is offered. The gate requires that the prompt was offered before the press; how it looks on screen is unchecked. The reach ring in the Scene viewport was not captured either.
- Loopback only. With real latency the prompt can be offered a moment before the server would accept; the receipt then says "out of reach".
- A locked interactable still shows its prompt (tags are server state); the HUD does not yet show the refusal, only the World tab does.
- Placing an interactable through the Place panel and editing it in the Inspector have no gate of their own; the gate's scene is written as source. They use the same commands as the trigger volume.
- Used-up state is per session; persistence is the deferred world ledger.

## Splines and scatter: design (12 October 2026)

Written before any code. The decision log makes splines first-class (roads, rivers, paths, walls, fences) and scatter built in. This is the first slice of both: a spline you can place and shape, and one kind of scatter, models along a spline (a fence line, a row of posts, trees along a road).

### Spline

- A scene object with a `spline` component (`{"closed": false}`) and, as with an encounter and its volumes, separate point objects that name it: `"spline_point": {"spline": "<the spline's local identity>", "index": 2}`. Points are ordinary scene objects, so the existing selection, gizmo, Undo and Save shape a spline; nothing new is needed to move a point. Points are ordered by index, then identity.
- Runtime: `Spline` (engine/runtime/spline.cpp), a uniform Catmull-Rom curve through the points, open or closed, with an arc-length table: `length()`, `sample(distance)` (position and tangent), `polyline()` for drawing. Two points give a straight line. Pure and deterministic.
- Bounds: 16 splines a scene, 32 points a spline.

### Scatter

- An optional `scatter` object in the spline component: `{"model": "<model asset>", "spacing": 2.0, "jitter": 0.0, "seed": 1, "align": true}`. One instance of the model every `spacing` metres of arc length, pushed sideways by up to `jitter` metres, facing along the curve (`align`) or turned at random. Every random number is a hash of the seed and the instance's index, so the same source gives the same instances on every machine.
- Definition, instance, entity: the component is the definition; instances are derived from it and the points' current places. They are not scene objects and not replicated objects: a session holds at most 128 replicated objects, and a fence must not spend them. Both ends derive the same instances from the same scene. Each has a stable derived identity (`scatter_instance(spline, index)`).
- Bounds: spacing 0.25 to 50 m, jitter 0 to 5 m, at most 256 instances a spline (the rest are dropped and the editor says so).

### Editor

Placement: "Spline" and "Spline point" in the Place panel's Gameplay category (a spline starts with two points; a point is added to the spline of the selected spline or point), the spline's fields in the Inspector (closed, scatter on/off, model, spacing, jitter, seed, align), the curve and its points drawn in the viewports like the other gameplay objects, and the scattered models drawn with the scene's models. Why there: a spline is a placed level-design object like an encounter, and the user asked for no new tabs without need.

### Not in this step

Scatter instances have no collision and are not in the navigation mesh (static collision is a cooked asset; baking scatter into it is its own step). No meshes generated along a spline (roads, walls). No area scatter, biomes or painting. Nothing in gameplay reads a spline yet (patrol paths are the obvious first reader). A model used only by a scatter is prepared by the editor on demand; it is not yet part of the scene's packaged dependency closure.

## Splines and scatter: built (12 October 2026)

Built as designed.

### Where things are

- `engine/runtime/spline.hpp/.cpp`: `SplineDefinition`, `ScatterDefinition`, `decode_spline`, `Spline` (length, sample, polyline), `scatter_along`.
- `engine/runtime/assembly.cpp`: the `spline` and `spline_point` components in the spawn plan (`splines`, `spline_points`), `scatter_instance`. Plans without splines serialize exactly as before.
- Editor: "Spline" and "Spline point" in the Place panel's Gameplay category; the Inspector's Spline section (closed, scatter on/off, model, spacing, jitter, seed, face along the curve) and a Spline point line; the curve and its points drawn in the viewports and clickable; scattered models drawn with the scene's models, rebuilt every frame from the points as they stand, so dragging a point moves the curve and its models.

### Verified

- EncounterTests: a two-point spline is a straight line by arc length; a curve passes through its points; a closed spline returns to its start; one point, thirty-three points and no length are refused; scatter places one aligned instance every spacing with both ends, stays inside its jitter, is repeatable from its seed and differs with another seed, does not repeat the first instance of a closed curve, and stops at 256 instances; decoding with defaults and six rejections.
- Gate `spline-d3d12` (new, 50 gates now): the training scene with a four-point spline that scatters one of the scene's hut models every 7 m: one spline, four points, four scattered models, all drawn. The capture was looked at: four more huts stand in a row along the far edge of the ground with the curve drawn through them, and the Outliner lists the spline and its four points. The row was put where the scene's own two huts already stand, so the new huts overlap them; that is the test scene's doing, not the scatter's.

### Not verified, and limits

- Placing a spline and a point through the Place panel, and the Inspector's spline fields, have no gate; the gate's scene is written as source. They use the same commands as the other gameplay objects.
- Dragging a point was not exercised by a gate (the code reads the gizmo's draft pose like the model draw does).
- Every spline point is a scene object and so a replicated object in Play, like trigger volumes; a scene of many long splines will meet the session's object limit. Points should become plain source data that Play does not replicate; noted, not done.
- No collision, no navigation, no closure for a scatter-only model, nothing in gameplay reads a spline (all in the design's "not in this step").
