"""Import the Unity Royal Crown Temple module export as native DarkAngel assets and scene."""
from __future__ import annotations

import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
SOURCE_EXPORT = ROOT / "artifacts/royal-crown-source-export-20261018"
UNITY_TEXTURE_ROOT = Path(r"C:\Unity Projects\AshenRootsMP\Assets\thirdparty\3D")
EXPORTED_TEXTURE_ROOT = SOURCE_EXPORT / "textures"
SOURCE = ROOT / "content"
DEST = SOURCE / "royal_crown/models"
SCENE = SOURCE / "royal_crown/RoyalCrownTemple.dascene"
TOOL = ROOT / "build/m5-editor-relwithdebinfo/AssetTool.exe"
CACHE = ROOT / ".cache/royal-crown-temple"


def run(*args: object) -> str:
    result = subprocess.run([str(TOOL), *map(str, args)], cwd=ROOT,
                            capture_output=True, text=True, timeout=900)
    if result.returncode:
        raise RuntimeError(f"AssetTool {' '.join(map(str, args))} failed:\n{result.stderr}{result.stdout}")
    return result.stdout.strip()


def guid(name: str) -> str:
    del name
    return "a0e2a2d8-3ff0-4e59-b58a-9589504fdd50"


def asset_id(path: Path) -> str:
    sidecar = json.loads(Path(str(path) + ".daimport").read_text(encoding="utf-8"))
    return sidecar["id"]


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, ensure_ascii=False) + "\n"
    path.write_text(payload, encoding="utf-8", newline="\n")


def add_new_texture_keys(path: Path) -> None:
    sidecar_path = Path(str(path) + ".daimport")
    if not sidecar_path.is_file():
        return
    original = sidecar_path.read_text(encoding="utf-8")
    sidecar = json.loads(original)
    glb = path.read_bytes()
    json_size = struct.unpack_from("<I", glb, 12)[0]
    document = json.loads(glb[20:20 + json_size])
    for material in document.get("materials", []):
        if material.get("emissiveTexture"):
            slot = material.get("extras", {}).get("darkangel_key")
            if slot:
                sidecar.setdefault("subassets", {}).setdefault(
                    f"texture/emissive/{slot}",
                    str(uuid.uuid5(uuid.NAMESPACE_URL, f"darkangel:{path.stem}:{slot}")))
    payload = json.dumps(sidecar, indent=2, ensure_ascii=False) + "\n"
    if payload != original:
        sidecar_path.write_text(payload, encoding="utf-8", newline="\n")


