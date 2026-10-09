# Authoritative motor-relative melee

9 October 2026, following a3a8d12. M4/M5 remain In progress. This implements an initial authored melee query/damage profile inside existing WorldSession ability state, not the finished Royal attack or animated weapon-socket sweep qualification.

## Implemented

AbilityDefinition optionally declares up to eight melee profiles, each associated with an existing HitWindow block. A profile contains a bounded motor-relative XYZ offset, sphere radius, finite supplied power, registered native evaluator ID and typed damage-type ID. Duplicate/non-hit block references and invalid fields/bounds reject at cooking or grant preparation. `.daability` sources optionally carry the same `melee` array, with `block`, `offset`, `radius`, `power`, `evaluator` and `damage_type`; these fields are frozen and included in the ability generation. The current policy hits each target once per activation/block/loop/target epoch.

WorldSession owns one explicitly bound native melee query provider and up to 16 registered game-owned evaluators. Melee grant installation requires both. There is no client hit command, client damage magnitude or engine-selected damage formula. Native evaluators receive immutable typed context and source/target attribute snapshots; they must be pure and produce bounded finite nonnegative damage. Damage/defence calculations remain game-owned. Current evaluators produce only Health damage; status/stagger/forces and tag-based immunity are later extensions.

AbilityState retains authored crossed hit intervals after timeline advancement, including a window crossed entirely in one tick and an action completing in that tick. Pending hit work pins the ability/action generation. It must be resolved or explicitly cancelled before advancing or starting another execution; overflow is explicit, never silent dropping. Cancellation, grant replacement, death and despawn invalidate owned pending requests. Already applied damage and paid costs are retained.

PhysicsMeleeQuery uses the existing authoritative PhysicsWorld, refuses prediction/replay contexts, and checks the fixed tick, topology, source/target epoch, achieved position, yaw and stance against the published motor state. All motors must complete post_physics before gameplay queries qualify. The initial profile overlaps an authored sphere in achieved motor space and conservatively checks present static/kinematic/dynamic blockers. Native collision hits are checked for world/tick/body/epoch validity. Query/result bounds fail preparation explicitly.

WorldSession resolves in stable owner/activation/block/loop/target order. Each request rechecks current source/target liveness, motor epoch and damageable owner state. Hit identity and target Health mutations prepare in copied native ability states; invalid evaluator output or queue exhaustion publishes neither damage nor ledger changes. A lethal result forces action cleanup immediately and invalidates later requests from the dead owner in that batch, including uninterruptible actions. Results retain source activation, target owner, tick/window/loop/type and actual clamped damage. Persistent credit identity across respawn remains open.

Health publication updates both the existing private World facade and authoritative ObjectData, then advances the existing WorldSession baseline revision. Current and late Loopback clients receive the resulting Health. The hit ledger is bounded to 256 identities per execution, native candidate lists to 16 per query, pending intervals to 64 per owner, total resolution work to 256 candidates and output to 128 results. Existing lifecycle queue limits remain enforced. Repeated resolution of the same completed tick is idempotent. Registrations/bindings cannot be replaced during the session.

## Verification

Reproduce `python scripts/verify_melee.py --build`; receipt [melee.json](evidence/melee.json). Tests cover real authoritative Jolt overlaps, present blockers, liveness and death ordering, lethal clamping, repeat-hit and duplicate-tick protection, completed crossed windows, explicit cancellation of pending completed work, evaluator rollback/retry, fixed-step backlog protection, native query phase qualification, wrong-role/prediction rejection, invalid authored block references, cooked melee roundtrip, current/late Loopback Health and identical synthetic 30/60/144 tick damage traces. Focused checks pass 5/5 native, 2/2 full-editor and 2/2 ordinary Headless. The primary full editor was rebuilt. These are focused checks, not refreshed full M4/M5, rendering, performance, GNS or EOS qualification.

## Remaining gates

- Wire client ability intents, typed terminal receipts and authoritative inclusion through existing WorldSession; coherent resource/action/cooldown snapshots and owner prediction/late join. No ability request RPC or new ability wire bundle is implemented here.
- Integrate real Royal input/attack pose/root/ability presentation using that shared path. The Royal GUI remains locomotion-only. Action layer/slot/mask arbitration, root requests before motor and achieved-transform canonical socket sweeps after physics remain required.
- This initial stationary motor-relative sphere is not an animated weapon sweep. Bounded socket sampling/subdivision, movement across an interval, teleport/correction discontinuities, authored repeat policies and measured sweep overflow/error gates remain open. Default melee stays on server action time; historical melee is not enabled.
- Native AnimationGraph/CombatKit frozen assets, typed parameters/events and stance swapping; reflected native source authoring/Character/Ability/Composer workflows and graph/mask/additive/GPU motion qualification.
- Effects/tags, deferred reservations, immunity/defence/stagger/status/death policies, checked Luau APIs and durable attribution; runtime health UI.
- All remaining M4 clock/input redundancy/fault matrix, multi-owner stress, precise Royal collision, region/cell lifecycle barriers and performance/bandwidth gates. M3 live EOS remains blocked on external private service inputs; M6 remains not started.


Latest owner-correction increment (9 October): [atomic ability/motor correction](ABILITY_CORRECTION_REPORT.md) adds protocol-3 owner-only fragmented bundles, exact terminal operation inclusion, lifecycle/Health checks and native-prepared ACK retirement. Client ability intents/receipts, owner prediction, observer actions and playable Royal integration remain open; M4/M5 remain In progress. Reproduce `python scripts/verify_ability_correction.py --build`.
