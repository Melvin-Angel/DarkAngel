# Scene scale: per-model collision and incremental placement

Status (15 October 2026): phases 0 to 5 done (3 with 3b); architecture revised the same day (design rules below); phase 6
(instancing) is designed below, and phase 7 (document caps) is next. Builds on cf818c2, fb4b01e and f2d641b, which fixed three
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

Revised 15 October 2026 after reviewing `PhysicsWorld` (engine/runtime/character_motor.cpp), the scene client and authority, the
collision wire encoding (world_session_packet.hpp), the observer, and the editor renderer.

- **Collision is part of the model.** Cooking a model also produces a local-space collision shape for it (welded, validated,
  deterministic; done in phase 1). A placement is a body: that shape plus the placement's transform and uniform scale. Nothing is
  baked per scene, and moving a placement moves a body without rebuilding triangles.
- **Static geometry is an immutable static layer, separate from tick state.** A `StaticCollisionLayer` is an immutable,
  reference-counted snapshot of `{body id, scene object id, shared geometry, position, rotation, scale}`, sorted by body id, with a
  content revision. `PhysicsWorld` points at one layer. `CollisionFrame` carries a pointer to the layer it was captured with, not a
  copy of its bodies, and `CollisionStreamFrame` carries only the layer's revision. History and replay never copy or rebuild static
  bodies.
- **Every participant derives the same layer from the scene.** Host, guests, prediction and replay worlds build or share the same
  layer from the scene's placements and the cooked model shapes. The session handshake and every collision snapshot compare
  revisions instead of shipping geometry references.
- **Editor placement is incremental.** Placing a model cooks that one model if needed, loads it directly from the asset cache,
  uploads only it to the GPU, and changes one body in the editor's query world. Packaging and Play preparation happen only at Play
  (and travel).
- **Rendering scales separately.** Frustum culling and draw-state batching come first; GPU instancing follows as its own phase,
  because it changes the prop shader contract (see phases 5 and 6).

### Design rules

**Stable identity.**
- A static body's identity is a 64-bit id derived from the scene object's `StableId`:
  `(first 62 bits of sha256("darkangel.static-body/" + object)) | 2^62`.
- Bit 62 marks static scene bodies. Moving boxes use small ids or `2^40 + index` (scene_world_objects.cpp), characters set bit 63
  in Jolt user data, and hand-authored collision meshes keep their authored ids (1001, ...). The three spaces cannot collide, and a
  hash collision between two placements fails the layer build.
- Jolt `BodyID`s are allocation slots and never leave `PhysicsWorld`. Hits, supports, sensors, history and the wire all use the
  body id. `StaticCollisionLayer::find(id)` maps a hit back to the scene object (`StableId`) and its model.
- Hit validity (`valid_hit`) stays per world: world id, tick, topology, and a per-world body generation.

**Deterministic revisions.**
- The revision is sha256 over a canonical byte encoding, in body-id order:
  - the body id and scene object id;
  - the geometry's runtime asset id and content signature;
  - position, rotation and scale, as the IEEE-754 bit patterns of the doubles, written as fixed-width hex.
- Rotations are normalized and sign-canonicalized (`w >= 0`; when `w == 0`, the first non-zero component positive), so `q` and `-q`
  give the same revision.
- No struct bytes, padding, pointer values or container iteration order enter the hash. Bodies are sorted before hashing, so
  insertion order never matters.
- Only collision inputs are hashed. Renaming an object, changing its material or tint, or editing a model with `"collision": "none"`
  does not change the revision. Moving or scaling a colliding placement does.

**Ownership.**
- A layer is immutable once built and shared by `std::shared_ptr<const StaticCollisionLayer>`. Simulation, prediction and replay
  worlds only ever see a complete layer, so they can never observe a half-edited scene.
