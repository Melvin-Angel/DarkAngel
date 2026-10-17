# Extensibility audit: making a game that is not Ashen Roots

14 October 2026, Claude Code. Question from the user: if future-me wanted to make a quick third-person action shooter on DarkAngel, how hard would it be? Read-only audit of the source at `6870d81`; nothing here is implemented.

## Short answer

- **A co-op third-person shooter prototype played inside the editor: days.** Most of what a shooter needs already exists as authored data: aimed abilities with travelling projectiles, an aim camera, a crosshair, NPCs that shoot back, encounters and checkpoints. All of it runs over server-authoritative networking with prediction.
- **Making it feel like a shooter (sprint, ammo and reload, hitscan, weapon switching on the HUD): one to three weeks of engine work.** Each of those touches a fixed vocabulary in engine code rather than data.
- **Shipping it as its own game: weeks, mostly not shooter-specific.** There is no standalone game executable (Play exists only inside the editor), and the editor is wired directly to Ashen Roots' game code.

The engine is in better shape than the "Ashen Roots engine" worry suggests. `engine/` contains no Royal or Ashen Roots names, and the game layer is a separate library (`games/AshenRoots`, about 3 KB of code). The problems are elsewhere:
- melee-action assumptions in a few engine types: combat slots, masks, the HUD model, movement tuning;
- the missing seam between editor and game.

## What a shooter can reuse today

| Need | What exists | Where |
| --- | --- | --- |
| Third-person movement | Fixed-tick character motor with jump buffer, coyote time, crouch, moving platforms; client prediction and correction | `character_motor.hpp`, `world_session.hpp` |
| Over-the-shoulder aiming | Authored camera rig (FreeLook, Aim with its own FOV and facing), blend on a held modifier, aim pitch on the motor | `camera.hpp`, `CAMERA_REPORT.md` |
| Guns | An aimed ability (`targeting: aim`, range) with an optional projectile: speed 1–200 m/s, gravity, lifetime. Authority owns flight, with one swept step per tick against characters and a ray against world collision, so no tunnelling. No friendly fire. | `ability.hpp` `AbilityAim`/`AbilityProjectile`, `PROJECTILE_REPORT.md` |
| Damage, DoT, status | Attributes, costs, effects with stacking and periodic ticks, tags, hit reactions, stagger, death timelines | `attributes.hpp`, `effects.hpp`, `ability.hpp` |
| Enemies that fight back | Behavior trees, senses (sight cones, hearing), navmesh, ranged NPCs (the archer), encounters with waves and trigger volumes | `behavior_tree.hpp`, `perception.hpp`, `navigation.hpp`, `encounter.hpp` |
| Levels and progression | Scene editor with Place/Inspector, doors/gates/platforms, pickups, checkpoints (altars), saves, scene travel, fast travel | `world_objects.hpp`, `engine/persistence`, `scene_transition.hpp` |
| Co-op | Up to 4 players (`scene_party_limit`), two-process UDP, party travel, revive/wipe | `party_control.hpp`, `party_lifecycle.hpp` |
| Feel | Effekseer VFX and Amplitude audio through cues; trails and decals | `engine/vfx`, `engine/audio`, `cue_player.hpp` |
| HUD | RmlUi document fed from confirmed state, with a crosshair while aiming, a target and a lock indicator | `hud.hpp`, `games/AshenRoots/ui/hud.rml` |
| Content | glTF/FBX import, cooking into a content-addressed store, typed authoring with undo/redo and coordinated Save | `engine/assets`, `apps/editor` |
| Input | Data-driven input profiles with semantic actions and chords | `input.hpp`, `.dainput` assets |

A first prototype could look like this:
- a Player with a kit whose RangedLight slot is "fire" (projectile at 150–200 m/s) and RangedHeavy is "alt fire";
- the Aim camera on the aim modifier;
- archer-style NPC behaviours and an encounter with waves, using the existing assets as templates.

None of that needs C++.

## Where it gets in the way

Ordered by how much each would cost a second game.

### 1. No standalone game, and the editor is wired to Ashen Roots (largest)

- Play runs only inside `DarkAngelEditor`. `DarkAngelHeadless` runs cooked script packages or scenes, not gameplay. The roadmap's `DarkAngelGame` does not exist yet.
- The editor calls game code directly:
  - `ashen_roots::configure_royal_combat` in `apps/editor/character_preview.cpp`;
  - `ashen_roots::royal_hud` in `main.cpp`;
  - the HUD document directory compiled in as `DAE_GAME_UI_DIR=games/AshenRoots/ui`;
  - the prop shader read from `games/AshenRoots/shaders`;
  - `AshenRootsCombat` linked into the editor and ten test targets.
- The game library needs a generated `royal_tags.hpp`, from `scripts/generate_royal_tags.py`, and checks the tag registry generation against it.

