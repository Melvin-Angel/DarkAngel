# Scene scale: per-model collision and incremental placement

Status: phase 0 (baseline) and phase 1 (per-model collision product) done on 15 October 2026; phase 2 next. Builds on cf818c2, fb4b01e and f2d641b, which fixed three
placement failures but kept the current architecture.

Decided on 15 October 2026: a static body budget of 16,384; full-mesh collision by default, with a per-model `none` import option;
doors, platforms and pickups stay in the 64-body moving budget.

## Problem

Placing one model the scene has not used before does all of this, synchronously, in `Shell::place` → `prepare_play_resources`
(apps/editor/play_controller.cpp):

1. Packages every authoring root into a new Play registry (`package_authoring`, which also calls `cook` on every root).
2. Bakes the whole scene's collision again: every placed static model is transformed into world-space triangles and written to
   `<scene>.dacollision` (apps/editor/scene_collision.cpp), then cooked and packaged a second time.
3. Reloads every resource: `EditorApp::reload_model` → `SceneResources::prepare_reload` re-uploads every scene model to the GPU, and
   `PlayController::prepare_reload` rebuilds the character resources, re-baking each placement in memory to check it against the baked
   file (the "collision is stale" check in apps/editor/character_preview.cpp `prepare_character_scene`).

Scene size is capped far below what levels need:

| Limit | Where | Why it exists today |
|---|---|---|
| 16 static meshes per scene | `bake_scene_collision`, `encode_collision_source`, `PhysicsWorld::insert_mesh`/`load`, `CollisionHistory::retain`, `validate_collision_stream`, `prepare_collision_frame` | Every static mesh is part of every `CollisionFrame`. History keeps one frame per tick, and `replay_motor` calls `PhysicsWorld::load` for every replayed tick, which destroys and recreates every Jolt body. The cost is meshes × replayed ticks × reconciles. |
| 64 bodies per world (boxes + meshes) | `PhysicsWorld::insert`/`insert_mesh`/`load` | Same per-frame copy and rebuild. |
| 2048 welded vertices / 4096 triangles per placed mesh | `bake_model_collision`, `validate_collision_mesh`, collision source decode | Bounded per-frame work; one baked world-space mesh per placement. |
| 1 MiB collision source | collision cooker | One file holds every placement's world-space triangles. |
| 64 package roots, 512 products | `AssetService::package`, `load_cooked_model` | Each placed model is its own package root. |
| 1 MiB registry, parsed on every model load | `load_cooked_model` | Registry JSON is read and parsed again for each model (quadratic in model count). |
| 63 uploaded models | `SceneResources::prepare_model` | An arbitrary cap; no GPU resource reason found. |
| 1024 objects, 1 MiB scene document, 65,536 JSON nodes | assembly.cpp, world.cpp, editor_document.cpp | Document bounds; separate from collision. See phase 6. |

Root causes:
- **Collision is per scene, not per model.** One hut placed 50 times becomes 50 separate world-space triangle copies, all re-baked
  whenever any placement changes.
- **Static geometry is treated as per-tick state.** It never changes during Play, but it is copied into every history frame and
  rebuilt for every replayed tick.
- **Placement goes through Play preparation.** Adding one model to the editor viewport is coupled to packaging, collision baking and
  a full Play-resource reload.

## Target architecture

- **Collision is part of the model.** Cooking a model also produces a local-space collision shape for it (welded, validated,
  deterministic). A placement is a body: that shape plus the placement's transform and uniform scale. Nothing is baked per scene,
  and moving a placement moves a body without rebuilding triangles.
- **Static geometry is a static layer, separate from tick frames.** A `StaticCollisionLayer` is an immutable, shared set of
  `{body id, shape, transform}` with a content revision (a hash over body ids, shape signatures and transforms). `PhysicsWorld` holds
  one layer. `CollisionFrame` and `CollisionStreamFrame` carry only boxes, actors and the layer revision. History and replay never
  copy or rebuild static bodies.
- **Every participant derives the layer from the scene.** Host, guests, prediction and replay worlds build the same layer from the
  scene's placements and the cooked model shapes. The session handshake compares revisions instead of sending geometry.