- The editor's authoring state stays in the document. A new layer is built from the document when a collision-relevant edit
  commits, not per mouse move: the gizmo draft is display-only until release.
  - The build copies body records (shared geometry pointers, about 100 bytes each) and rehashes them, in O(N) time; geometry is
    never rebuilt.
  - Geometry comes from a `CollisionGeometryCache` keyed by runtime id and signature, so N placements of one model share one Jolt
    `MeshShape`.
- `PhysicsWorld::set_static_layer(next)` diffs the old and new layers by body id in one merge walk, and touches only bodies that
  were added, removed, moved, rescaled or had their geometry swapped. Unchanged bodies keep their Jolt body and generation. A
  full-layer install (first load, scene change) adds bodies in one batch and optimizes the broadphase once; small diffs never
  trigger a broadphase optimize.
- Publication: the Play session takes the layer that exists when Play is prepared. Static collision does not change during a Play
  session; a scene edit stops Play, as today. Scene travel builds the destination's layer before the swap (scene_transition), and a
  failed build leaves the current scene running.

**Prediction and reconciliation.**
- A layer change is a topology change: `set_static_layer` with a different revision increments the world's topology, so every
  existing topology check keeps guarding it.
- `load(frame)` clears and reinserts only boxes and historical actors. It changes static bodies only when the frame's layer has a
  different revision from the world's, which never happens inside one topology.
- `OwnerPrediction` keeps one persistent replay world (`PhysicsWorld::Mode::Replay`) holding the static layer. `replay_motor` gains
  an overload that reuses it, so a reconcile inserts no static bodies at all. The old overload (a temporary world per call) remains
  for callers that replay once.
- A revision mismatch fails with a diagnostic naming both revisions (prediction world against frame, guest against host). Replay
  returns `TopologyMismatch`.

**Jolt specifics.**
- Shapes are reference-counted (`RefConst<Shape>`) and immutable after creation. Sharing one `MeshShape` across bodies and across
  `PhysicsSystem`s is supported. A layer holds `shared_ptr<const CollisionGeometry>`, which holds the shape's `RefConst`, so every
  world using the layer keeps the shape alive.
- Uniform scale only: `scale` in (0, 1000]. Scale 1 uses the mesh shape directly; any other scale wraps it in a `ScaledShape`,
  cached per (geometry, scale) in the layer. Negative, zero and non-uniform scales are rejected when the layer is built (`Transform`
  has one scale).
- `ScaledShape` is a decorated shape: it passes sub-shape ids through unchanged, so `GetTriangleUserData` and `GetMaterialIndex` on
  the inner `MeshShape` still give the authored triangle key and material index.
- Static bodies use object layer 0 (static) and keep the existing layer pair filters. Queries classify them as `StaticCollision`.
- `PhysicsSystem::Init` is raised from 256 to 16,640 bodies: 16,384 static, 64 moving boxes, 2 × 33 character proxies, and a
  margin. The static budget and the 64-box moving budget are counted separately.

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
  instancing or culling). That is phases 5 and 6.
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

- `StaticCollisionLayer`, `StaticCollisionBody`, `CollisionGeometryCache` and `static_body_id(StableId)`
  (engine/runtime/static_collision.cpp, in DarkAngelPhysics). Built per the design rules above.
- `PhysicsWorld`:
  - `set_static_layer`, `static_layer()`, and static bodies outside `clear()`.
  - The 16 static-mesh and 64 boxes-plus-meshes checks become a 16,384-static budget plus a 64-box budget.
  - `load_scene(CollisionDefinition)` turns hand-authored meshes into static-layer bodies at identity.
  - `remove(id)` on a static body installs a layer without it.
  - `add_mesh` is removed: it had no callers.
- `CollisionFrame` holds `std::shared_ptr<const StaticCollisionLayer> statics` instead of `meshes`. `CollisionStreamFrame` holds
  `static_revision` instead of mesh references.
  - The wire item kind 3 becomes the revision: 64 characters, sent once per snapshot.
  - `prepare_collision_frame(stream, layer)` checks the revision.
  - The observer compares revisions and stops emitting identity poses for static meshes.
