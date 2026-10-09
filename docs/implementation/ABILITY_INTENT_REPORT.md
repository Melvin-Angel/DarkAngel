# Owned fixed-tick ability intents

9 October 2026, following ce30120. M4/M5 remain In progress. This connects protocol-3 clients to the same WorldSession native ability commit path and exact owner correction/inclusion. Royal input/action presentation and pending owner prediction remain open.

## Implemented

A typed AbilityIntent contains network/operation/requested tick, avatar epoch, grant generation, kit slot, semantic edge, cancellation and replacement intent. It contains no ability definition, damage amount, claimed hit, held duration or client EntityHandle. The server derives the action binding from its installed kit and reconstructs held/tap timing on the fixed 60Hz clock. A valid matching press is required for Hold/Released/Tapped; hold/tap gestures are consumed once. Cancellation cannot fabricate a tap. Kit replacement resets gesture state and retains the fresh-press gate.

Commands are ReliableOrdered messages in the existing WorldSession envelope/profile 3. Decode enforces readiness, connection-owned motor/avatar, finite bounded structural fields, eight-tick maximum lead and native queue capacity. Transport callbacks only queue work. Fixed-tick advancement expires cooldowns, prepares eligible queued requests, then advances retained actions before motor/physics. A new action enters at the accepted tick with clock zero; it is not advanced retroactively. Time-zero authored hit windows get one initial native point-query opportunity after physics; existing rate-zero/hitstop actions do not repeat it.

Requests revalidate avatar/grant/tick at execution. Expired inputs receive InputExpired without backdating. Expired release cleans an owned release-cancel channel through InputLost; it cannot cause a release activation or an accidental tap. Gesture timing uses the native input profile and ability minimum-held policy; the incoming packet cannot supply duration. Server-native immediate requests retain their existing current-tick semantics; gameplay NPC/host controllers must use the defined fixed-tick phase consistently.

The service prepares the entire owner batch, lifecycle updates, Health costs and output notices before publishing. Exact duplicate wire payloads use the original native record and activation identity; changed payloads fail explicitly. No parallel operation/cost ledger is introduced: raw intent identity is retained inside the existing native operation record. Receipts carry operation, accepted tick and authoritative inclusion revision, plus failure, commitment, activation and duplicate identity. Receipt acceptance remains distinct from inclusion in a correction bundle.

Operation history exhaustion sends an explicit nonterminal HistoryFull/resync notice and retains bounded pending input. No terminal rejection is silently forgotten and later allowed to spend. Native-prepared correction ACK can release storage; retained input is then executed or explicitly expired on its current simulation tick. Incoming/outgoing notice queues are bounded to 128, pending commands to 32 per owner and owner count remains four. Consumers drain notices/lifecycle updates; overflow is explicit.

Connection removal/failure cancels connection-owned native execution, clears held/input queues and rearms grants without refunding costs. Forced cleanup also applies to uninterruptible executions. Current and late Health still use the existing World/ObjectData baseline. Private costs/actions/cooldowns and exact operation inclusion use the owner correction bundle. Queued client receipt handles re-resolve through the current local lifecycle baseline when drained; no raw server runtime handle crosses the wire.

## Verification

Run `python scripts/verify_ability_intent.py --build`; receipt [ability-intent.json](evidence/ability-intent.json). Focused native checks pass 8/8; the full-editor intent executable passes; ordinary SDK-free Headless checks pass 4/4. The primary full editor is rebuilt. Tests cover queued-versus-executed authority, accepted start tick, duplicate commitment, native reconstructed Hold/minimum duration, single gesture consumption, matching tap/release and anti-repeat, stale queued kit and avatar epoch, future lead, expiry/lost release cleanup, forced disconnect, receipt lifecycle replacement and explicit bounded-history resync/recovery. Existing ability asset, correction, commitment, melee, authority/Loopback and motor-session checks are included.

Separate native GNS host/client processes validate protocol-3 owned intents, duplicate input, accepted operation1, exact inclusion, Health100->90 and Stamina60->40 once, aligned correction tick6 and prepared receipt retirement. The process fixture uses published synthetic motor states and is explicitly not a Jolt melee, animation or realtime performance gate. It waits for actual adapter connection admission and uses a final delivered-snapshot control fence. EOS live acceptance remains externally blocked.

## Exact remaining gates

- Native pending owner operation/resource/tag view; acceptance before baseline catch-up, rejection of dependent combo children, out-of-order receipts/baselines and motor/action replay without duplicate costs/damage/cues.
- Full held-input lease/missing-input, automatic clock-offset/jitter/lead, redundancy, four-owner fault matrix and bandwidth/performance qualification. Current expiry/disconnect checks are focused boundaries, not all DAE-009 input-loss gates.
- Frozen native AnimationGraph/CombatKit assets, Character/Ability/Composer authoring and stance transitions; bind Royal to those same definitions and this serialized host/client path. Royal remains locomotion-only in the GUI.
- Canonical socket/clip action integration, motor-achieved root ownership, bounded animated sweeps and all motion/layer/mask/additive/combo/visual gates; effects/tags/deferred reservations/checked Luau/durable credit/health UI; precise Royal collision and remaining M4 lifecycle/streaming qualification.
- Observer action/public state and late-join/interest recovery beyond existing Health/movement. The new intent/correction foundation does not close M4/M5.

Original assets and dependency pins are unchanged. No masks/equipment rules are baked into the engine. M6 remains not started; continuation through Loopback/GNS is authorized.
