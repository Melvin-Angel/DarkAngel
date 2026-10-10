# Jumping into huts: motor collision overflow triage

User reproduction: spam Jump while moving into the huts in the latest Royal editor Play session. Reported error: Motor collision work overflow; resynchronize.

Read-only tracing confirms CharacterMotor uses Jolt CharacterVirtualSettings.mMaxNumHits=32 (engine/runtime/character_motor.cpp:559), checks GetMaxHitsExceeded after ExtendedUpdate (line693), marks the motor failed and throws. This identifies the guard, not the cause. No local reproduction, build, test or fix has been performed. Earlier M4 evidence describes solid box-proxy hut bounds and blocked doors; precise hut collision qualification remains outstanding. Do not assume that historical layout matches the current edited scene.

Priority after usage reset: reproduce repeated jump plus movement against current hut/corner contacts through the existing WorldSession character session. Inspect the actual prepared collision geometry and Jolt hit/overflow semantics; capture failing tick, pose, overlapping bodies and input. Add a focused regression before changing behavior. Preserve bounded collision work, isolated replay, authoritative movement and existing M4 acceptance semantics. Do not merely suppress the guard or blindly increase the cap. Run targeted motor/session checks and one backend; no full historical suite for diagnostic/form changes.

Usage already92% used /8% remaining at triage, reset18:13:29 Copenhagen10 October; prior user stopping point10% remains applicable. Existing18:15 follow-up is reprioritized to this bug. No implementation before reset.

Observed checkpoint a79b19d007f3b9aaf9b599a2d0b9d20c310266fc. User-owned modified scene/light ability/light Composer and untracked RoyalCombat.dascene.records directory are preserved. Source-byte observations at triage:

- content/royal_district/RoyalCombat.dascene: 90328cae0791f4acbc8a2ef3fcfa5eacc71a51a293945389f9c4f0772d5225c1
- content/royal_district/combat/light.daability: a9c34e882e9ad0fd06eecaa84f9f9c56fa08ac6aa0b9b0808e26cc6068b4bf0f
- content/royal_district/combat/light.daaction: 8d858aa360f56e7a0694a079dd8d5d2078cb205226b2e91121877ca3280b9d44

M4/M5 remain In progress; no new verification claim. No runtime/source content edits, dependencies, subagents, push or reset consumption during this triage.
