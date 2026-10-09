# Global periodic effect capacity and native workload evidence

9 October 2026, following tagged source/scene checkpoint 308cad8. M4/M5 remain **In progress**.

Effect application now reserves the worst simultaneous periodic outcome batch across all configured owners before publication. The shared atomic tick queue holds 128 outcomes; an otherwise valid 129th periodic instance now rejects at application instead of making a future tick fail. Candidate attributes, tags, identities and execution receipts remain unpublished on failure. Explicit removal/expiry/death/source-bound cleanup releases capacity through the actual owned state. Per-source refresh reuses the same active periodic instance and reservation. The policy is conservative and does not rely on schedule staggering.

## Verification and measurement

67/67 complete optimized native tests, native/full-editor/Headless budget fixtures and full product compatibility pass. Run `python scripts/verify_effect_budget.py --build`. Receipt/source/binary hashes and per-profile measurements are in `evidence/effect-budget.json`. The fixture reserves 128 effects across four actors, verifies atomic overflow rejection, runs 240 simulation ticks, checks 512 executions and peak due batch 128, retains coherent attributes/ticks and verifies removal plus refresh capacity. Native/full-editor/ordinary Headless builds use the same native implementation. Full optimized and Royal/tag/effect/asset/observer/prediction/backend/GNS compatibility is refreshed.

Timing covers only native ability/effect advancement and outcome drain. The neutral Health delta still uses the real registered evaluator/resource transaction and complete preparation path. The initial optimized native run measured mean 0.100141 ms, p99 0.196 ms and max 0.2193 ms. Ordinary C++ `new`/`new[]` calls totaled 332964 across the measured 240 ticks. Setup, C/Flecs/aligned allocation, Jolt, graph/pose, graphics, networking and full frame work are excluded. The reproducible receipt records fresh profile measurements; no arbitrary machine-time threshold or full performance qualification is claimed.

The allocation count identifies continuing preparation/capture/container work. Allocation reduction, realistic combined workloads, full bandwidth and RTT/loss/jitter/streaming/lifecycle matrices remain required. Action-owned reversible tokens, effect/timer/credit/cue replication, authored status/combo/projectile/motor policy, reflected graph/socket/authoring/Luau, reservations and RmlUi gates remain open. M3 live EOS is externally blocked. M6 has not started.