- `CollisionHistory` and `replay_motor` compare layer revisions, not per-mesh signature maps. `OwnerPrediction` holds a persistent
  replay world.
- Acceptance:
  - SceneScaleBench at 100, 500, 2,000 and 16,384 placements of repeated models, plus a scene of many unique models.
  - Unique shape builds equal the number of distinct models, not placements.
  - Replay inserts zero static bodies, measured by a world counter. Per-reconcile cost no longer grows with the static body count
    beyond query effects.
  - 16,384 bodies load. 16,385 fail with a clear budget error.
  - All existing motor, replay, prediction, correction, collision stream and observer tests pass, updated where they asserted mesh
    lists.
- New tests: layer revision determinism (insertion order, quaternion sign, visual-only fields excluded), diff-based
  `set_static_layer` (unchanged bodies keep their generations), shared shapes, scaled bodies hit with the right triangle key and
  material, a revision mismatch in `update_prediction`/replay, and the budget.

Implemented (15 October 2026):
- engine/runtime/static_collision.cpp: `StaticCollisionLayer`, `CollisionGeometryCache`, `static_body_id`, `placement_rotation`.
- `PhysicsWorld` diff install. `CollisionFrame::statics` and `CollisionStreamFrame::static_revision`.
- Wire item 3 is the revision. The observer compares revisions.
- Persistent `OwnerPrediction` replay world, and the `replay_motor(..., PhysicsWorld&)` overload.
- Jolt `Init` sized for 16,640 bodies.
- `add_mesh` removed.
- Test: `M4.static_collision`. Collision mesh, stream and observer tests now assert layer revisions instead of mesh lists.

Results (SceneScaleBench, 600 ticks, replay in 15-command batches; "models" is distinct collision shapes):

| Placements (models) | Layer build | Shapes built | Install | Move one (diff) | Live tick | Replay, persistent world (worst) | Replay, fresh world (old cost) | Static inserts in replay after the first | Private memory |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 (1) | 12 ms | 2 | 0.1 ms | 0.03 ms | 12.6 µs | 0.26 ms (3.1) | 0.64 ms | 0 | +9 MB |
| 100 (100) | 470 ms | 101 | 0.1 ms | 0.03 ms | 6.7 µs | 0.16 ms (1.6) | 0.44 ms | 0 | +15 MB |
| 500 (1) | 7 ms | 2 | 0.4 ms | 0.12 ms | 6.1 µs | 0.12 ms (0.5) | 0.81 ms | 0 | +8 MB |
| 500 (200) | 835 ms | 201 | 0.4 ms | 0.7 ms | 7.3 µs | 0.16 ms (0.8) | 0.78 ms | 0 | +21 MB |
| 2,000 (1) | 17 ms | 2 | 1.6 ms | 0.7 ms | 8.7 µs | 0.19 ms (2.3) | 2.26 ms | 0 | +13 MB |
| 2,000 (200) | 788 ms | 201 | 2.6 ms | 3.6 ms | 7.3 µs | 0.18 ms (1.7) | 2.52 ms | 0 | +21 MB |
| 16,383 + ground (1) | 120 ms | 2 | 15.5 ms | 4.7 ms | 9.9 µs | 0.60 ms (12.9) | 28.7 ms | 0 | +18 MB |
| 16,383 + ground (200) | 1,455 ms | 201 | 21.8 ms | 6.5 ms | 8.2 µs | 0.61 ms (17.2) | 22.8 ms | 0 | +27 MB |
| 16,385 bodies | rejected: "Static collision body budget (16384) exceeded" | | | | | | | | |

What changed against the baseline:
- The 16-mesh and 64-body limits are gone for static geometry. 16,384 static bodies install in 16 to 22 ms.
- Shapes are built once per distinct model, not per placement.
- A reconcile no longer re-inserts static bodies: zero inserts after the first replay. Replay at 16k bodies costs 0.6 ms on average,
  against 28.7 ms when every replay builds a fresh world, which was the old architecture's per-reconcile cost. The worst replay
  (13 to 17 ms at 16k) is the first one, which installs the layer once.
