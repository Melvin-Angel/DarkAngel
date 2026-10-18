"""GPU regressions for black toon ink, metre-based fading and distance fog.

Uses the prepared Royal Crown package and generates isolated scene variants;
authored content and user preferences are never saved by these hidden runs.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import uuid

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def pixels(image):
    return getattr(image, "get_flattened_data", image.getdata)()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("d3d12", "vulkan"), default="d3d12")
    args = parser.parse_args()
    editor = ROOT / "build/m5-editor-relwithdebinfo/DarkAngelEditor.exe"
    scene_path = ROOT / "content/royal_crown/RoyalCrownTemple.dascene"
    scene_bytes = scene_path.read_bytes()
    scene = json.loads(scene_bytes)
    output = ROOT / "artifacts" / ("toon-outlines-" + uuid.uuid4().hex[:10])
    output.mkdir(parents=True)
    preferences = output / "preferences.json"
    env = os.environ.copy()
    env["DAE_EDITOR_PREFERENCES"] = str(preferences)
    receipt = dict(backend=args.backend, binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),
                   scene_sha256=hashlib.sha256(scene_bytes).hexdigest(), captures={})
    images = {}
    cases = (("off", False, 1), ("thin", True, 1), ("thick", True, 8),
             ("faded", True, 8), ("fog_mid", False, 1), ("fog_full", False, 1))
    for name, enabled, thickness in cases:
        variant = copy.deepcopy(scene)
        settings = variant["sources"][variant["root"]]["entities"]["00000000000000000000000000000001"]["optional"]["rendering"]
        settings["post"].update(bloom=False, fxaa=False, exposure=0, exposure_compensation=0,
                               contrast=1, saturation=1, tint=[1, 1, 1], grade_lift=[0, 0, 0],
                               grade_gamma=[1, 1, 1], grade_gain=[1, 1, 1], grade_temperature=0)
        settings["post"]["outline"] = dict(enabled=enabled, thickness=thickness, color=[0, 0, 0],
                                            fade_start=10000, fade_end=10001)
        settings["fog"] = dict(enabled=False)
        if name == "faded":
            settings["post"]["outline"].update(fade_start=2, fade_end=3)
        if name.startswith("fog_"):
            settings["fog"] = dict(enabled=True, mode="exponential", color=[.2, .6, .8],
                                    density=.008 if name == "fog_mid" else 10)
        variant_path = output / (name + ".dascene")
        variant_path.write_text(json.dumps(variant), encoding="utf-8")
        preferences.write_text(json.dumps(dict(version=1, layout="", layout_dpi=1, ui_scale=.75)), encoding="utf-8")
        capture = output / (name + ".png")
        command = [editor, "--registry", ROOT / ".cache/royal-crown-temple/registry.json",
                   "--cas", ROOT / ".cache/royal-crown-temple/cas",
                   "--model", "c0774767-2fff-44ad-afc9-418a61a93e30", "--scene", variant_path,
                   "--sources", ROOT / "content", "--asset-cache", ROOT / ".cache/editor-assets",
                   "--backend", args.backend, "--hidden", "--no-audio", "--frames", "5",
                   "--width", "1920", "--height", "1080", "--camera-from", "38", "25", "42",
                   "--camera-target", "0", "8", "-100", "--capture", capture]
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=180)
        log = result.stdout + result.stderr
        (output / (name + ".log")).write_text(log, encoding="utf-8")
        match = re.search(r"viewport_rect=(\d+),(\d+),(\d+),(\d+)", log)
        if result.returncode or not match:
            raise RuntimeError(f"{name} failed: {log[-3000:]}")
        rect = tuple(map(int, match.groups()))
        images[name] = Image.open(capture).convert("RGB").crop(rect)
        receipt["captures"][name] = dict(path=str(capture), viewport_rect=rect)
        print(f"{args.backend} {name} captured", flush=True)
    if len({image.size for image in images.values()}) != 1:
        raise RuntimeError("Outline comparison viewports differ")
    base = list(pixels(images["off"]))
    comparisons = {}
    for name in ("thin", "thick"):
        darkened = brightened = black_ink = 0
        for before, after in zip(base, pixels(images[name])):
            delta = sum(before) - sum(after)
            darkened += delta >= 30
            brightened += delta <= -30
            black_ink += max(before) >= 60 and max(after) <= 8
        comparisons[name] = dict(darkened_pixels=darkened, brightened_pixels=brightened,
                                 black_ink_pixels=black_ink)
        if darkened < 500 or black_ink < 500 or brightened > 100:
            raise RuntimeError(f"Black outline must visibly darken geometry: {name} {comparisons[name]}")
    if comparisons["thick"]["black_ink_pixels"] < comparisons["thin"]["black_ink_pixels"] * 1.5:
        raise RuntimeError("Increasing thickness did not widen the black ink")
    faded_changes = sum(max(abs(a-b) for a, b in zip(before, after)) > 2
                        for before, after in zip(base, pixels(images["faded"])))
    if faded_changes > 20:
        raise RuntimeError(f"Metre-based outline fade did not remove distant ink: {faded_changes}")
    # With saturated fog every surface, including highlights/emission, must
    # approach tone(fog colour). Measure the actual pixels, not shader text.
    fog_target = tuple(round(255 * (1-math.exp(-channel))) for channel in (.2, .6, .8))
    fog_pixels = list(pixels(images["fog_full"]))
    geometry = [index for index, colour in enumerate(fog_pixels)
                if max(abs(a-b) for a, b in zip(colour, fog_target)) <= 2]
    if len(geometry) < 10000:
        raise RuntimeError(f"Distant toon surfaces do not converge to fog colour: {len(geometry)}")
    middle = list(pixels(images["fog_mid"]))
    def error(colour):
        return sum(abs(a-b) for a, b in zip(colour, fog_target))
    unfogged_error = sum(error(base[index]) for index in geometry) / len(geometry)
    middle_error = sum(error(middle[index]) for index in geometry) / len(geometry)
    if middle_error >= unfogged_error * .9:
        raise RuntimeError("Exponential fog did not move toon surfaces toward the fog colour")
    receipt["distance_fade"] = dict(pixels_different_from_disabled=faded_changes)
    receipt["fog"] = dict(target=fog_target, converged_pixels=len(geometry),
                          unfogged_mean_error=unfogged_error, medium_fog_mean_error=middle_error)
    if scene_path.read_bytes() != scene_bytes:
        raise RuntimeError("Authored scene changed during capture")
    receipt["comparisons"] = comparisons
    receipt["passed"] = True
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(comparisons), flush=True)
    print("PASS " + str(output / "receipt.json"), flush=True)


if __name__ == "__main__":
    main()
