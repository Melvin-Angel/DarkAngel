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