- Layer build time with many unique models is dominated by Jolt mesh-shape construction (about 4 ms per 826-triangle shape). That
  is paid once per model, at Play preparation.
- Moving one body by diff costs 4.7 ms at 16k bodies. The diff rebuilds the id-to-body map, which is O(N). A later refinement could
  diff in place.

Verification: `M4.static_collision` and the collision, motor, session, prediction and correction tests pass. Full CTest passed apart from
timeouts: `M5.native_authoring` took 683 s against 436 s in phase 1, and `M5.actor_assets` exceeded its 94 s limit.
- Every test ran 1.2 to 1.6x slower in that run, including tests that never touch physics (`M5.clips`, `M5.canonical_rig`,
  `M4.model_collision`). The machine was loaded by other desktop applications.
- Controlled A/B runs were inconclusive: the no-physics control itself varied from 2.0 to 4.2 s. Restoring the 256-body Jolt
  capacity, or using a fresh replay world per reconcile, made no measurable difference.
- `NativeAuthoringTests --fixture-only` passes when run alone. Session-test timing is an open measurement, to repeat on an idle
  machine.

### Phase 3: scene-derived layers, session and navigation

- The editor builds the layer from the scene plan:
  - every model placement except the player's skin, pickups, altars, doors and platforms;
  - its cooked `model-collision` product, through `load_cooked_model_collision`;
  - the placement transform.
  The function is `scene_static_collision(plan, world, skin, load)` in apps/editor/scene_collision.cpp.
- A scene `collision` component profile (hand-authored) adds its bodies to the same layer.
- `CollisionDefinition` gains `std::shared_ptr<const StaticCollisionLayer> statics`, so sessions, the placement query world and
  navigation receive one object.
- `prepare_character_scene`:
  - drops the per-placement bake, the stale matching and the AABB box proxies;
  - hashes the layer revision into the handshake, replacing the per-mesh geometry identity string.
  A guest whose scene or cooked models differ fails at join with "static collision revision mismatch".
- Navigation reads the layer: each body's shape triangles, transformed by its pose.
- Acceptance:
  - The Royal scenes play with model collision.
  - Party/GNS loopback and the scene-travel gates pass.
  - Navigation bakes from the layer.
  - A scene with 2,000 placements prepares Play.

### Phase 4: editor without the scene bake

- Remove `bake_scene_collision`, `scene_collision_for`, `publish_scene_collision`, the `<scene>.dacollision` bake (including
  `scene_saved` baking) and `bake_model_collision`.
- Generated `.dacollision` files next to scenes become unused. Delete them in the same change, after checking that nothing other
  than the bake referenced them. The user's untracked RoyalCombat.dacollision is left alone; it is a user file.
- `SceneResources::prepare_model` loads straight from the asset service: cook that one model if it is not cooked or is stale, then
  package and load its closure on its own. `Shell::place` no longer calls `prepare_play_resources`.
- The placement-surface query world (apps/editor/viewport_tools.cpp) keeps a layer and calls `set_static_layer` with the diff when
  the document's collision changes, instead of rebuilding.
- Play preparation packages once, at Play. Package limits rise to work bounds (roots and products), and the registry is parsed
  once per load batch instead of once per model.
- The 63-model upload cap becomes a budget.
- Acceptance:
  - Placing a model the scene has not used costs about one model cook and upload, independent of scene size.
  - Moving a placement does not touch other bodies.
  - Prepare Play at 2,000 placements completes.
  - Editor gates pass.

#### Phases 3, 3b and 4 as implemented (15 October 2026)

Phases 3 and 4 share their editor files (play_controller, editor_app), so they land in one commit, verified together.

