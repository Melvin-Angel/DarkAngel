"""Scene-scale baseline (docs/implementation/current/SCENE_SCALE_PLAN.md, phase 0).

Generates scale-test scenes from the committed RoyalTraining scene with N extra static hut placements, runs the editor's
--exercise-scale measurement on each (hidden, with its own asset cache so the user's cache is untouched), runs SceneScaleBench
against the Royal environment collision, and writes everything to .cache/scene-scale/baseline.json with a summary table.

Measures only. The generated scenes live in content/_scale_test/ (gitignored) and are removed afterwards unless --keep is given.
Build DarkAngelEditor and SceneScaleBench in build/m5-editor-relwithdebinfo first.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "m5-editor-relwithdebinfo"
OUT = ROOT / ".cache" / "scene-scale"
SCENES = ROOT / "content" / "_scale_test"
BASE_SCENE = ROOT / "content" / "royal_district" / "RoyalTraining.dascene"
# Static models the Royal registry already holds, so every generated scene opens without packaging.
USED_MODELS = ["35d15141-eae0-400d-a616-78d890fc0982", "9a300109-7840-4bf0-bdae-94fb58759e99"]
# A static model outside that registry: placing it takes the cook-and-package path.
NEW_MODEL = "53a35b5c-be67-4cc8-a0f1-2621419b12f2"
AUTHORING_FRAMES, PLAY_FRAMES = 120, 300


def asset_id(path: str) -> str:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))["id" if path.endswith(".daimport") else "asset"]


def generate(count: int) -> Path:
    manifest = json.loads(BASE_SCENE.read_text(encoding="utf-8"))
    old_root = manifest["root"]
    # A fixed identity per count, so reruns reuse the same derived collision asset.
    digest = bytearray(hashlib.sha256(f"darkangel.scale-test/{count}".encode()).digest()[:16])
    digest[6] = (digest[6] & 0x0F) | 0x40  # UUIDv4 version and variant bits, as AssetId requires
    digest[8] = (digest[8] & 0x3F) | 0x80
    new_root = str(uuid.UUID(bytes=bytes(digest)))
    graph = json.loads(json.dumps(manifest["sources"][old_root]).replace(old_root, new_root))
    template = next(e for e in graph["entities"].values() if e.get("model") == USED_MODELS[0])
    columns = 16
    for index in range(count):
        entity = json.loads(json.dumps(template))
        entity["model"] = USED_MODELS[index % len(USED_MODELS)]
        entity["name"] = f"Scale hut {index}"
        fields = entity["types"]["1"]["fields"]
        # 12 m cells north of the training ground, clear of the player and the dummy.
        fields["2"] = -90.0 + 12.0 * (index % columns)
        fields["4"] = 30.0 + 12.0 * (index // columns)
        graph["entities"][f"{0x1000 + index:032x}"] = entity
    manifest["root"] = new_root
    manifest["sources"] = {new_root: graph}
    SCENES.mkdir(parents=True, exist_ok=True)
    path = SCENES / f"Scale_{count}.dascene"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


def run_editor(scene: Path, count: int, timeout: int) -> dict:
    report = OUT / f"editor_{count}.json"
    report.unlink(missing_ok=True)
    config = OUT / f"config_{count}.json"
    config.write_text(json.dumps({"report": str(report), "used_model": USED_MODELS[0], "new_model": NEW_MODEL,
                                  "authoring_frames": AUTHORING_FRAMES, "play_frames": PLAY_FRAMES}), encoding="utf-8")
    model = asset_id("content/royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport")
    kit = asset_id("content/royal_district/combat/player.dakit")
    collision = asset_id("content/royal_district/collision/environment.dacollision")
    arguments = [str(BUILD / "DarkAngelEditor.exe"), "--registry", ".cache/royal-scene/registry.json", "--cas", ".cache/royal-scene/cas",
                 "--model", model, "--scene", str(scene.relative_to(ROOT)), "--backend", "d3d12", "--sources", "content",
                 "--asset-cache", str((OUT / "assets").relative_to(ROOT)), "--character-collision", collision,
                 "--character-kit", kit, "--character-player", "00000000000000000000000000000004",
                 "--hidden", "--frames", str(AUTHORING_FRAMES + PLAY_FRAMES + 80), "--exercise-scale", str(config)]
    log = OUT / f"editor_{count}.log"
    try:
        completed = subprocess.run(arguments, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        log.write_text(completed.stdout + "\n--- stderr ---\n" + completed.stderr, encoding="utf-8")
        exit_code = completed.returncode
    except subprocess.TimeoutExpired as expired:
        log.write_text(f"timeout after {timeout} s\n{expired.stdout or ''}", encoding="utf-8")
        exit_code = "timeout"
    result = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {}
    result["exit_code"] = exit_code
    result["log"] = str(log.relative_to(ROOT))
    return result


def run_bench(counts: str) -> dict:
    report = OUT / "physics.json"
    completed = subprocess.run([str(BUILD / "SceneScaleBench.exe"), "--source", "content/royal_district/collision/environment.dacollision",
                                "--counts", counts, "--report", str(report)], cwd=ROOT, capture_output=True, text=True, timeout=1800)
    if completed.returncode != 0:
        return {"error": completed.stderr.strip() or completed.stdout.strip(), "exit_code": completed.returncode}
    return json.loads(report.read_text(encoding="utf-8"))


def summary(baseline: dict) -> str:
    lines = ["Editor (hidden, D3D12; models = model placements, the player included):",
             f"{'models':>8} {'author ms p50/p95':>18} {'place used':>11} {'move':>8} {'place new':>11} {'prepare Play':>13} {'play ms p50/p95':>16} {'private MB':>11}"]
    for row in baseline["editor"]:
        steps = row.get("steps", {})

        def step(name):
            entry = steps.get(name)
            if not entry:
                return "-"
            return f"{entry['ms']:.0f} ms" if entry["ok"] else "FAIL"

        def frames(name):
            entry = row.get(name)
            return f"{entry['p50_ms']:.1f}/{entry['p95_ms']:.1f}" if entry else "-"

        memory = (row.get("memory_end") or {}).get("private_mb")
        scene = row.get("scene") or {}
        statics = scene.get("model_placements", scene.get("static_models", row["requested"]))
        lines.append(f"{statics:>8} {frames('authoring_frames'):>18} {step('place_used_model'):>11} {step('move_placement'):>8} "
                     f"{step('place_new_model'):>11} {step('prepare_play'):>13} {frames('play_frames'):>16} "
                     f"{(f'{memory:.0f}' if memory else '-'):>11}")
        for name, entry in steps.items():
            if not entry["ok"]:
                lines.append(f"{'':>8} {name}: {entry['error']}")
        if row.get("exit_code") not in (0, None):
            lines.append(f"{'':>8} editor exit {row['exit_code']} (see {row['log']})")
    lines += ["", "Physics (SceneScaleBench, 2F hut copies plus ground):",
              f"{'meshes':>7} {'triangles':>10} {'load ms':>8} {'capture us/tick':>16} {'tick us':>8} {'replay ms/15 ticks':>19} {'worst':>7}"]
    for row in baseline["physics"].get("results", []):
        if row["ok"]:
            lines.append(f"{row['static_meshes']:>7} {row['triangles']:>10} {row['load_ms']:>8.2f} {row['capture_us_per_tick']:>16.1f} "
                         f"{row['tick_us']:>8.1f} {row['replay_ms_per_15_ticks']:>19.2f} {row['replay_ms_worst']:>7.2f}")
        else:
            lines.append(f"{row['static_meshes']:>7} {row['triangles']:>10} FAIL: {row['error']}")
    if "error" in baseline["physics"]:
        lines.append(f"SceneScaleBench failed: {baseline['physics']['error']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--counts", default="4,12,16,17,64,256", help="extra static hut placements per generated scene")
    parser.add_argument("--bench-counts", default="1,2,4,8,12,16,17,32,64,128")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per editor run")
    parser.add_argument("--keep", action="store_true", help="keep the generated scenes in content/_scale_test")
    parser.add_argument("--skip-editor", action="store_true")
    args = parser.parse_args()
    for binary in ("DarkAngelEditor.exe", "SceneScaleBench.exe"):
        if not (BUILD / binary).exists():
            print(f"Build {binary} in {BUILD.relative_to(ROOT)} first.", file=sys.stderr)
            return 1
    OUT.mkdir(parents=True, exist_ok=True)
    baseline = {"editor": [], "physics": {}}
    try:
        if not args.skip_editor:
            for count in (int(value) for value in args.counts.split(",")):
                # The base scene already places ground, two huts and the player; "models" counts every model placement.
                print(f"Editor run with {count} extra huts...", flush=True)
                row = run_editor(generate(count), count, args.timeout)
                row["requested"] = count
                baseline["editor"].append(row)
        print("Physics bench...", flush=True)
        baseline["physics"] = run_bench(args.bench_counts)
    finally:
        if not args.keep and SCENES.exists():
            shutil.rmtree(SCENES)
    (OUT / "baseline.json").write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
    print(summary(baseline))
    print(f"\nFull results: {(OUT / 'baseline.json').relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
