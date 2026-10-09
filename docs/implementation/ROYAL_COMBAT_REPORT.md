# Royal native melee integration — 9 October 2026

Royal now has a playable light melee ability in the primary full editor. Frozen native kit/attribute/ability/action/clip assets compose with the existing CharacterSceneSession, WorldSession and Jolt motor. M4/M5 remain **In progress**.

Launch `scripts/open_royal_scene.ps1`, select Game, press Play and click the viewport. Mouse Left (the existing attack.light action) activates the light slot. The other seven canonical slots are unassigned. The training target stands in front of the player. Each accepted attack costs 10 Stamina and applies authored power 25 once per target/action. The application registers the pure power evaluator; this is a training recipe, not a final combat formula. Health and Stamina appear in the Game panel. The target has a native stationary motor and rest pose; NPC behavior, idle/death graph and despawn are later work.

`RoyalCombat.dascene` is a new scene composed from the existing environment/model assets. The original RoyalVillage scene, GLBs, skeleton, input/action originals, Unity references and dependency pins are preserved. The primary launcher uses `--character-kit` and explicit scene roles; the editor resolves placed IDs or unambiguous authored local IDs through SpawnPlan origins. Combat framing keeps the stationary player/target clear of the hut wall. One full editor product remains at `build/m5-editor-relwithdebinfo/DarkAngelEditor.exe`.

Input edges queue across zero-simulation render frames and are submitted as protocol-3 slot/edge operations through the same serialized Loopback command path as movement. Overflow rejects the entire newly queued batch. The server reconstructs gestures and commits the existing native grant/cost/cooldown/action transaction. It advances the timeline, sends motor root requests through achieved-motion physics, publishes all motor poses, resolves the bound authoritative melee query and applies existing World/session Health. A completed action still supplies its final root interval. No client hit/damage message or local-host authority shortcut is introduced.

Owner movement/root prediction uses confirmed action state; new pending ability/cost/combo prediction is not implemented. After physics and damage, the client validates frozen action/clip generations and uses the atomic owner motor/action correction for isolated motor replay. It samples the stripped clip at that confirmed action clock, then sends the native-prepared correction ACK and drains receipts. Observer action interpolation/public resource replication remains open. The graph continues on achieved movement and returns to locomotion after the full-body action. This initial profile uses a motor-relative sphere; canonical animated socket sweeps remain open.

## Verification

Reproduce `python scripts/verify_royal_combat.py --build`; exact source/binary hashes and gate results are in `evidence/royal-combat.json`.

- 12/12 affected native checks: combat/ordinary character scene, action binding/timeline, ability assets/commit/correction/intent, kit and graph assets, authoritative melee and actor history.
- Full-editor build/native combat fixture, feature-disabled asset fixture and 4/4 ordinary Headless checks.
- D3D12 and Vulkan Game checks: 120 fixed ticks, two real InputManager activations, target Health 50, Stamina 80 and no pending motor correction. Separate 30-tick captures show the active attack with Health 75/Stamina 90. Original scene locomotion is refreshed separately.
- The native composition checks queued render edges, operation/gesture deduplication, action completion, root/pose integration, exact owner correction, target death, no repeated damage to dead targets and unchanged authoring data. Queue overflow cannot leave a partial activation. Visual review confirmed visible characters and Health/Stamina after the framing correction.

Hidden synthetic checks do not qualify real-time frame budgets or full network faults. No new external GNS scene or live EOS acceptance is claimed.

## Exact remaining M4/M5 gates

Next coherent work: pending owner ability/cost prediction with exact operation inclusion and dependent combo replay, while retaining the same frozen assets and WorldSession path. Also add observer action/public resource state and active-state late join. Retain explicit resynchronization on missing history/generation/queue overflow.

Native effect/tag ownership, deferred reservations, Burn/slow/invulnerability/stagger/equipment modifier slice, period/expiry and death/interruption/source/cleanse cleanup, attributed damage/credit and game-owned Luau composition/formulas remain open. Projectile/ranged abilities and a real melee combo are not implemented. Canonical animated socket sweeps, upper-body/additive/mask/graph action layering, typed graph bool/int/float/enum parameters and IsInAir/OnJumped/OnLanded/OnDamaged states/events remain open. Full Character/Ability/CombatKit/Action Composer reflected authoring, base locomotion/stance selection, kit-swap scripting and graphical health bars remain open. Runtime kit swapping is verified through the existing native WorldSession API; masks are not engine equipment semantics.

Complete M4/M5 input lead/clock offset/jitter/redundancy and RTT 0/50/150/250 ms with loss 0/2/5%, jitter/duplication/reordering/burst loss, streaming/content/topology/history/lifecycle, bandwidth/allocation/cascade and real-time performance qualification remains open. Royal environment collision still uses generated box proxies; precise environment mesh collision and broader player/NPC/camera workflows remain open. M3 live EOS remains externally blocked by private deployment/policy and two identities. Do not promote M4/M5 to Verified from this focused playable slice.