Phase 3: scene-derived layers.
- `scene_static_collision(plan, world, skin, geometry)` (engine/runtime/static_collision.cpp) makes one body per static model
  placement.
- `CharacterPreviewResources::model_collision` loads each model's cooked shape once, through one cache, from the package the
  resources were loaded from.
- Rule: authored collision (a scene `collision` profile, or `--character-collision`) is the scene's whole static collision when
  present, so hand-authored workflows and the gates' fixed collision are unchanged. Without it, Play uses the placements' layer.
  `scripts/open_royal_scene.ps1` and the scale script no longer pass authored collision, so your Royal scenes now use model
  collision. The launcher re-prepares `.cache/royal-scene` once if its registry has no `model-collision` products.
- `prepare_character_scene`:
  - drops the per-placement bake check, the AABB box proxies and its `RuntimeModel` loader;
  - hashes the layer revision into the handshake (string `... static1`).
  Guests build the same layer from the same scene and models.
- Removed: apps/editor/scene_collision.*, `bake_scene_collision`, `scene_collision_for`, the `scene_saved` rebake and the
  `<scene>.dacollision` it wrote. `bake_model_collision` remains as the reference weld that `M4.model_collision` compares
  against. The untracked RoyalCombat.dacollision is a user file and was left alone.
- `PlayController::static_collision()` gives viewport placement (installed by layer diff) and navigation the open scene's effective
  collision, rebuilt per document revision from shared shapes. Navigation rasterizes each body's shape at its pose.
- Assembly sources and spawn plans are parsed with the document's 1 MiB bound instead of 128 KB (about 460 placements).

Phase 3b: static scenery stays local.
- Play replicated every scene object, and `SceneAuthority` caps a scene at 128 objects.
- `static_scenery(plan, object, skin)` (assembly) marks placed models with no gameplay role: not the skin, no gameplay
  component, no script binding.
- `prepare_character_scene` leaves scenery out of the session objects. The viewport draws scenery in Play from the scene's own
  world.

Phase 4: incremental placement and package bounds.
- `Shell::place` no longer prepares Play. A model outside the Play package is cooked and packaged alone (`Controller::package_model`)
  and only that model is uploaded (`SceneResources::prepare_model(id, registry, cas)`). Its collision joins the viewport's layer
  from that package.
- Package bounds: 4,096 roots and 16,384 products (`runtime_package_root_limit`, `runtime_package_product_limit` in assets.hpp).
  The 17 cooked-asset loaders read registries through `registry_json`, which parses a registry once per file revision; they used
  to parse the whole manifest on every load, under a 512-product and 1 MiB cap.
- The 63-model upload cap is now 4,096 distinct models.
- Prepare Play reports its own breakdown in the console ("N roots; package X ms, resources Y ms").

Results (hidden editor, D3D12, warm cache; the machine was loaded):

| Models | Authoring frame p50/p95 | Place used | Move | Place new | Prepare Play | Play frame p50/p95 | Private MB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 (baseline) | 1.3 / 2.0 ms | 10 ms | 8 ms | 5,594 ms | 3,400 ms | 1.4 / 2.4 ms | 751 |
| 8 | 3.0 / 5.9 ms | 22 ms | 13 ms | **220 ms** | 4,217 ms | 2.2 / 3.5 ms | 730 |
| 68 (phase 3) | 4.4 / 7.1 ms | 59 ms | 22 ms | 4,237 ms | 4,206 ms | 5.9 / 7.8 ms | 730 |
| 260 (phase 3b) | 13.5 / 29.3 ms | 353 ms | 132 ms | 5,223 ms | 5,069 ms | 17.2 / 24.2 ms | 680 |
| 1,004 | 60.8 / 74.8 ms | 633 ms | 393 ms | **725 ms** | 6,226 ms (package 3.0 s, resources 3.2 s) | 53.0 / 64.9 ms | 625 |

