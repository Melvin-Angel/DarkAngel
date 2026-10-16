"""R6.1: UI documents in the UI tab of the D3D12 editor on disposable content (template, live preview, edit, command log, Undo, Save, Play eviction and restore).

Build DarkAngelEditor and UiDocumentTests with the pinned MSVC environment first.
Uses the existing authoring-fixture.json seed without running or rewriting its slow producer.
No protected project sources or original fixture caches are edited.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/m5-editor-relwithdebinfo"
EVIDENCE = ROOT / "docs/implementation/evidence"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gate", choices=("all", "ui", "ui-play"), default="all")
    args = parser.parse_args()
    seed = json.loads((BUILD / "authoring-fixture.json").read_text(encoding="utf-8"))
    run_id = uuid.uuid4().hex[:10]
    work = ROOT / ".cache" / ("r6-ui-" + run_id)
    source, cache = work / "sources", work / "cache"
    shutil.copytree(seed["source"], source)
    shutil.copytree(seed["cache"], cache)
    protected = [p for p in (ROOT / "content/royal_district").rglob("*") if p.is_file()
                 and (p.name in ("RoyalCombat.dascene", "RoyalTraining.dascene") or ".records" in str(p))]
    before = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    receipt = {
        "run": run_id, "gate": args.gate, "fixture": str(work), "gates": [],
        "binary_sha256": hashlib.sha256((BUILD / "DarkAngelEditor.exe").read_bytes()).hexdigest(),
        "qualification": "Synthetic ImGui input in the D3D12 editor on this machine; no physical-device or remote/EOS qualification.",
    }
    receipt_path = EVIDENCE / ("r6-ui-" + run_id + ".json")

    def run(name, command, markers=()):
        log = EVIDENCE / ("r6-" + run_id + "-" + name + ".log")
        start = time.monotonic()
        result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=600)
        output = result.stdout + result.stderr
        log.write_text(output, encoding="utf-8")
        entry = dict(name=name, exit=result.returncode, seconds=round(time.monotonic() - start, 3),
                     log=str(log.relative_to(ROOT)), markers=all(m in output for m in markers))
        receipt["gates"].append(entry)
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        if result.returncode or not entry["markers"]:
            raise RuntimeError(f"{name} failed: {log}\n{output[-4000:]}")
        print(name + " passed", flush=True)

    try:
        if args.gate == "all":
            for name in ("UiDocumentTests", "UiServiceTests", "PuzzleTests", "HudTests"):
                run(name, [BUILD / (name + ".exe")])
        model = json.loads((source / "royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport").read_text())["id"]
        collision = json.loads((source / "royal_district/collision/environment.dacollision").read_text())["asset"]
        base = [BUILD / "DarkAngelEditor.exe", "--registry", seed["reaction_registry"], "--cas", cache / "cas",
                "--model", model, "--scene", source / "royal_district/RoyalTraining.dascene",
                "--character-kit", seed["kit"], "--character-collision", collision,
                "--character-player", "00000000000000000000000000000004", "--sources", source,
                "--asset-cache", cache, "--backend", "d3d12", "--hidden", "--no-audio", "--capture-workspace"]
        # UI examples are shipped project content; keep the fixture producer compatible with older seeds.
        shutil.copytree(ROOT / "content/ui", source / "ui", dirs_exist_ok=True)
        cases = {
            "ui-play": (["--exercise-ui-play", "--frames", "64"], ["Editor ui-play hud=1 marker=1 modal=1 host_puzzle=1 frontend=1 cleanup=1"]),
            "ui": (["--exercise-ui", "--frames", "32"], ["Editor ui create=1 preview=1 edit=1 command=1 undo=1 save=1 members=1 shared_style=1 package=1 migrate=1 play_coexist=1"]),
        }
        for name, (options, markers) in cases.items():
            if args.gate not in ("all", name):
                continue
            # The navigation gate saves its own disposable scene; each regression begins with the seed again.
            if False:
                shutil.copytree(seed["source"], source, dirs_exist_ok=True)
            capture = EVIDENCE / ("r6-" + run_id + "-" + name + ".png")
            run(name, base + options + ["--capture", capture], markers)
        if args.gate in ("all", "ui-play"):
            player = json.loads((source / "players/royal_player.daplayer").read_text())["asset"]
            report = EVIDENCE / ("r6-" + run_id + "-ui-party.json")
            command = base + ["--player-asset", player, "--game-workspace", "--listen", "127.0.0.1:26519", "--players", "2", "--party-report", report, "--capture", EVIDENCE / ("r6-" + run_id + "-ui-party.png"),
                              "--player-script", "await-players:2;wait:60;use;ui-await;ui-confirm;wait:20;ui-next;ui-confirm;wait:20;ui-next;ui-confirm;ui-solved;capture;await-tick:300;quit",
                              "--input-script", "ui-solved;await-tick:240;quit"]
            run("ui-party", command, ["Script UI solved"])
            def final(path):
                value = json.loads(path.read_text())
                return value.get("last_running", value)
            for path in (report, report.with_name(report.stem + "-player-1.json")):
                state = final(path)
                puzzles = state.get("world", {}).get("puzzles", [])
                if not puzzles or not all(p["solved"] for p in puzzles):
                    raise RuntimeError("Party member lacks confirmed puzzle result: " + str(path))
        receipt["passed"] = True
    finally:
        receipt["protected_unchanged"] = all(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest() == before[str(p.relative_to(ROOT))]
                                             for p in protected)
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    if not receipt["protected_unchanged"]:
        raise RuntimeError("Protected content changed")
    if args.gate == "all":
        (EVIDENCE / "r6-ui.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(str(receipt_path), flush=True)


if __name__ == "__main__":
    main()
