"""Check final Royal viewport pixels after target resize, pose playback and gameplay look.

Uses the same content, registry and cache as open_royal_scene.ps1. Build the editor
first. Hidden runs do not save project sources; preferences and receipts are isolated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("d3d12", "vulkan"), default="d3d12")
    parser.add_argument("--case", choices=("all", "animation", "character", "game"), default="all")
    args = parser.parse_args()
    editor = ROOT / "build/m5-editor-relwithdebinfo/DarkAngelEditor.exe"
    source = ROOT / "content"
    output = ROOT / "artifacts" / ("viewport-output-" + uuid.uuid4().hex[:10])
    output.mkdir(parents=True)
    preferences = output / "preferences.json"
    clean_preferences = dict(version=1, layout="", layout_dpi=1, ui_scale=.75,
                             console_info=True, console_warning=True, console_error=True)
    env = os.environ.copy()
    env["DAE_EDITOR_PREFERENCES"] = str(preferences)

    def asset(path, key="asset"):
        return json.loads((source / path).read_text(encoding="utf-8"))[key]

    scene = source / "royal_district/RoyalCombat.dascene"
    protected = [scene, source / "players/royal_player.daplayer",
                 *scene.with_suffix(".dascene.records").glob("*.json")]
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    base = [editor, "--registry", ROOT / ".cache/royal-scene/registry.json",
            "--cas", ROOT / ".cache/royal-scene/cas", "--model",
            asset("royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport", "id"),
            "--scene", scene, "--sources", source, "--asset-cache", ROOT / ".cache/editor-assets",
            "--character-kit", asset("royal_district/combat/player.dakit"),
            "--character-player", "00000000000000000000000000000004",
            "--backend", args.backend, "--hidden", "--no-audio", "--width", "2560", "--height", "1600"]
    receipt = dict(backend=args.backend, case=args.case, binary_sha256=hashlib.sha256(editor.read_bytes()).hexdigest(),
                   captures={}, comparisons=[], qualification="Final GPU viewport pixels; synthetic camera input.")

    def run(name, options, marker):
        # Use the same layout on every run, independent of saved user preferences.
        preferences.write_text(json.dumps(clean_preferences), encoding="utf-8")
        capture = output / (name + ".png")
        command = list(map(str, [*base, *options, "--capture", capture]))
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=600)
        log = result.stdout + result.stderr
        (output / (name + ".log")).write_text(log, encoding="utf-8")
        if result.returncode or marker not in log:
            raise RuntimeError(f"{name} failed: {log[-4000:]}")
        match = re.search(r"viewport_rect=(\d+),(\d+),(\d+),(\d+)", log)
        if not match:
            raise RuntimeError("Rebuild the editor with viewport capture rectangle reporting")
        rect = tuple(map(int, match.groups()))
        image = Image.open(capture).convert("RGB").crop(rect)
        receipt["captures"][name] = dict(path=str(capture), viewport_rect=rect)
        print(name + " rendered", flush=True)
        return image

    def compare(name, first, second, minimum):
        # Presentation rows can add/remove a scrollbar, resizing the preview.
        # Compare normalized image coordinates, as a retained frozen texture is
        # stretched into the new rectangle by ImGui in exactly the same way.
        size = (min(first.width, second.width), min(first.height, second.height))
        first = first.resize(size, Image.Resampling.BILINEAR)
        second = second.resize(size, Image.Resampling.BILINEAR)
        width, height = first.size
        # Exclude toolbar, HUD bars and projected names at the outer edges.
        region = (int(width * .1), int(height * .2), int(width * .9), int(height * .8))
        delta = ImageChops.difference(first.crop(region), second.crop(region))
        pixels = delta.tobytes()
        changed = sum(max(pixels[i:i + 3]) > 12 for i in range(0, len(pixels), 3))
        receipt["comparisons"].append(dict(name=name, changed_pixels=changed, minimum=minimum))
        if changed < minimum:
            raise RuntimeError(f"{name}: final viewport is frozen or pose/camera change is invisible ({changed} pixels)")
        print(f"{name}: {changed} changed image pixels", flush=True)

    try:
        if args.case in ("all", "animation"):
            action = asset("royal_district/combat/light.daaction")
            # Both runs start on the same pose. Advance AFTER the first target was
            # bound, so retaining that first texture cannot satisfy this comparison.
            previews = [run(f"animation-{frames}", ["--exercise-composer-playback", "--animation-workspace",
                        "--authoring-asset", action, "--frames", str(frames)],
                        "Composer preview playback clock=") for frames in (8, 28)]
            compare("animation playback reaches display after resize", *previews, 1000)
        if args.case in ("all", "character"):
            character = asset("characters/royal_human.dacharacter")
            clip = asset("royal_district/clips/attack.glb.daimport", "id")
            characters = [run(f"character-{tick}", ["--exercise-character-preview",
                          "--authoring-asset", character, "--character-preview-clip", clip,
                          "--composer-tick", str(tick), "--frames", "8"],
                          "Authored Character preview tick=") for tick in (0, 20)]
            compare("Character pose reaches display after resize", *characters, 150)
        if args.case in ("all", "game"):
            games = [run("game-default", ["--play", "--game-workspace", "--frames", "30"],
                         "DarkAngel editor rendered"),
                     run("game-look", ["--exercise-camera-look", "--frames", "30"],
                         "Camera scripted mouse right/up")]
            compare("FreeLook changes displayed camera", *games, 3000)
        receipt["passed"] = True
    finally:
        receipt["protected_unchanged"] = all(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest() == before[str(p)]
                                             for p in protected)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    if not receipt["protected_unchanged"]:
        raise RuntimeError("Project source changed during viewport verification")
    print(output / "receipt.json", flush=True)


if __name__ == "__main__":
    main()