- Play prepares and runs with 1,004 placements. The baseline failed at about 14.
- Placing a new model no longer reloads the scene: 5.6 s down to 0.22 s at 8 placements. At 1,004 placements the 0.7 s is the
  per-edit whole-document cost that "place used" also shows (phase 7).
- The first run after a cooker change recooks every model (one run took 42 s to prepare Play).

Remaining risks and limits:
- 2,000+ placements are blocked by the 1,024-object scene cap (phase 7). Phase 3's 2,000-placement acceptance moves there.
- Prepare Play still rescans the project once per root and reloads every resource: about 6 s at this scale.
- Authoring and Play frames at 1,004 placements are about 55 to 60 ms, one draw per mesh part per placement (phases 5 and 6).
- Full CTest: all pass except `M5.actor_assets` and `M5.attribute_authoring`, which hit their fixed 90 s CTest limit.
  `ActorAssetTests` passes when run alone, in 80 to 101 s. A no-physics control (`M5.clips`) ran 2.3x slower at the same time
  (4.5 s against 1.98 s), so the machine was loaded. Scaled by that, the actor run (about 44 s) matches phase 1's 48 s.

### Phase 5: frustum culling and draw batching

- Cull each placement against the view frustum using its model's bounding sphere before submitting draws.
- Sort visible draws by pipeline, model, mesh and material, so vertex buffers, pipeline state and material bindings are set once
  per run, not once per part. Keep `DRAW_FLAG_VERIFY_ALL` out of release builds.
- Instrumentation, reported by `--exercise-scale`: draws submitted, placements visible and culled.
- Acceptance:
  - Authoring frame time at 260 placements improves on the 8 ms baseline.
  - Draw count scales with visible placements, not total placements.
  - Picking, selection and capture gates are unchanged, because culling changes only what is submitted.

Implemented (15 October 2026):
- **Frustum culling.** A static placement whose bounding sphere (from model bounds, scaled) is outside the view is not submitted.
  Skinned models always draw, because they animate past their rest bounds.
- **Draw-state reuse.** Pipeline, material binding and vertex/index buffers are set only when they change between consecutive
  draws. The Frame constant buffer is dynamic, so per-draw constants need no re-commit. The worn-mask block resets the trackers.
  `DRAW_FLAG_VERIFY_ALL` is set only in debug builds.
- **Per-frame document fingerprint.** The real bottleneck, which the breakdown below found: the toolbar called `EditorDocument::dirty()` every
  frame, and that re-parsed, re-serialized and hashed the whole scene document. The digest is now cached per document revision.
- The World Outliner builds only the rows in view (`ImGuiListClipper`).
- Instrumentation:
  - `Shell::scene_visible`, `scene_culled` and `scene_draws` are recorded per frame.
  - `Shell::frame_sections` times exercises and update, UI, Play, scene render, effects and present.
  - Both are reported by `--exercise-scale` as `authoring_draws`, `play_draws` and `*_sections_ms`.
  - `measure_scene_scale.py --layout view` packs the placements in front of the authoring camera for the worst case.

Results (hidden editor, D3D12):

| Scene | Authoring frame p50/p95 | Play frame p50/p95 | Draws / visible / culled | Scene render | UI |
|---|---:|---:|---:|---:|---:|
| 1,004 placements, before (phase 4) | 60.8 / 74.8 ms | 53.0 / 64.9 ms | 1 draw per mesh part, none culled | | |
| 1,004 spread (authoring camera sees 10) | **2.0 / 5.1 ms** | **1.8 / 2.7 ms** | 36 / 10 / 995 | 1.1 ms | 0.6 ms |
| 260 in view | 2.2 / 3.5 ms | 2.6 / 3.7 ms | 287 / 261 / 0 | | |
| 1,004 in view (worst case) | 5.9 / 9.6 ms | 7.3 / 11.5 ms | 840 / 814 / 191 | 4.9 ms | 0.6 ms |

