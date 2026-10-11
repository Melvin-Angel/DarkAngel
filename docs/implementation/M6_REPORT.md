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

### Next step (track B)

Place the Royal Guard as the combat target: `load_character_preview` gains an NPC role for the target (its own kit, abilities, attributes and `target_attributes` from `CookedNpc`), `CharacterSceneCombat` gains a target kit so the scripted attacker presses the NPC's kit rather than the player's, and the Royal scene names the NPC for its target placement. Then item 2, the controller seam: replace `drive_attacker` with a small server-side controller interface that requests the operations a player intent does.
