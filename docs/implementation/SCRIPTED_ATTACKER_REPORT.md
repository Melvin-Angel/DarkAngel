# Scripted training attacker on the combat target - 11 October 2026

First item of the autonomous M5 list: something that attacks the player, so player-side damage, flinch, interruption, dodge invulnerability and death can be exercised in Play. This is shared combat-target behaviour on the existing target actor. It is not an NPC controller, AI or an M6 claim. M4/M5 remain In progress.

## What it is

`CharacterSceneCombat` has an optional `attacker` (`CharacterSceneAttacker`: slot, first tick, interval, range, face-player). When set, `CharacterSceneSession`:

- equips the existing combat target with the scene kit on the server (`equip_combat_kit`, the same grant path as the player);
- on each scheduled tick, while both actors are alive, the target is idle and the player is within range, submits one request for that slot through `WorldSession::request_ability`, the server's existing checked request path. Requirements, Stamina cost, cooldown, commitment, hit windows, the authoritative melee query, the game damage evaluator, bound effects and interruption are the ordinary ability path. There is no damage shortcut and no client or presentation involvement;
- on a committed request turns the target to the player for that activation, then feeds the action's root/lock request to the target's motor exactly as it does for the player (the shared `action_motion` helper).

Nothing changes when `attacker` is unset: the target stays the passive actor every earlier gate uses. No wire, protocol, prediction or asset-schema change.

The target's attack is shown by the existing observed-pose path from its public action state. The player's side uses what already existed and had only been covered by server-only tests: Health arrives in the owner correction, the flinch is selected from the public effect frame for the owner's confirmed pose, interruption reaches the owner through reconciliation, and the kit death timeline plays on the owner pose.

## Using it

Game workspace, Gameplay Inspector, **Target effects** tab, **Training attacker**: tick "Target attacks the player" (optionally "Heavy"), stop and press Play again. The target then attacks every 150 ticks while the player is within 2.5 m. The tab shows the last request and why it was refused, if it was. Command line for scripted runs: `--target-attacks light|heavy` (first attack at tick 10, every 120 ticks) and `--exercise-attacked`.

To see the flinch in the Royal scene, bind the owned Flinch effect to a hit window in the Combat kit panel as before (it is still unbound in `player.dakit`; binding it by default is part of the stagger item).

## Verification

`python scripts/verify_effect_reaction.py` now runs twenty gates and all pass ([receipt](evidence/effect-reaction.json)); every target was rebuilt first through the pinned build command (`character_scene.hpp` changed), with no vcpkg activity. The whole CTest suite in the editor build directory was then run once: 82 of 87 pass, and the five that do not are the known unrelated ones (`M5.actor_assets`, `M5.attribute_authoring`, `M5.composer_structure`, `M5.composer_preview`, `M4.royal_mesh_collision`).

- **CharacterCombatSceneTests** (`M5.character_combat_scene`, cooked Royal `player.dakit`, fresh isolated sessions):
  - Target placed facing away, Light every 120 ticks from tick 10: no request before tick 10; the request is committed by authority and is public state on the same tick; the target turns to the player; its observed pose leaves locomotion; the hit lands at tick 26 and the player's Health is 75 in both the replicated world and the owner correction; exactly one damage per attack (50 after the second); prediction pending 0, no resynchronisation beyond the session bootstrap, target unharmed.
  - Range: a target 6 m away never requests. Facing off: the swing is committed but misses, so hits come only from the authoritative query. An unassigned slot is rejected at session creation.
  - Royal game rules with the owned Flinch effect bound to the Light hit: the player starts a Heavy four ticks before the incoming hit; the hit interrupts it through authority and owner reconciliation (confirmed and predicted action both gone, no pending operation, the interrupted Heavy never damages the target); the owner's pose equals the flinch clip sample on the effect-instance clock; a Heavy pressed while State.Staggered is refused and settles without a resync.
  - Death: four hits take the player to Health 0; the owner death timeline starts and its final pose is held; the script stops requesting; a press by the dead player is refused and prediction stays settled.
  - Dodge invulnerability against a real hit: a wall behind the player stops the dash so both runs end in the same place. Dodging 45 ticks before the hit (window over) takes 25 damage and flinches; dodging 14 ticks before the hit (inside the authored State.Invulnerable window) takes no damage and no flinch, then or later.
  - The authoring world is unchanged throughout.
- **D3D12 editor Play** with the authored fixture kit (Flinch on Heavy, death timeline) and `--target-attacks heavy`, both captures inspected:
  - [tick 40](evidence/attacked-flinch-d3d12.png): player Health 60, owner flinch at clip tick 14, no death, prediction pending 0, scene unchanged. The player is visibly knocked forward with the target behind it.
  - [tick 240](evidence/attacked-death-d3d12.png): player Health 0, death timeline at tick 94, prediction pending 0. The player is down; the target stands.
- The existing reaction, death, dodge, mask and camera gates pass unchanged with the attacker off.

## Limits

- A fixed-interval script: no movement, pursuit, target selection, combos, blocking or dodging. It attacks from where the scene placed it.
- It uses the player's scene kit (the active mask's kit under masks). There is no separate enemy kit, Character or attribute set.
- Nothing regenerates Stamina, so the target stops after its Stamina is spent (ten Lights or five Heavies with the Royal kit); the inspector then reports "not enough Stamina".
- The toggle is editor session state applied on the next Play; it is not saved in the scene or the Player.
- A dead player can still walk: death is presentation plus refused abilities; there is no motor rule, respawn or downed state yet.
- Stagger is still a pose and an ability gate (State.Staggered), not a movement restriction.
- Not verified: Vulkan, host/client over GNS, physical pointer use of the new checkboxes.
