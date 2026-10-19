#!/usr/bin/env python3
"""Convert a triangulated OBJ with per-vertex normals into a compact static GLB."""
import json
import math
import struct
import sys
from pathlib import Path


def align(data):
    return data + b"\0" * (-len(data) % 4)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: convert_obj_to_static_glb.py input.obj output.glb")
    source, destination = map(Path, sys.argv[1:])
    positions, normals, triangles = [], [], []
    for line in source.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            positions.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "vn":
            normals.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "f":
            indices = [int(part.split("/")[0]) - 1 for part in fields[1:]]
            for i in range(1, len(indices) - 1):
                triangles.extend((indices[0], indices[i], indices[i + 1]))
    if not positions or len(normals) != len(positions) or not triangles:
        raise ValueError("OBJ needs positions, matching normals, and faces")
    if min(triangles) < 0 or max(triangles) >= len(positions):
        raise ValueError("OBJ face index is out of range")
    minimum = [min(p[i] for p in positions) for i in range(3)]
    maximum = [max(p[i] for p in positions) for i in range(3)]
    binary = bytearray()
    views, accessors = [], []

    def add(raw, component, kind, count, target, minimum_value=None, maximum_value=None):
        nonlocal binary
        binary = bytearray(align(bytes(binary)))
        offset = len(binary)
        binary.extend(raw)
        vi = len(views)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(raw), "target": target})
        accessor = {"bufferView": vi, "componentType": component, "count": count, "type": kind}
        if minimum_value is not None:
            accessor["min"] = minimum_value
        if maximum_value is not None:
            accessor["max"] = maximum_value
        ai = len(accessors)
        accessors.append(accessor)
        return ai

    pos = add(struct.pack("<" + "f" * (len(positions) * 3), *(x for p in positions for x in p)),
              5126, "VEC3", len(positions), 34962, minimum, maximum)
    nor = add(struct.pack("<" + "f" * (len(normals) * 3), *(x for n in normals for x in n)),
              5126, "VEC3", len(normals), 34962)
    idx = add(struct.pack("<" + "I" * len(triangles), *triangles), 5125, "SCALAR", len(triangles), 34963)
    document = {
        "asset": {"version": "2.0", "generator": "DarkAngel Earth Spear OBJ conversion"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Earth_Spear_Rock", "mesh": 0}],
        "meshes": [{"name": "Earth_Spear_Rock", "extras": {"darkangel_key": "mesh/earth-spear-rock"},
                    "primitives": [{"attributes": {"POSITION": pos, "NORMAL": nor}, "indices": idx, "material": 0}]}],
        "materials": [{"name": "Earth_Spear_Stone", "doubleSided": True,
                       "extras": {"darkangel_key": "material/earth-spear-stone"},
                       "pbrMetallicRoughness": {"baseColorFactor": [0.43, 0.36, 0.27, 1.0],
                                                 "metallicFactor": 0.0, "roughnessFactor": 0.92}}],
        "bufferViews": views,
        "accessors": accessors,
        "buffers": [{"byteLength": len(binary)}],
    }
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    binary = bytearray(align(bytes(binary)))
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    output = bytearray(struct.pack("<4sII", b"glTF", 2, total))
    output += struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    output += struct.pack("<II", len(binary), 0x004E4942) + binary
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output)
    print(f"Wrote {destination}: {len(positions)} vertices, {len(triangles) // 3} triangles, bounds {minimum}..{maximum}")


if __name__ == "__main__":
    main()
