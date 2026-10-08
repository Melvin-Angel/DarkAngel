# Royal District playable scene target

User-authorized M4/M5 integration target: saved native scene with TempleIslandTerrain ground tiles, Mud Huts, a canonical skinned human with omnidirectional locomotion and one Action Composer attack. Keep the original Unity asset folders read-only. M6 and live EOS are not prerequisites for the local/Loopback/GNS slice; their milestone gates remain separate.

Current source checkpoint:

- All 6 Mud Huts and 48 TempleIslandTerrain FBXs have owned glTF/geometry-buffer sources under `content/royal_district/static`.
- Original trim UVs and pivots are preserved. Two exact copied atlases are shared per pack. The native PBR material uses the supplied base color; Unity's custom shader is not reproduced.
- Original FBX/texture, derived glTF/buffer and native registry hashes are recorded in `static/conversion.json` and [native receipts](evidence/royal-assets.json).
- Every model passes native adoption, cook, unchanged warm cook, frozen package and runtime inspection. Source UUIDs/subasset UUIDs live in the standard `.daimport` sidecars.
- Ground_16x16 and Ground_32x32 have top surfaces at 0.5 m. Huts retain base pivots around y=0 and are 6-8 m wide. Place huts and player on the tile surface, rather than centering each model during rendering.

Reproduce conversion with Blender 4.5.1:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 4.5\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/convert_royal_district.py -- 'C:\Unity Projects\AshenRootsMP\Assets\thirdparty\3D\Royal District' content/royal_district/static
python scripts/prepare_royal_assets.py
```

`AssetTool adopt-many` performs a bounded explicit batch before catalog scan. A partial adoption preserves assigned metadata/UUIDs and can resume. Existing single-source commands remain available.

Required integration work remains: native scene resource loading for multiple model assets, saved placements and collision; canonical complete human skin with render attributes and GPU skinning; fixed-tick Play input/camera/motor integration; movement-driven locomotion and action pose/root/socket bindings; one native authoritative attack with costs/cooldown, collision-hit ownership and presentation deduplication. Authoring must use the current asset/cook and EditorDocument services. Follow the full DAE contracts and acceptance gates; an attractive screenshot does not qualify combat/network behavior. M4/M5 remain In progress and M3 live EOS remains Blocked.


Royal scene renderable human checkpoint: explicit opt-in human-gltf-v2 keeps the legacy deformation fixture profile unchanged. Complete original14-part canonical human and supplied atlas are owned sources under content/royal_district/character. The new frozen closure has19 products, binary DASKIN02 vertex/index payloads with position/normal/UV/four joints/four weights, shared DDS atlas and the existing canonical rig/archive producer. Runtime verifies bounds, types, digests, signatures and indices. Modular bind reference passes7286 vertices with maximum2.99e-7m error; failed reimport preserves the full closure and duplicate registry IDs reject. Reproduce python scripts/verify_renderable_human.py. Optimized45/45 and affected no-animation3/3 pass; original FBX/atlas hashes unchanged. GPU rendering/visual quality, player Play integration, graph/action/ability/authority and broader M4/M5 gates remain open. Existing GNS receipts are unchanged; EOS blocked; M6 not started.


Royal native renderer checkpoint: m5-editor-relwithdebinfo enables the existing Diligent editor with frozen Jolt/Ozz. Multi-model scene validation prepares each required GPU asset before publishing an opened document; resource reload validates/uploads all cached models and skins before publication. Scene mode preserves source pivots. The four-object RoyalVillage includes ground, two huts and the complete14-part human. Canonical four-influence GPU skinning (256-joint palette cap) exactly matches CPU reference viewport pixels for rest and attack tick18 on D3D12 and Vulkan. Native Play/Step isolation, undo/redo, save/open, resource reload and failed shader preservation pass. Source attack is frozen in owned content with standard clip metadata; the five-root closure has33 products. Reproduce python scripts/verify_royal_renderer.py. Vulkan Khronos validation layer is unavailable; Diligent checks enabled. The pose diagnostic is not a live gameplay attack; interactive motor/graph/action/ability integration and broader M4/M5 gates remain open. Launch scripts/open_royal_scene.ps1, optionally -CloseView -AttackPose. M3 live EOS remains blocked; M6 not started.
