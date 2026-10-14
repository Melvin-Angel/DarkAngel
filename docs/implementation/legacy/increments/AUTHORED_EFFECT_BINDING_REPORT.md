# Authored on-hit effect composition

9 October 2026, following `74717c7`. M4/M5 remain **In progress**.

Optional `.dakit` `effect_bindings` links a granted ability UUID and melee hit-block ID to a frozen effect UUID and magnitude. Up to 32 records are allowed, with at most eight distinct effects per ability/block. Unknown grants, unknown effect catalogue entries, missing melee blocks, duplicate bindings and invalid/nonfinite magnitudes reject native preparation. Frozen kit hashes include the composition; old leases remain pinned. Canonical combat slots and existing sources without bindings retain their behavior.

The server's pure damage context now includes the frozen ability UUID from its native pending hit. This is not a client-supplied hit field or a protocol schema change. Ashen Roots resolves a prepared binding map by that UUID and block. The game owns damage/immunity and execution evaluator 2; the engine retains atomic surviving-target application, credit, cleanup and replication. Heavy-to-Burn is authored in `content/royal_district/combat/player.dakit`; the special heavy damage-type branch and hardcoded Burn magnitude are removed.

Changing the binding magnitude from 5 to 7 produces Health 51 after the same 35-damage hit and two periodic executions, with the same 30-Stamina cost. Native asset checks cover the source binding, missing/unknown/duplicate references and source-free frozen loading. The original light attack remains unchanged.

Focused verification passes: all four affected native checks and primary-editor D3D12 heavy at30/210 ticks plus120-tick light compatibility. Reproduce `python scripts/verify_authored_effect_bindings.py --build`; completed hashes are in `evidence/authored-effect-bindings.json`. The complete 74-test/multi-profile/Vulkan/GNS evidence belongs to the preceding checkpoint, and is not claimed as rerun here.

Composition currently belongs to the kit, applies to surviving melee-hit actors and uses fixed authored magnitudes. Self/on-commit/area/projectile triggers and richer captures are not implemented. Modifier-only effects can be composed without an execution evaluator. New custom execution formulas still need registered game code. The binding map is prepared for a fresh Play session; live kit editing/swapping is not qualified.

The next priority is the native Ability/Effect/Composer designer and validated Save/cook/fresh-Play loop described in `ABILITY_AUTHORING_CONTINUE.md`. There is no shipped ability/effect designer yet. Broad M4/M5 acceptance gates remain open; M6 has not started.
