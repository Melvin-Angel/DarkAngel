# Frozen native locomotion graph increment — 9 October 2026

Scope: native `.dagraph` schema 1 cooks the existing compiled clip/1D/2D normalized-locomotion profile. This makes CombatKit stance references implementable through the asset service without a second graph runtime. M4/M5 remain **In progress**.

The source owns a persistent AssetID, stable node IDs, root, explicit blend points/triangles and typed clip UUID references with source-only mount-relative locators. Clip sources retain their existing sidecar and canonical rig IDs. The frozen graph product strips paths and records all transitive clip/rig product hashes. Source, sidecar and canonical bytes participate in the cook recipe. The existing transactional catalog preserves the prior generation when clip conversion, dependency identity or compiled graph validation fails. Warm cooks skip conversion. Runtime loading uses only the package registry/CAS, verifies types, hashes and frozen dependency generations, constructs the existing immutable plan and owns the frozen clips. The compiled plan still enforces node/depth/cycle/reachability, skeleton compatibility, triangulation and four-active-pose budgets.

Royal's new `content/royal_district/locomotion.dagraph` reproduces its nine-clip cardinal preset. `prepare_royal_scene.py` cooks/packages it and the primary launcher uses `--character-graph`. The full editor resolves the frozen rig against the existing skinned model and feeds the same CharacterSceneSession, motor-achieved graph and GPU skinning path. The older explicit nine-clip diagnostic option remains available. Original GLBs, sidecars, skeleton, action sources and dependency pins are unchanged.

## Verification

Reproduce: `python scripts/verify_graph_assets.py --build`. Exact source/binary hashes and gate results: `evidence/graph-assets.json`.

- 7/7 affected native checks: graph assets/runtime, clips/omni clips, character scene, ability assets and ordinary asset pipeline.
- Full-editor build and frozen graph fixture.
- Feature-disabled asset fixture and ordinary Headless smoke check.
- Primary Royal editor: 120 fixed ticks on D3D12 and Vulkan with the cooked graph, existing input/motor/session/pose exercise.

These are focused deterministic/hidden checks. They do not qualify real-time performance, network fault/streaming or live EOS behavior.

## Exact remaining gates

Next coherent increment: versioned CombatKit source/cook/load with optional canonical eight slots, frozen input/schema/ability/stance closure, runtime swap and existing WorldSession grant lifecycle. Then connect Royal input to protocol-3 ability intents and native authoritative melee, Action Composer root/motor ownership and rendered attack/target Health.

The initial graph asset profile does not add bool/int/enum parameters, character states/events (IsInAir/OnJumped/OnLanded/OnDamaged), transitions, authored layers/additive/masks or Action Composer slots. Those native authoring/runtime gates remain open. Canonical animated socket sweeps, effects/tags/Luau game-owned formulas and cleanup/credit, pending owner prediction/combos, observer actions/public resource visibility and late join, character workflow and Health UI remain open. Complete M4/M5 timing/input lead/clock offset/jitter, RTT/loss, streaming/topology/lifecycle, bandwidth and performance qualification remains open. Royal environment collision remains generated box proxies. M3 live EOS remains externally blocked. No masks or final combat formulas were baked into the engine.
