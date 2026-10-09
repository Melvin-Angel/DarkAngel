# Royal native static mesh collision

9 October 2026, following 58263ca. M4/M5 remain **In progress**.

The primary Royal character launcher now selects the existing native `.dacollision` asset for the ground and two huts. Collision comes from the verified RuntimeModel's already-normalized, node-flattened vertices and indexed material parts. The native exporter applies the same scale/rotation/translation and original pivot convention as the renderer. Original glTF/GLB, scenes and dependency pins are preserved.

The owned `content/royal_district/collision/environment.dacollision` uses existing schema 3, with three frozen mesh products. Geometry contains 265/457/8 welded vertices and 482/826/12 triangles. No Royal triangle was dropped as duplicate or degenerate. The general helper rejects bounds/index/part errors and filters only redundant geometric faces and degenerate triangles, reporting their counts. Root and mesh UUIDs remain stable across rebakes. Source publication validates the complete candidate, preserves unchanged bytes and atomically replaces a staged native file.

Native cooking and registry/CAS generation checks remain the existing collision pipeline. Collision roots now compose with the multi-root model/kit registry; the independent requested collision closure retains its geometry bounds and signatures. Runtime Play prepares the native geometry before collision ACK/control and uses the same WorldSession, immutable history and disposable owner replay. No raw glTF coordinates are parsed by the runtime collision path.

Before Play, the editor matches each static model's transformed geometry against one unambiguous prepared collision mesh. Missing, extra, moved or stale model/placement bindings reject before constructing gameplay and preserve authoring. Binding follows physical geometry rather than sorted placed IDs, so the original RoyalVillage scene can use the same geometry. The gameplay content signature includes collision geometry. Compatible model reload retains explicit player/target/collision references; geometry changes require a deliberate native rebake.

Rebake through the native command `AssetTool bake-scene-collision registry CAS scene output.dacollision collision-UUID excluded-skin-UUID mesh-UUID...`, using the existing source's root and mesh UUIDs. Then cook/package through `scripts/prepare_royal_scene.py`. The source and cooked models are the inputs; there is no second physics source format.

## Verification

63/63 complete optimized native tests, 16/16 affected Royal tests, 6/6 ordinary Headless, full-editor fixtures, D3D12/Vulkan combat and GNS compatibility pass. Run `python scripts/verify_royal_collision.py --build`. Evidence/source/binary hashes are recorded in `evidence/royal-collision.json`, with refreshed Royal, observer and owner-prediction receipts.

Native tests validate seam welding/duplicate and degenerate handling; renderer transform/pivot math; immutable source ownership; native source/CAS loading without source files; mixed registry closure; independent CPU triangle versus Jolt ray fractions/identities; ground normals/material IDs; real motor clearance; packaged combat/owner correction; stale binding rejection before Play; unchanged authoring; and the original scene's reordered identities. An actual hut corner traversal reaches x=-4.39231 with the precise mesh versus x=-11.19 with the former solid bounds proxy.

The tested hut entrances contain closed cooked geometry. Collider generation preserves those surfaces. Door opening/state/interaction and additional authored collision policies remain future work. This verifies the existing static model shape; it does not invent doorway holes or claim a new interior gameplay workflow.

## Remaining gates

Native effects/tags/reservations and Burn/slow/invulnerability/stagger/equipment/cleanup/credit/Luau composition are next. Authored combos/projectiles, typed graph states/events/layers/masks, animated socket sweeps and reflected Character/Ability/CombatKit/Composer authoring remain open. Full clock/jitter/lead/redundancy, RTT/loss/fault/streaming/lifecycle and bandwidth/allocation/performance gates remain required. The precise Royal scene profile is static; broader moving/streamed content qualification is separate. Public effects/tags/cues and external-provider graphical scene/late-join workflows remain open. M3 live EOS is externally blocked. M6 has not started.