Today a second game means forking those call sites or adding `#ifdef`s.

**Fix:** a small game-module interface the project selects, for example `GameModule { configure_combat(CharacterSceneCombat&); hud_model(...); ui_directory(); shaders(); tag_registry(); }`, with Ashen Roots as its first implementation. The editor and a future game executable both take it. The refactor's steps 2–3 (EditorApp, PlayController) are the natural place: once Play is a service, a thin `DarkAngelGame` can host the same PlayController without the editor.

### 2. Combat vocabulary is melee-action shaped

- `CombatSlot` is a fixed enum of nine slots (Light, Heavy, RangedLight, RangedHeavy, Spell1, Spell2, Block, Parry, Dodge) in `combat_kit.hpp`. It is baked into the kit asset schema, the cooker and the editor.
  - A shooter can map onto it: fire, alt fire, grenade, melee, roll.
  - Reload, weapon swap or interact-while-aiming need a new slot, which means engine, schema and editor changes.
- **Masks are an engine concept:**
  - `mask_slot_count=4` in the Player asset;
  - the mask name in `HudViewModel`;
  - mask loadouts in `CharacterPreviewResources`.

  They work as weapon loadouts (switching the active mask swaps the kit through authority and prediction, see the mask switch gate), but under Ashen Roots' name.

**Fix:** rename masks to loadouts in the engine layer (Ashen Roots keeps calling them masks in its UI). Make slots a per-project list in the kit schema rather than an enum, or at least add generic Ability1..N slots.

### 3. Shooter mechanics that do not exist

- **Hitscan:** none. A 200 m/s projectile is close enough for a prototype; real hitscan would be a new targeting mode beside `aim` that reuses the projectile's world ray and the shared strike path.
- **Ammo and reload:** no weapon model. Ammo can be modelled with today's data as an attribute: firing costs 1 (ability costs exist), and a reload ability applies an effect that restores it. That covers a prototype; a magazine/reserve split and reload cancelling would want native support.
- **Aim assist, recoil and spread:** none. Spread would be an aim parameter on the ability; recoil is presentation on the camera rig.
- **AI line of sight:** NPCs fire without a line-of-sight check (noted in `PROJECTILE_REPORT.md`). Senses do have sight, so this is wiring, but a shooter notices it at once.

### 4. Movement tuning is compiled in

Walk speed is 5 m/s and crouched speed 2 m/s, as literals in `character_motor.cpp:671`. The jump buffer is six ticks. No per-character movement asset exists. A shooter wants sprint, slower aiming movement and possibly slide. Each is a motor change, and every motor change must stay deterministic for prediction and correction (`MotorTests`, `MotorSessionTests`).

**Fix:** a movement-profile asset referenced by the Character/Player, cooked into the motor's fixed-tick parameters.

### 5. The HUD model is a fixed struct

`HudViewModel` has named fields (health, stamina, mask, target, crosshair, lock). An ammo counter or weapon name means editing the engine struct and the game's mapping function. The data-driven UI plan (`future/UI_PLAN.md`) is deferred by the user; it is the right fix and would also serve Ashen Roots.

### 6. Gameplay rules are C++ callbacks, not scripts

Luau scripting (`script_runtime.hpp`) attaches behaviour scripts to world objects (transforms, events); it does not reach combat. Game rules are C++ callbacks on `CharacterSceneCombat` (`damage`, `combat_damage`, `effect_evaluators`), which is how Ashen Roots implements invulnerability and its on-hit effects. That seam is clean and adequate for a code-first game. A designer-scripted shooter would need script bindings for the combat callbacks.

### 7. Scale limits worth knowing

- Sessions hold at most 33 characters (`session_character_capacity`).
- Parties hold up to 4 players.
- Actors are skinned humanoids with a locomotion graph; there are no vehicles or non-humanoid motors.

Fine for a co-op horde or mission shooter. Not for large PvP or vehicles.

### 8. The editor's `main.cpp` monolith

`main.cpp` is still a large orchestrator (246 KB after refactor step 1c). Game-specific editor features currently land there. Refactor steps 2–3 move it into services; until then, game-specific editor work is the most expensive kind.

## Suggested order if a second game becomes real

1. Finish refactor steps 2–3 (EditorApp, PlayController), then introduce the game-module interface and move every `ashen_roots::` call behind it. This is the precondition for a `DarkAngelGame` executable.
2. Generalize masks into loadouts and combat slots into a per-project list.
3. A movement-profile asset (speeds, sprint, aim movement, jump) in place of motor literals.
4. A hitscan targeting mode, plus the ammo-as-attribute pattern documented as the supported way.
5. A data-driven HUD binding (the deferred UI plan), so a game adds counters without engine edits.
6. Script bindings for combat rules, if designers rather than programmers will write the second game's rules.

Items 1–3 also make Ashen Roots cleaner; none of them is shooter-only work.
