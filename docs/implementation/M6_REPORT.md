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

### Next step (track B)

Item 3, navigation: check whether `recastnavigation` is installed in the build tree (dry-run rule from the autonomous prompt, additions only), then a Recast/Detour adapter, a cooked navmesh for the Royal scene collision, a path query, and a controller that follows the path to the player. The controller seam needs no change for this: a path follower is another `ActorController`.