def add_stable_subasset_keys(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF":
        raise SystemExit(f"Invalid GLB container: {path}")
    version, total, json_size, json_kind = struct.unpack_from("<IIII", data, 4)
    if version != 2 or total != len(data) or json_kind != 0x4E4F534A:
        raise SystemExit(f"Unsupported GLB profile: {path}")
    document = json.loads(data[20:20 + json_size])
    for sampler in document.get("samplers", []):
        sampler["magFilter"] = 9729  # LINEAR
        sampler["minFilter"] = 9987  # LINEAR_MIPMAP_LINEAR
        sampler["wrapS"] = 10497     # REPEAT
        sampler["wrapT"] = 10497
    binary_header = 20 + json_size
    binary_size, binary_kind = struct.unpack_from("<II", data, binary_header)
    if binary_kind != 0x004E4942:
        raise SystemExit(f"Missing GLB binary chunk: {path}")
    binary = bytearray(data[binary_header + 8:binary_header + 8 + binary_size])

    # GLTFast exported several Unity atlases as all-black JPEGs even though
    # the source Texture2D assets are intact. Resolve the exported image name
    # back to its Unity asset and embed those original bytes before DarkAngel
    # cooks the texture products. Refuse ambiguous same-name assets unless
    # their bytes are identical.
    if not UNITY_TEXTURE_ROOT.is_dir():
        raise SystemExit(f"Unity texture source folder is missing: {UNITY_TEXTURE_ROOT}")
    texture_files: dict[str, list[Path]] = {}
    for candidate in UNITY_TEXTURE_ROOT.rglob("*"):
        if candidate.is_file() and candidate.suffix.lower() in {".png", ".jpg", ".jpeg"}:
            texture_files.setdefault(candidate.stem.casefold(), []).append(candidate)
    for image in document.get("images", []):
        image_name = Path(image.get("name", "")).stem.casefold()
        candidates = texture_files.get(image_name, [])
        if not candidates:
            fallback = EXPORTED_TEXTURE_ROOT / f"{Path(image.get('name', '')).stem}.png"
            if fallback.is_file():
                candidates = [fallback]
        if not candidates or "bufferView" not in image:
            continue
        source_bytes = candidates[0].read_bytes()
        if any(other.read_bytes() != source_bytes for other in candidates[1:]):
            raise SystemExit(f"Ambiguous Unity texture name {image.get('name')}: {candidates}")
        current_view = document["bufferViews"][image["bufferView"]]
        current_offset = current_view.get("byteOffset", 0)
        current_bytes = bytes(binary[current_offset:current_offset + current_view["byteLength"]])
        if current_bytes == source_bytes:
            continue
        extension = candidates[0].suffix.lower()
        mime_type = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}[extension]
        binary.extend(b"\0" * ((-len(binary)) % 4))
        image_offset = len(binary)
        binary.extend(source_bytes)
        view_index = len(document["bufferViews"])
        document["bufferViews"].append({"buffer": 0, "byteOffset": image_offset,
                                         "byteLength": len(source_bytes)})
        image["bufferView"] = view_index
        image["mimeType"] = mime_type
        try:
            source_label = candidates[0].relative_to(UNITY_TEXTURE_ROOT)
        except ValueError:
            source_label = candidates[0].relative_to(SOURCE_EXPORT)
        print(f"Restored Unity texture {source_label} -> {path.name}")

    # glTFast omits the separate emission atlas on the Unity light material.
    # Recover the texture by the source material's actual texture GUID and
    # preserve its authored emission tint in glTF's supported emissive fields.
    for material in document.get("materials", []):
        if material.get("emissiveTexture"):
            continue
        material_name = material.get("name", "")
        material_paths = list(UNITY_TEXTURE_ROOT.rglob(f"{material_name}.mat"))
        if not material_paths:
            continue
        if len(material_paths) != 1:
            raise SystemExit(f"Ambiguous Unity material {material_name}: {material_paths}")
        source_material = material_paths[0].read_text(encoding="utf-8", errors="replace")
        emission = re.search(
            r"- _EmissionMap:\s*\n\s*m_Texture:\s*\{fileID:\s*(\d+),\s*guid:\s*([0-9a-fA-F]+)",
            source_material)
        if not emission or int(emission.group(1)) == 0:
            continue
        texture_guid = emission.group(2).lower()
        guid_matches = []
        for meta in UNITY_TEXTURE_ROOT.rglob("*.meta"):
            meta_text = meta.read_text(encoding="utf-8", errors="replace")
            if re.search(rf"^guid:\s*{re.escape(texture_guid)}\s*$", meta_text, re.M | re.I):
                guid_matches.append(meta.with_suffix(""))
        if len(guid_matches) != 1:
            raise SystemExit(f"Could not uniquely resolve Unity emission texture {texture_guid}: {guid_matches}")
        emission_path = guid_matches[0]
        extension = emission_path.suffix.lower()
        mime_type = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}.get(extension)
        if not mime_type:
            raise SystemExit(f"Unsupported Unity emission image format: {emission_path}")
        emission_bytes = emission_path.read_bytes()
        binary.extend(b"\0" * ((-len(binary)) % 4))
        image_offset = len(binary)
        binary.extend(emission_bytes)
        image_index = len(document.setdefault("images", []))
        document["images"].append({"name": emission_path.name, "mimeType": mime_type,
                                   "bufferView": len(document["bufferViews"])})
        document["bufferViews"].append({"buffer": 0, "byteOffset": image_offset,
                                         "byteLength": len(emission_bytes)})
        texture_index = len(document.setdefault("textures", []))
        document["textures"].append({"source": image_index})
        material["emissiveTexture"] = {"index": texture_index}
        color = re.search(
            r"- _EmissionColor:\s*\{r:\s*([^,]+),\s*g:\s*([^,]+),\s*b:\s*([^,]+),",
            source_material)
        material["emissiveFactor"] = (
            [max(0.0, min(1.0, float(channel))) for channel in color.groups()]
            if color else [1.0, 1.0, 1.0])
        print(f"Restored Unity emission map {emission_path.relative_to(UNITY_TEXTURE_ROOT)} -> {path.name}")

    def accessor_bytes(accessor_index: int) -> tuple[dict, int, int, int]:
        accessor = document["accessors"][accessor_index]
        view = document["bufferViews"][accessor["bufferView"]]
        component = accessor["componentType"]
        width = {5121: 1, 5123: 2, 5125: 4, 5126: 4}[component]
        count_per_item = {"SCALAR": 1, "VEC3": 3}[accessor["type"]]
        stride = view.get("byteStride", width * count_per_item)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return accessor, offset, stride, width

    dropped = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            position = primitive.get("attributes", {}).get("POSITION")
            indices = primitive.get("indices")
            if position is None or indices is None:
                continue
            pa, po, ps, _ = accessor_bytes(position)
            if pa["componentType"] != 5126 or pa["type"] != "VEC3":
                continue
            position_view = []
            for i in range(pa["count"]):
                position_view.append(struct.unpack_from("<3f", binary, po + i * ps))
            ia, io, istride, width = accessor_bytes(indices)
            if ia["type"] != "SCALAR" or ia["count"] % 3:
                continue
            code = {1: "B", 2: "H", 4: "I"}[width]
            old_indices = [struct.unpack_from("<" + code, binary, io + i * istride)[0]
                           for i in range(ia["count"])]
            kept = []
            for i in range(0, len(old_indices), 3):
                a, b, c = (position_view[old_indices[i + j]] for j in range(3))
                e = tuple(b[k] - a[k] for k in range(3))
                f = tuple(c[k] - a[k] for k in range(3))
                cross = (e[1]*f[2]-e[2]*f[1], e[2]*f[0]-e[0]*f[2], e[0]*f[1]-e[1]*f[0])
                if sum(x*x for x in cross) > 1e-20:
                    kept.extend(old_indices[i:i + 3])
                else:
                    dropped += 1
            if len(kept) == len(old_indices):
                continue
            offset = len(binary)
            binary.extend(b"\0" * ((-offset) % 4))
            offset = len(binary)
            for value in kept:
                binary.extend(struct.pack("<" + code, value))
            view_index = len(document["bufferViews"])
            document["bufferViews"].append({"buffer": 0, "byteOffset": offset,
                                             "byteLength": len(kept) * width})
            ia["bufferView"] = view_index
            ia.pop("byteOffset", None)
            ia["count"] = len(kept)
            ia["min"] = [min(kept)] if kept else [0]
            ia["max"] = [max(kept)] if kept else [0]
    if dropped:
        print(f"Removed {dropped} degenerate triangles from {path.name}")
    for collection, prefix in (("meshes", "mesh"), ("materials", "material")):
        for i, item in enumerate(document.get(collection, [])):
            item.setdefault("extras", {}).setdefault("darkangel_key", f"{prefix}/{i}")
    # The current DarkAngel glTF cooker consumes position, normal, and UV0;
    # Unity's exporter emits tangents even though the DarkAngel toon path
    # derives its own lighting basis. Remove only this unsupported stream.
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive.get("attributes", {}).pop("TANGENT", None)
    document["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    binary_chunk = struct.pack("<II", len(binary), 0x004E4942) + binary
    result = b"glTF" + struct.pack("<IIII", 2, 20 + len(encoded) + len(binary_chunk), len(encoded), 0x4E4F534A) + encoded + binary_chunk
    if result != data:
        path.write_bytes(result)


def place(index: int, name: str, model: str | None, x: float, y: float, z: float,
          yaw: float = 0.0, pitch: float = 0.0, roll: float = 0.0,
          scale: float = 1.0, optional: dict | None = None) -> dict:
    entity = {
        "name": name,
        "model": model,
        "target": None,
        "types": {
            "1": {"version": 2, "fields": {
                "1": yaw, "2": x, "3": y, "4": z,
                "5": pitch, "6": roll, "7": scale}},
            "2": {"version": 1, "fields": {"1": 100, "2": 100}},
        },
    }
    if optional:
        entity["optional"] = optional
    return entity


def main() -> None:
    if not TOOL.is_file():
        raise SystemExit(f"Build AssetTool first: {TOOL}")
    layout_path = SOURCE_EXPORT / "RoyalCrownTemple.layout.json"
    if not layout_path.is_file():
        raise SystemExit("Unity export is missing; complete the Royal Crown source export first.")
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    if len(layout["placements"]) != 1017 or len(layout["trees"]) != 31:
        raise SystemExit("Unexpected Unity layout counts; refusing to publish an incomplete scene.")

    models_dir = SOURCE_EXPORT / "models"
    model_names = sorted({p["moduleId"].replace(".", "_") for p in layout["placements"]})
    model_names += [f"tree_13{variant}" for variant in "abc"]
    relative_files: list[str] = []
    for name in model_names:
        exported = models_dir / name / f"{name}.glb"
        if not exported.is_file() or exported.stat().st_size < 64:
            raise SystemExit(f"Missing or empty exported model: {exported}")
        add_stable_subasset_keys(exported)
        destination = DEST / f"{name}.glb"
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            add_stable_subasset_keys(destination)
            add_new_texture_keys(destination)
            if destination.read_bytes() != exported.read_bytes():
                raise SystemExit(f"Refusing to replace existing model source: {destination}")
        else:
            shutil.copyfile(exported, destination)
        relative_files.append(destination.relative_to(SOURCE).as_posix())

    pending = [relative for relative in relative_files if not Path(str(SOURCE / relative) + ".daimport").exists()]
    if pending:
        adopted = run("adopt-many", SOURCE, CACHE, *pending).splitlines()
        if len(adopted) != len(pending):
            raise SystemExit("AssetTool adopted an unexpected number of GLB models.")

    models = {name: asset_id(SOURCE / "royal_crown/models" / f"{name}.glb") for name in model_names}
    root = guid("royal-crown-temple-scene")
    entities: dict[str, dict] = {}
    directional_object = None
    index = 1
    for item in layout["placements"]:
        name = item["moduleId"].replace(".", "_")
        entity_id = f"{index:032x}"
        entities[entity_id] = place(
            index, f"{item['displayName']}__{item['placementId']}", models[name],
            item["x"], item["y"], item["z"],
            math.radians(item["yaw"]), scale=item["scale"])
        index += 1

    for tree in layout["trees"]:
        name = tree["model"]
        entity_id = f"{index:032x}"
        non_uniform = max(tree["sx"], tree["sy"], tree["sz"]) - min(tree["sx"], tree["sy"], tree["sz"]) > 0.01
        scale = tree["sy"] if non_uniform else tree["sx"]
        entities[entity_id] = place(index, tree["name"], models[name],
            tree["x"], tree["y"], tree["z"], math.radians(tree["yaw"]),
            math.radians(tree["pitch"]), math.radians(tree["roll"]), scale)
        index += 1

    for light in layout["lights"]:
        entity_id = f"{index:032x}"
        kind = light["type"].lower()
        if kind not in ("directional", "point", "spot"):
            continue
        component = {
            "schema": 1, "type": kind, "enabled": True,
            "cast_shadows": kind == "directional",
            "color": [light["r"], light["g"], light["b"]],
            "intensity": light["intensity"], "range": max(light["range"], 0.01),
            "falloff": 2.0,
            "inner_cone": max(0.0, min(light["spotAngle"] * 0.35, 89.0)),
            "outer_cone": max(0.01, min(light["spotAngle"], 179.0)),
        }
        entities[entity_id] = place(index, light["name"], None,
            light["x"], light["y"], light["z"], math.radians(light["yaw"]),
            math.radians(light["pitch"]), math.radians(light["roll"]), optional={"light": component})
        if kind == "directional" and directional_object is None:
            directional_object = entity_id
        index += 1

    # The scene's player is both the visible canonical-human model and the
    # possessed actor selected by the Royal Crown launcher.
    player_model_path = SOURCE / "royal_district/character/canonical-human.glb"
    if not Path(str(player_model_path) + ".daimport").exists():
        run("adopt-human-renderable", SOURCE, CACHE,
            player_model_path.relative_to(SOURCE).as_posix(),
            "animation/canonical_human.daskeleton")
    player_model = asset_id(player_model_path)
    player_object_id = 0x500
    entities[f"{player_object_id:032x}"] = place(
        player_object_id, "Royal Crown Player Spawn", player_model,
        0.0, -8.0, -176.0)

    first_ground_id = "00000000000000000000000000000001"
    if first_ground_id not in entities:
        raise SystemExit("Scene has no root terrain entity.")
    ambient = layout["ambient"]
    sky_color = [ambient[0], ambient[1], ambient[2]]
    entities[first_ground_id]["optional"] = {"rendering": {
        "schema": 1,
        "main_light": directional_object,
        "sky_enabled": True,
        "sky_color": [0.29, 0.12, 0.035],
        "sky": {
            "procedural": True,
            "zenith": [0.055, 0.018, 0.018],
            "horizon": [0.76, 0.27, 0.035],
            "ground": [0.11, 0.075, 0.018],
            "sun_direction": [-0.35, 0.7, -0.55],
            "sun_color": [1.0, 0.55, 0.25],
            "sun_intensity": 0.6,
            "sun_size": 0.08,
        },
        "toon": {"ambient_color": sky_color, "ambient_strength": 0.6,
                 "shadow_tint": [0.18, 0.12, 0.20], "thresholds": [0.18, 0.52, 0.82],
                 "softness": 0.035, "local_strength": 1.0},
        "shadows": {"enabled": True, "cascade_count": 4, "resolution": 2048,
                    "distance": 320.0, "depth_bias": 0.001, "normal_bias": 0.002,
                    "split_distribution": 0.5},
        "fog": {"enabled": True, "mode": "linear", "color": [0.38, 0.22, 0.075],
                "start": 170.0, "end": 340.0, "density": 0.015},
        "post": {"bloom": True, "bloom_threshold": 1.0, "bloom_intensity": 0.3,
                 "exposure": 0.0, "contrast": 1.08, "saturation": 1.12,
                 "fxaa": True, "outline": {"enabled": True, "thickness": 3.0}},
    }}

    graph = {"schema": 1, "asset": root, "entities": entities, "mounts": {}}
    scene = {"schema": 1, "root": root, "placement": first_ground_id, "sources": {root: graph}}
    if SCENE.exists():
        existing = json.loads(SCENE.read_text(encoding="utf-8"))
        if existing != scene:
            previous_root = str(uuid.uuid5(uuid.UUID("0ae2a2d8-3ff0-4e59-b58a-9589504fdd50"), "royal-crown-temple-scene"))
            if existing.get("root") == previous_root and previous_root in existing.get("sources", {}):
                previous_graph = existing["sources"].pop(previous_root)
                existing["root"] = root
                previous_graph["asset"] = root
                existing["sources"][root] = previous_graph
            if existing.get("root") == root and root in existing.get("sources", {}):
                previous_graph = existing["sources"][root]
                expected_graph = scene["sources"][root]
                for entity_id, expected_entity in expected_graph["entities"].items():
                    if entity_id in previous_graph.get("entities", {}):
                        old_entity = previous_graph["entities"][entity_id]
                        if old_entity.get("name") != expected_entity.get("name"):
                            continue
                        if expected_entity.get("model") is None and old_entity.get("optional") == expected_entity.get("optional"):
                            old_entity["model"] = None
                        old_optional = old_entity.get("optional", {})
                        if old_optional.get("rendering") is not None:
                            expected_entity.setdefault("optional", {})["rendering"] = old_optional["rendering"]
                        if old_optional.get("light") is not None:
                            expected_entity.setdefault("optional", {})["light"] = old_optional["light"]
        if existing != scene:
            raise SystemExit(f"Refusing to replace existing scene source: {SCENE}")
    write_json(SCENE, scene)

    CACHE.mkdir(parents=True, exist_ok=True)
    for relative in relative_files:
        run("cook", SOURCE, CACHE, relative)
    player_kit_path = SOURCE / "royal_district/combat/player.dakit"
    run("cook", SOURCE, CACHE, player_model_path.relative_to(SOURCE).as_posix())
    run("cook", SOURCE, CACHE, player_kit_path.relative_to(SOURCE).as_posix())
    player_kit = json.loads(player_kit_path.read_text(encoding="utf-8"))["asset"]
    asset_ids = [*([models[name] for name in model_names]), player_model, player_kit]
    partials: list[Path] = []
    for i in range(0, len(asset_ids), 64):
        group = asset_ids[i:i + 64]
        registry = CACHE / f"registry-part-{i // 64}.json"
        run("package-many", SOURCE, CACHE, group[0], registry, *group[1:])
        partials.append(registry)
    combined = json.loads(partials[0].read_text(encoding="utf-8"))
    products = {asset["id"]: asset for asset in combined["assets"]}
    roots = list(combined.get("roots", [combined["root"]]))
    for partial in partials[1:]:
        partial_data = json.loads(partial.read_text(encoding="utf-8"))
        for root_id in partial_data.get("roots", [partial_data["root"]]):
            if root_id not in roots:
                roots.append(root_id)
        for asset in partial_data["assets"]:
            previous = products.get(asset["id"])
            if previous is not None:
                if previous != asset:
                    raise SystemExit(f"Conflicting shared cooked product: {asset['id']}")
            else:
                combined["assets"].append(asset)
                products[asset["id"]] = asset
    combined["roots"] = roots
    write_json(CACHE / "registry.json", combined)
    print(f"Imported {len(model_names)} scene modules plus the player model; wrote {len(entities)} scene entities to {SCENE}")
    print(f"Cooked registry: {CACHE / 'registry.json'}")


if __name__ == "__main__":
    main()
