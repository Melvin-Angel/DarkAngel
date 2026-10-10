# Native hut-jump overflow reproduction

The reported failure is now reproduced against the current native environment triangle meshes, not historical hut box proxies. Twelve approaches run360 fixed ticks each with Jump requested every3 ticks. Approach1 starts(-10,0.51,2) and pushes toward(-7,0,-4); it fails at tick88 with previous foot(-7.98317,0.650578,-0.695314), jump=true and movement(0.447214,-0.894427). Eleven other approaches complete.

Reproduce: build MotorTests in build/m5-editor-relwithdebinfo, then run `MotorTests.exe --hut-jump content/royal_district/collision/environment.dacollision`. Exit2 deliberately reports reproduced overflow; exit0 means all cases completed. The existing default MotorTests run passes. [Probe log](evidence/hut-jump-probe.log), [build](evidence/hut-jump-probe-build.log), [motor regression](evidence/hut-jump-probe-motor-regression.log).

This is direct native motor/real-geometry reproduction, not yet a WorldSession or rendered user-input reproduction. No motor fix or runtime source modification. The pinned Jolt source sets its MaxHitsExceeded flag when reaching the hit cap before attempting contact reduction. Thus the flag alone does not distinguish a reduced contact list from hard collector truncation. Do not simply ignore it: Jolt notes exceeded contact collection can break deterministic ordering. Current native cap32 remains unchanged.

Next: inspect contact collection around this failing pose and test bounded mesh-contact strategies, including enhanced internal-edge removal (currently disabled), before choosing any budget change. Establish an explicit overflow regression, live/replay consistency and one backend after a fix. Preserve bounded work and authoritative movement; do not blindly raise the cap or suppress the guard.

Probe code was prepared before the prior usage window ran out and carried over uncommitted; this checkpoint records it after reset. Current user-imported animation clips/characters and scene records are untracked and remain untouched. M4/M5 In progress; no full qualification claim. No dependencies, push or subagents.