- **Editor placement is incremental.** Placing a model cooks that one model if needed, loads it directly from the asset cache,
  uploads only it to the GPU, and adds one body to the editor's query world. Packaging and Play preparation happen only at Play
  (and travel), and only when something changed.
- **Merging is optional and later.** At Save or Play, static bodies may be merged into one compound per spatial cell, only if
  measurements show the broadphase or queries need it (phase 5).

## Phases

Each phase lands on its own, builds, and keeps the existing gates green. Every phase ends with one verification pass (build, the
relevant CTest subset, the editor gates it touches), per the one-pass-per-chunk rule.

### Phase 0: scale baseline (measurement first)

- A generator script (scripts/) that writes a scale-test scene with N placements of the Royal static models (N = 100, 500, 2000),
  kept outside `content/` or in a dedicated `content/scale_test/` scene, so it never touches RoyalCombat.
- Timings, printed by an editor exercise flag (for example `--exercise-scale`): place one new model, place one already-used model, move a
  placement, prepare Play, Play tick time, owner reconcile time (replay ticks × bodies), editor frame time, and process/GPU memory.
- Today's numbers stop at 16 static models; record them as the baseline, then rerun after every phase.

Implemented: `scripts/measure_scene_scale.py` (scene generator and driver; scenes go to the gitignored `content/_scale_test/`, which
the script removes afterwards, and it uses its own asset cache under `.cache/scene-scale/`), the editor's `--exercise-scale
<config.json>` (apps/editor/exercises/scale_exercise.cpp, measures only and never saves), and `SceneScaleBench`
(tests/scene_scale_bench.cpp, not a CTest). Rerun with `python scripts/measure_scene_scale.py`. Results go to
`.cache/scene-scale/baseline.json`.

#### Baseline, 15 October 2026 (hidden editor, D3D12, warm asset cache)

Scenes are RoyalTraining (ground, two huts, the player) plus N hut placements. "Models" counts every model placement, the player
included.

| Models | Authoring frame p50/p95 | Place used model | Move placement | Place new model | Prepare Play | Play frame p50/p95 | Private memory at end |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 1.3 / 2.0 ms | 10 ms | 8 ms | 5594 ms | 3400 ms | 1.4 / 2.4 ms | 751 MB |
| 16 | 1.5 / 2.4 ms | 13 ms | 7 ms | fails | fails (collision source byte limit) | — | 180 MB |
| 20 | 1.5 / 2.3 ms | 11 ms | 7 ms | fails | fails (16-model limit) | — | 181 MB |
| 21 | 1.5 / 2.4 ms | 11 ms | 13 ms | fails | fails (16-model limit) | — | 183 MB |
| 68 | 2.8 / 4.2 ms | 21 ms | 27 ms | fails | fails (16-model limit) | — | 181 MB |
| 260 | 8.0 / 12.3 ms | 54 ms | 47 ms | fails | fails (16-model limit) | — | 157 MB |

Physics (SceneScaleBench: the ground plus copies of the 826-triangle two-floor hut, 600 ticks, replay in 15-command batches):

| Static meshes | Triangles | Scene load | Capture per tick | Live tick | Replay per 15 ticks (worst) |
|---:|---:|---:|---:|---:|---:|
| 1 | 12 | 0.18 ms | 0.5 µs | 6.7 µs | 0.24 ms (0.34) |
| 4 | 2490 | 7.3 ms | 0.8 µs | 6.0 µs | 0.24 ms (0.46) |
| 8 | 5794 | 22 ms | 1.1 µs | 6.4 µs | 0.34 ms (0.49) |
| 16 | 12402 | 35 ms | 1.4 µs | 5.9 µs | 0.44 ms (0.72) |
| 17 and up | | rejected: "Invalid history frame" | | | |

What the baseline shows:
- **Play stops working before 16 models.** With 16 placements, Prepare Play fails on the 1 MiB collision source limit: the
  world-space bake of about 14 huts is already more than 1 MiB of JSON. Above that, the 16-model limit rejects the bake.
- **Placing a new model takes 5.6 s** with only 8 placements, all spent in packaging, collision re-baking and the full resource
  reload. Prepare Play alone takes 3.4 s, and private memory goes from about 180 MB to 750 MB once Play resources are prepared.
- **Ordinary edits grow with scene size.** Placing an already-loaded model or moving a placement costs 10 ms at 8 placements and
  about 50 ms at 260, because every edit re-resolves the whole scene and rebuilds the authoring `World`. That is not collision.
  It belongs with phase 6, but phase 4 should avoid adding more per-edit whole-scene work.
- **Authoring frames grow with the draw loop** (1.3 ms → 8 ms p50 at 260 placements, one draw per mesh per placement, no
  instancing or culling). That is the render follow-up in phase 5.
- **Replay cost grows with static mesh count** (0.24 → 0.44 ms per reconcile from 1 to 16 meshes) because every replayed tick
  re-inserts every static body. Scene load costs about 2 ms per hut, because a Jolt mesh shape is built for each placement even
  when the model is the same. Both confirm phases 1 and 2: at thousands of placements, both would cost hundreds of
  milliseconds.

### Phase 1: per-model collision product

- Model cook (static-gltf importer, engine/assets/gltf_cooker.cpp) also emits a `model-collision` product: local-space positions
  welded exactly as `bake_model_collision` does now, triangles with material keys, signature. The model record references it, and
  `load_cooked_model` returns it with the model.
- Import option in the `.daimport` (`"collision": "mesh" | "none"`, default `mesh`; `box` and `convex` are possible later). Pickups,
  altars, doors and platforms keep their current non-static handling.
- Per-shape bound raised from 2048/4096 to a work bound (for example 65,536 triangles), validated at cook time, so authoring errors
  surface when the model is cooked, not when it is placed.
- `CollisionGeometry` gets built from the product once per model and is shared by every placement (Jolt `MeshShape`, with
  `ScaledShape`/body transform for the placement).
- Tests: cook determinism, weld equivalence with today's bake (same triangles once transformed), bound errors at cook time.

Implemented (15 October 2026):
- `cook_model_collision`, `encode_model_collision`, `decode_model_collision` and `load_cooked_model_collision`
  (engine/assets/model_collision.cpp). They share one weld with `bake_model_collision`, which keeps its old 2048/4096 bounds
  until phase 4 removes it.
- The product is binary (`DAMCOL01`, kind and extension `model-collision`), because a full-size shape does not fit the 1 MiB
  JSON bound. Its identity is `model_collision_id(model)`, derived from the model's ID, so no sidecar needed a new subasset.
- `model.json` carries `"collision"`: the product's ID, or null. `RuntimeModel::collision` exposes it.
- The importer honours the sidecar's `"collision": "mesh" | "none"`. Mask visuals and item world models fold the same import
  (`static_model_collision`), so their model products stay identical to the standalone cook.
- `validate_collision_mesh` now allows 65,536 vertices and 65,536 triangles per shape. A native `.dacollision` source keeps its
  own 2048/4096 work bound.
- Test: `M4.model_collision` (tests/model_collision_tests.cpp).

Moved to phase 2: building one shared `CollisionGeometry` per model. Nothing consumes the product until the static layer exists.

Verified: full CTest 111/111. `measure_scene_scale.py` hits the same limits as the baseline, as expected, since nothing uses the
product yet. With a warm cache at 8 models, placing a new model takes 6.2 s (5.6 s before) and Prepare Play 3.9 s (3.4 s before):
noise plus one more product per model. The first run after this change recooked every model (24 s for the place step), because
the cooker build hash changed.

### Phase 2: static collision layer in the physics world

- New `StaticCollisionLayer` (engine/runtime): built from `{id, shared CollisionGeometry, position, rotation, scale}`; validated;
  revision hash; body budget raised to a large static cap (for example 16,384) that is separate from the 64 boxes and actors.
- `PhysicsWorld::set_static_layer(std::shared_ptr<const StaticCollisionLayer>)` adds the bodies once. The editor query world also gets
  `add_static`/`move_static`/`remove_static` for incremental edits.
- `CollisionFrame` drops `meshes` and gains `static_revision`. `capture`, `load`, `CollisionHistory`, `update_prediction` and
  `replay_motor` stop handling meshes. Replay keeps one isolated replay world per owner prediction, holding the layer, and
  `load(frame)` only swaps boxes and actors. A frame whose `static_revision` differs from the world's layer is a
  `TopologyMismatch`, as a topology change is today.
- Hits on static bodies keep reporting the body id, subshape and material key. `valid_hit` checks the layer revision instead of the
  per-mesh generation.
- `load_scene(CollisionDefinition)` stays for hand-authored collision sources (the existing `collision` component profiles and
  `environment.dacollision`). Their meshes become static-layer bodies with an identity transform.
- Tests: the existing motor, replay, prediction, correction and collision tests, plus new ones: 2000 static bodies with replay
  cost independent of body count; a revision mismatch rejects replay; incremental add, move and remove in the query world.

### Phase 3: session and network

- Scene authority and clients (scene_authority*, scene_client, character_scene) build the static layer from the scene plan and
  cooked model shapes, instead of taking it from a baked `CollisionDefinition`.
- Handshake: the geometry identity hashed into `SessionHandshake` (now built in `prepare_character_scene`) becomes the layer
  revision. A guest whose scene or models differ fails at join, with a message that names the mismatch.
- `CollisionStreamFrame` drops mesh references (smaller snapshots) and carries `static_revision`. `prepare_collision_frame` no
  longer needs prepared geometry.
- Navigation (engine/runtime/navigation.cpp) builds its triangle soup from the static layer (shape triangles transformed per
  body), not from baked world-space meshes.
- Gates: party/GNS loopback, guest join with a mismatched scene, scene travel, navigation bake.

### Phase 4: editor without the scene bake

- Remove `bake_scene_collision`, `scene_collision_for`, `publish_scene_collision` and the `<scene>.dacollision` bake (including
  `scene_saved` baking), plus the stale-placement matching and the 16-model limit in `prepare_character_scene`. Existing
  generated `.dacollision` files next to scenes become unused. Delete them in the same change, after checking that nothing other
  than the bake references them.
- `SceneResources::prepare_model` loads straight from the asset service: cook that one model if it is not cooked or is stale, then
  load its product closure from the asset cache catalog (no Play package). `Shell::place` no longer calls
  `prepare_play_resources`.
- The placement-surface query world (apps/editor/viewport_tools.cpp) holds the static layer and updates it incrementally on place,
  move and delete, instead of being rebuilt from the baked definition.
- Play preparation packages once, at Play: the scene is the single package root and its models are dependencies (edges), so the
  64-root limit no longer depends on model count. Raise the product limit to a large work bound, and replace per-call registry parsing
  with a `RuntimeRegistry` that is parsed once and shared by all loads from that package.
- Raise the 63-model upload cap to a budgeted limit (count and bytes). Models no longer placed are kept until the budget is
  exceeded, then evicted least-recently-used. Placed models are never evicted.
- Gates: the editor placement and world-place gates, scene-editing exercise, Play from the editor, Save/Open round trip, scene
  travel, plus phase 0 numbers (placing a new model must not reload other models).

### Phase 5 (optional): merge on Save or Play

Only if phase 0 numbers show the broadphase, queries or memory need it: group static bodies by spatial cell and model, and build one
`StaticCompoundShape` per cell at Play preparation (deterministic, so host and guests get the same layer revision). The editor keeps
individual bodies so editing stays incremental. Rendering has a matching follow-up: instanced draws per model and frustum culling,
once scenes reach thousands of placements.

### Phase 6 (separate): document and object caps

1024 objects per scene, the 1 MiB scene document and the 65,536-node JSON work limit come next once collision is no longer the
limit. That needs its own plan: per-object records already exist in `.dascene.records/`, so the manifest can stop inlining
entities, and scene streaming or chunking is a later question.

## Migration and compatibility

- Wire and handshake versions change in phase 3 (new handshake string), so old and new builds never join each other.
- Cooked registries from before phase 1 lack the collision product. The asset cache recooks models on fingerprint change; the
  importer version bump in phase 1 forces that once.
- Scenes with a `collision` component profile keep working through `load_scene`, now as static-layer bodies.

