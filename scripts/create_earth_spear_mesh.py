#!/usr/bin/env python3
"""Write a faceted Earth spear mesh along Dark Angel's +Z projectile-forward axis."""
import math
import random
import sys
from pathlib import Path


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("content/royal_district/combat/earth/earth_spear_rock.obj")
    random.seed(23)
    sides = 7
    rings = [(-0.66, 0.16), (-0.30, 0.30), (0.18, 0.24), (0.52, 0.13)]
    vertices = []
    # Keep one readable silhouette for the ground, hand and in-flight stages.
    # The prior 1.74 m spear overwhelmed the character once it reached the hand.
    mesh_scale = 0.72
    phase = 0.14
    for z, radius in rings:
        for index in range(sides):
            angle = 2 * math.pi * index / sides + phase
            jitter = random.uniform(0.82, 1.18)
            vertices.append((mesh_scale * radius * jitter * math.cos(angle), mesh_scale * radius * jitter * math.sin(angle), mesh_scale * z))
    tip = len(vertices)
    vertices.append((mesh_scale * 0.025, mesh_scale * -0.018, mesh_scale * 1.08))
    triangles = []
    for layer in range(len(rings) - 1):
        lower, upper = layer * sides, (layer + 1) * sides
        for index in range(sides):
            a, b = lower + index, lower + (index + 1) % sides
            c, d = upper + (index + 1) % sides, upper + index
            triangles.extend(((a, b, c), (a, c, d)))
    for index in range(sides):
        triangles.append(((len(rings) - 1) * sides + index,
                          (len(rings) - 1) * sides + (index + 1) % sides, tip))
    for index in range(1, sides - 1):
        triangles.append((0, index + 1, index))

    lines = []
    for triangle in triangles:
        points = [vertices[index] for index in triangle]
        u = [points[1][axis] - points[0][axis] for axis in range(3)]
        v = [points[2][axis] - points[0][axis] for axis in range(3)]
        normal = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(component * component for component in normal)) or 1
        normal = tuple(component / length for component in normal)
        start = sum(line.startswith("v ") for line in lines) + 1
        for point in points:
            lines.append("v " + " ".join(f"{value:.6f}" for value in point))
        for _ in points:
            lines.append("vn " + " ".join(f"{value:.6f}" for value in normal))
        lines.append(f"f {start}//{start} {start + 1}//{start + 1} {start + 2}//{start + 2}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {out}: {len(triangles)} flat-shaded triangles, length axis +Z")


if __name__ == "__main__":
    main()
