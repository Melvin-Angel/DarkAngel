# Atomic owner ability correction

9 October 2026, following eaea998. M4/M5 remain In progress. This extends the existing WorldSession protocol with an owner correction bundle and native-prepared ACK; client ability commands and pending prediction are the next integration step.

## Implemented

Protocol profile 3 adds AbilityCorrection and AbilityCorrectionAck to the existing session envelope, adapters and lifecycle/readiness handling. Profiles 1/2 retain their existing message classes. The new payload is versioned schema 1 with explicit integer widths, scalar encoding, enum values and fixed UUID/hash bytes. Existing motor codecs now live in one shared private helper used by motor messages and correction bundles; no native object layout or EntityHandle is copied onto the wire.

A bundle freezes session/network identity, world baseline revision, achieved MotorState, ability tick/revision/grant generation, Health schema IDs, all owner attributes, unexpired cooldowns, exact retained terminal operation records and current action/ability identity and pinned generations. It includes active slot, activation, action clock/rate/phase. Motor and ability ticks must agree. The client checks the bundle against its current reliable object/lifecycle baseline, including Health/MaxHealth equality, and resolves a fresh local checked entity handle before publication. The bundle is retained separately from the generic latest observer motor sample for coherent native owner reconciliation.

Operation records explicitly carry ID, acceptance/rejection reason, commitment and activation identity. Highest operation and explicit retirement boundary are diagnostics/control metadata; sparse IDs are not inferred as included. Native owner prediction must use exact terminal records and rejection receipts. An ACK identifies the actual sent/retained bundle tuple and its included retirement bound; the server checks connection ownership, baseline revision and sent history before releasing native receipt storage. Retired IDs remain ineligible to commit again. ACK is explicit after native assets/reconciliation are prepared, not automatic on packet receipt.

Only the connection owning the corresponding motor/avatar receives private owner bundles. Observers retain existing public Health/movement baselines; new observer action/public-tag replication is not claimed. Native NPC callers remain server-local through existing ability APIs. The existing server ability service produces all bundle data; no parallel combat world, transport or resource ledger is introduced.

Bounds: 64 attributes, 32 cooldown groups, 128 exact operation records, four owner states, 8192 payload bytes and at most ten 896-byte fragments. Each packet is at most 968 bytes including session and fragment headers, below the existing 1000-byte budget. Fragment groups freeze their payload until complete or lifecycle replacement; duplicate/reordered pieces cannot publish partial state. Conflicting duplicates, invalid types/counts/IDs, nonfinite values, incoherent ticks and malformed action state reject. Incomplete groups expire on the existing bounded transport-pump timeout and expose a resync flag. Periodic keyframes recover lost groups; a successful native-prepared ACK clears resync. Server sent histories and retired-owner bookkeeping remain bounded.

Owner corrections are scheduled before ordinary motor/collision snapshots in profile 3. Current scheduling/fragmentation establishes correctness, not final bandwidth or realtime performance acceptance. Damage evaluation now extracts only attribute values instead of copying the new operation snapshot history into each read-only evaluator context.

## Verification

Run `python scripts/verify_ability_correction.py --build`; receipt [ability-correction.json](evidence/ability-correction.json). Tests exercise a 64-attribute fragmented bundle, exact accepted/rejected sparse operation inclusion, coherent costs/cooldowns/action/motor tick, local lifecycle handles, explicit ACK retirement without duplicate spending, reordered fragments, missing-fragment timeout/keyframe recovery and resync acknowledgement, owner-only audience and wrong-role rejection. Codec probes reject truncation, duplicate attributes, nonfinite values and motor/ability tick mismatch. Existing authority, Loopback, motor-session, ability commitment and melee tests are included with full-editor rebuilding and ordinary SDK-free Headless compatibility.

Focused verification passes 6/6 native tests, the full-editor correction executable and 3/3 ordinary Headless checks. The primary full editor was rebuilt.

These are focused receipts, not a new full-suite, GNS/EOS, latency matrix, owner replay or playable Royal acceptance.

## Remaining gates

- Owned client ability intents with stable operation IDs, tick/lead/expiry limits, reconstructed input-edge/hold/tap timing, terminal receipt delivery and lifecycle/disconnect cancellation. Local host must use that serialized shared path for the playable slice.
- One native owner pending-operation/resource/tag view, correlated acceptance/rejection/inclusion, dependent combo invalidation and coherent motor/action replay with asset generation preparation. This bundle does not itself execute client prediction.
- Current and late observer action phase/public attributes/tags, persistent cues and interest/lifecycle restoration without replaying past damage.
- Connect Royal ability/input/action/root/pose and canonical socket sweeps, native graph/kit assets and reflected authoring, effects/tags/reservations/cleanup, native health UI and all remaining M4/M5 qualification. See MELEE_REPORT.md and prior milestone reports.

M3 live EOS remains externally blocked. Dependencies, original content and prior profiles are preserved; M6 remains not started.


Latest intent increment (9 October): [owned fixed-tick ability intents](ABILITY_INTENT_REPORT.md) adds protocol-3 semantic slot/edge requests, server gesture reconstruction, expiry/lead/epoch validation, canonical native receipts/inclusion and connection cleanup. Focused native/editor/Headless and separate GNS cost/correction process checks pass; Royal GUI ability/root/pose, pending prediction and full M4/M5 gates remain open. Reproduce `python scripts/verify_ability_intent.py --build`.
