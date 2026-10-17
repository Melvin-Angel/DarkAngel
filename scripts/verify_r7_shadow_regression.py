"""Run the enabled-cascade static/skinned shadow crash regression on both backends."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "m5-editor-relwithdebinfo" / "DarkAngelEditor.exe"
DEFAULT_SCENE = ROOT / "artifacts" / "r7" / "main_scene_visual" / "diag_validation_shadow.dascene"
DEFAULT_OUTPUT = ROOT / "artifacts" / "r7" / "visual_polish"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--model", required=True, help="Any valid model asset id used to satisfy the editor startup contract")
    parser.add_argument("--registry", type=Path, default=ROOT / ".cache" / "royal-scene" / "registry.json")
    parser.add_argument("--cas", type=Path, default=ROOT / ".cache" / "royal-scene" / "cas")
    parser.add_argument("--sources", type=Path, default=ROOT / "content")
    parser.add_argument("--asset-cache", type=Path, default=ROOT / ".cache" / "editor-assets")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    common = [
        str(BUILD),
        "--registry", str(args.registry),
        "--cas", str(args.cas),
        "--model", args.model,
        "--scene", str(args.scene),
        "--sources", str(args.sources),
        "--asset-cache", str(args.asset_cache),
        "--frames", "30",
        "--hidden",
        "--capture-workspace",
    ]
    receipt: list[str] = []
    for backend in ("d3d12", "vulkan"):
        capture = args.output / f"shadow_regression_{backend}.png"
        completed = subprocess.run(
            common + ["--backend", backend, "--capture", str(capture)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            env=os.environ.copy(),
        )
        output = completed.stdout + completed.stderr
        if completed.returncode != 0:
            print(output)
            return completed.returncode or 1
        match = re.search(r"Viewport foreground pixels=(\d+)", output)
        if not match or int(match.group(1)) < 100:
            print(output)
            raise RuntimeError(f"{backend}: shadow capture did not contain a foreground receipt")
        receipt.append(f"{backend}: exit=0 foreground_pixels={match.group(1)} capture={capture}")
    print("R7 cascaded shadow regression passed")
    print("\n".join(receipt))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