At 1,004 placements, frame time went from 61 ms to 2 ms with a typical view, and to 6 ms with everything in view. Draw calls now
scale with visible placements, at about 6 µs per draw. Placing a used model and moving one (250 to 290 ms at 1,004) remain
whole-document edits (phase 7).

### Phase 6: GPU instancing

Instancing changes the prop shader contract:
- The editor's built-in shader and games/AshenRoots/shaders/stylized_prop.hlsl take the world matrix and normal rotation from a
  per-instance structured buffer (`SV_InstanceID`) instead of the `Frame` constant buffer.
- An instanced pipeline variant is built next to the existing one.
- One `DrawIndexed` per (model, mesh part, material) carries all visible instances.
- Skinned characters, worn masks and the selected or dragged placement stay on the existing per-object path.

Phase 6 depends on phase 5's visible lists and must keep the pixel-exact renderer gates (`verify_royal_renderer.py`) passing. If
those gates cannot run (Pillow is not installed), the phase stops at a design and a measured prototype rather than shipping
unverified.

Decision (15 October 2026): designed, not implemented in this refactor.
- Phase 5 measured the need: with all 1,004 placements in front of the camera, 840 draws cost 4.9 ms (about 6 µs per draw) and
  the frame takes 6 ms. Instancing pays off at several thousand visible placements, not at today's scenes.
- The change touches the shader contract shared with the game (`stylized_prop.hlsl`) and the reload-shader path. Its only
  pixel-exact check, `verify_royal_renderer.py`, needs Pillow, which this machine does not have.
- It would therefore ship unverified, and collision and editor scalability did not depend on it.

Implementation plan for the follow-up:
1. **Instance data.** `struct Instance {row_major float4x4 World; row_major float4x4 Rotation; float4 Tint;}` in a
   `StructuredBuffer<Instance> Instances` (dynamic, ring-allocated per frame). `Frame` keeps `ViewProjection` and `Surface`.
   `VSMain` reads `Instances[base + SV_InstanceID]`. The base offset comes from a root constant or a small per-draw cbuffer.
2. **Shader sources.** Derive the instanced variant from the prop shader text by the same replace-based approach `skin_shader()`
   uses, and require the game shader to declare the contract (a compile-time check like the existing "Frame resource contract"
   check). Keep the non-instanced pipeline for skins, worn masks, the dragged placement and the VFX preview.
3. **Batching.** Group phase 5's visible static placements by (model, mesh, part material, pipeline). Upload their instance
   records once per frame, then issue one `DrawIndexed` with `NumInstances` per group. Sort groups by pipeline and material so
   bindings change once per group.
4. **Identity.** Picking stays CPU-side (model bounds against placements), and selection and gizmos draw the selected placement
   on the per-object path, so object identity is untouched. Collision and serialization never see render groups.
5. **Acceptance.**
   - `measure_scene_scale.py --layout view` at 1,000 and 4,000 placements: draw count equals the number of groups (here about 30),
     and scene render time stays flat as placements grow.
   - Pixel comparison (`verify_royal_renderer.py`, Pillow installed) matches the non-instanced path exactly, or within the
     documented float tolerance of the instanced transform.

### Phase 7 (separate): document and object caps

1024 objects per scene, the 1 MiB scene document and the 65,536-node JSON work limit come next once collision is no longer the
limit. Phase 0 shows each edit re-resolving the whole scene (10 ms at 8 placements, 50 ms at 260). That needs its own plan:
per-object records already exist in `.dascene.records/`, so the manifest can stop inlining entities, and edits can resolve only
the changed object. Optional static merging per spatial cell (`StaticCompoundShape` at Play preparation) belongs here too, only
if measurements show the broadphase needs it.

## Migration and compatibility

- The collision wire item and the handshake change in phases 2 and 3, so old and new builds never join each other: the handshake
  string changes.
- Cooked registries from before phase 1 lack the collision product. The asset cache recooks models when the cooker build hash
  changes.
- Scenes with a `collision` component profile keep working through `load_scene`, now as static-layer bodies.
