# Frozen Action Composer clip binding — 9 October 2026

Native action schema 2 binds the existing timeline to a typed clip UUID, canonical rig and explicit motor/none root policy. Schema 1 sources retain their behavior. M4/M5 remain **In progress**.

Source-only clip locators are mount-relative and stripped during cooking. The action owns a frozen clip/archive/rig/archive closure, retaining source identities and dependency pins. Duration must equal the clip's 60 Hz tick count in action units. A non-looping clip cannot drive repeated action loops; an upper-body action cannot claim motor root ownership. The cooked descriptor includes the stripped clip's root curve and archive generation. Runtime loading verifies every closure digest, canonical compatibility and the embedded clip descriptor. The action generation includes frozen dependencies. Ability and CombatKit cooking/loading now carry this same action closure rather than decoding only an unbound source timeline. Warm cooks skip conversion; failures preserve the prior head; feature-disabled binding cooking fails explicitly.

`action_motion_between` computes bounded local-space root requests over action-clock intervals and movement-lock state. Zero-rate hitstop requests no root but retains an active movement lock. The caller transforms the request into motor space and passes it through the existing MotionRequest/CharacterMotor pipeline. Jolt owns achieved displacement; pose sampling never writes world transforms. Active definitions deep-copy the complete clip binding and remain pinned across source edits. This is a native composition primitive; automatic scene/ability motor dispatch is the next increment.

## Verification

Reproduce: `python scripts/verify_action_clip_binding.py --build`; receipt: `evidence/action-clip.json`.

- 16/16 affected native checks: action binding/timeline, ability assets/commit/correction/intent, kit assets, graph assets/runtime, clips/omni, character scene, authoritative melee, motor, motor faults and motor session.
- Full-editor build and action binding fixture.
- Feature-disabled ordinary asset fixture, explicit rejection of clip-binding cooking, and 4/4 ordinary Headless checks.
- The actual Royal 52-tick attack clip is sampled with the same action clock used for native root requests. A real Jolt wall limits achieved root displacement while retaining the action owner. Synthetic 30/60/144 rendering schedules produce identical fixed-tick root/lock traces. The cooked kit fixture now carries a bound action through the ability closure and exercises existing WorldSession commitment/replacement.

These checks do not qualify real-time performance or the full network fault matrix. Original content assets are unchanged.

## Exact remaining gates

Next: compose the cooked kit, action clip, protocol-3 serialized intents/receipts/corrections, motor root requests and authoritative melee in Royal's existing CharacterSceneSession. Royal is still locomotion-only: the new foundation is exercised natively but there is no playable attack or Health presentation yet. Do not read a pose diagnostic as combat completion.

Character/kit/ability and Action Composer editor authoring, typed graph parameters/states/events/transitions, layer/additive/mask/action slots, canonical animated socket sweeps, effects/tags/Luau game-owned formulas/cleanup/credit and deferred reservations remain open. Pending owner prediction/combos, observer action/public resource visibility and late join, health UI remain open. Full M4/M5 input lead/clock offset/jitter, RTT/loss, streaming/topology/lifecycle, bandwidth and performance gates remain open. Royal environment collision remains generated box proxies. M3 live EOS remains externally blocked. No new GNS/EOS or graphics qualification is claimed here.
