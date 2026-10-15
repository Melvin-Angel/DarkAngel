"""R2 shared UI pilot: disposable D3D12 fixtures, captures and isolated preferences.
Build the supported editor profile first. Does not qualify all R2 authoring workflows.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/m5-editor-relwithdebinfo"
EVIDENCE = ROOT / "docs/implementation/evidence"

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--regressions",action="store_true");args=parser.parse_args()
    seed = json.loads((BUILD / "authoring-fixture.json").read_text(encoding="utf-8"))
    run_id = uuid.uuid4().hex[:10]
    work = ROOT / ".cache" / ("r2-ui-" + run_id)
    source, cache = work / "sources", work / "cache"
    shutil.copytree(seed["source"], source)
    shutil.copytree(seed["cache"], cache)
    preferences = work / "preferences.json"
    env = os.environ.copy()
    env["DAE_EDITOR_PREFERENCES"] = str(preferences)
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (ROOT / "content/royal_district").rglob("*")
                 if p.is_file() and (p.suffix == ".dascene" or ".records" in str(p))}
    scene = source / "royal_district/RoyalTraining.dascene"
    before_scene = scene.read_bytes()
    model = json.loads((source / "royal_district/static/terrain/SM_RC_Terrain_Ground_32x32.gltf.daimport").read_text())["id"]
    collision = json.loads((source / "royal_district/collision/environment.dacollision").read_text())["asset"]
    base = [BUILD / "DarkAngelEditor.exe", "--registry", seed["reaction_registry"], "--cas", cache / "cas",
            "--model", model, "--scene", scene, "--character-kit", seed["kit"],
            "--character-collision", collision, "--character-player", "00000000000000000000000000000004",
            "--sources", source, "--asset-cache", cache, "--backend", "d3d12", "--hidden", "--no-audio", "--capture-workspace"]
    receipt = {"run": run_id, "fixture": str(work), "gates": [],
               "binary_sha256": hashlib.sha256((BUILD / "DarkAngelEditor.exe").read_bytes()).hexdigest(),
               "qualification": "Synthetic input, local D3D12; DPI uses the editor scale multiplier, not an OS monitor change."}
    files=("editor_theme.hpp","editor_theme.cpp","editor_ui.hpp","editor_ui.cpp","editor_preferences.cpp","editor_workspaces.cpp","editor_assets.cpp","editor_authoring.cpp","editor_items.cpp","editor_composer.cpp","audio_workspace.cpp","scene_navigation.cpp","reference_picker.hpp","editor_app.cpp","exercises/authoring_sources.cpp","exercises/editor_ux_exercise.cpp","exercises/live_previews_exercise.cpp","pose_previews.cpp","vfx_workspace.cpp")
    receipt["source_sha256"]={"apps/editor/"+name:hashlib.sha256((ROOT/"apps/editor"/name).read_bytes()).hexdigest() for name in files}
    receipt_path = EVIDENCE / ("r2-editor-ux-" + run_id + ".json")
    def run(name, options, marker=None):
        capture = EVIDENCE / ("r2-" + run_id + "-" + name + ".png")
        start = time.monotonic()
        result = subprocess.run(list(map(str, base + options + ["--capture", capture])), cwd=ROOT,
                                env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
        output = result.stdout + result.stderr
        log = EVIDENCE / ("r2-" + run_id + "-" + name + ".log")
        log.write_text(output, encoding="utf-8")
        markers=[marker] if isinstance(marker,str) else (marker or [])
        success = result.returncode == 0 and all(value in output for value in markers)
        receipt["gates"].append(dict(name=name, passed=success, exit=result.returncode,
                                    seconds=round(time.monotonic()-start,3), log=str(log.relative_to(ROOT)), capture=str(capture.relative_to(ROOT))))
        receipt_path.write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
        if not success:
            raise RuntimeError(name + " failed:\n" + output[-4000:])
        print(name + " passed",flush=True)
    try:
        marker = "Editor R2 viewport_dominant=1 maximize_restore=1 reset=1 settings=1 draft_retained=1 scene_unchanged=1"
        run("scene", ["--exercise-editor-ux", "--frames", "74"], marker)
        saved = json.loads(preferences.read_text())
        assert saved["version"] == 1
        assert "[Docking][Data]" in saved["layout"] and "[Window][Viewport]" in saved["layout"]
        # Change a stored split reference, then require a new process to realize
        # and save that width. This qualifies layout restoration, not mouse dragging.
        block=re.search(r"\[Window\]\[World Outliner\]\n(.*?)(?=\n\[|\Z)",saved["layout"],re.S).group(1)
        dock_id=re.search(r"DockId=(0x[0-9A-Fa-f]+)",block).group(1)
        saved["layout"]=re.sub(r"(DockNode\s+ID="+dock_id+r"\b[^\n]*SizeRef=)\d+(,\d+)",lambda match:match.group(1)+"245"+match.group(2),saved["layout"])
        preferences.write_text(json.dumps(saved),encoding="utf-8")
        run("custom-layout",["--frames","12"])
        restored=json.loads(preferences.read_text())
        restored_block=re.search(r"\[Window\]\[World Outliner\]\n(.*?)(?=\n\[|\Z)",restored["layout"],re.S).group(1)
        assert abs(int(re.search(r"Size=(\d+),",restored_block).group(1))-245)<=4
        # A separate process must consume a stored user scale and preserve its valid layout.
        saved["ui_scale"] = 1.25
        preferences.write_text(json.dumps(saved),encoding="utf-8")
        run("restart", ["--exercise-editor-ux", "--frames", "74"], marker)
        assert json.loads(preferences.read_text())["ui_scale"] == 1.25
        saved["ui_scale"] = 1
        preferences.write_text(json.dumps(saved),encoding="utf-8")
        run("small", ["--exercise-editor-ux", "--width", "960", "--height", "640", "--frames", "74"], marker)
        saved = json.loads(preferences.read_text());saved["ui_scale"] = 1.5
        preferences.write_text(json.dumps(saved),encoding="utf-8")
        run("scale150", ["--width", "1280", "--height", "800", "--frames", "12"])
        saved["ui_scale"]=1;preferences.write_text(json.dumps(saved),encoding="utf-8")
        run("minimum",["--width","640","--height","480","--frames","12"])
        clip = json.loads((source / "royal_district/clips/attack.glb.daimport").read_text())["id"]
        for name, flag in (("assets","--assets-workspace"),("character","--character-workspace"),("player","--player-workspace"),
                           ("npc","--npc-workspace"),("animation","--animation-workspace"),("ability","--ability-workspace"),
                           ("items","--items-workspace"),("audio","--audio-workspace"),("vfx","--vfx-workspace"),("game","--game-workspace")):
            saved["ui_scale"] = 1
            preferences.write_text(json.dumps(saved),encoding="utf-8")
            extra=[]
            if name=="character":extra=["--authoring-asset",json.loads((source/"characters/royal_human.dacharacter").read_text())["asset"],"--exercise-character-preview","--character-preview-clip",clip,"--composer-tick","20"]
            elif name=="player":extra=["--authoring-asset",json.loads((source/"players/royal_player.daplayer").read_text())["asset"]]
            elif name=="npc":extra=["--authoring-asset",seed["npc"]]
            elif name=="ability":extra=["--authoring-asset",seed["ability"]]
            elif name=="animation":extra=["--authoring-asset",seed["reaction_action"]]
            elif name=="vfx":extra=["--vfx-select","vfx/placeholder/hit_spark.efkproj"]
            elif name=="items":extra=["--authoring-asset",json.loads((source/"items/field_ration.daitem").read_text())["asset"]]
            run(name,[flag,*extra,"--frames","12"])
        run("audio-small",["--audio-workspace","--width","960","--height","640","--frames","12"])
        preferences.write_text('{"version":999}',encoding="utf-8")
        run("invalid-preferences",["--settings-workspace","--frames","12"])
        assert json.loads(preferences.read_text())["version"] == 1
        assert scene.read_bytes() == before_scene
        if args.regressions:
            clip = json.loads((source / "royal_district/clips/attack.glb.daimport").read_text())["id"]
            mask_id=json.loads(next(iter(sorted(source.glob("masks/*.dafacemask")))).read_text())["asset"]
            kit_value=next(v for path in source.rglob("*.dakit") if (v:=json.loads(path.read_text()))["asset"]==seed["kit"])
            heavy_id=next(slot["ability"] for slot in kit_value["slots"] if slot["slot"]=="heavy")
            heavy_action=next(v["action"] for path in source.rglob("*.daability") if (v:=json.loads(path.read_text()))["asset"]==heavy_id)
            cases = [
                ("native-navigation", ["--exercise-native-navigation", "--frames", "6"], None),
                ("history-validation", ["--exercise-history-validation", "--frames", "6"], "Shared history validation"),
                ("validation-error", ["--exercise-validation-error", "--frames", "6"], "R2 failed publication retained draft/source and stopped gameplay"),
                ("binding-links", ["--exercise-binding-links", "--authoring-asset", seed["ability"], "--frames", "6"], None),
                ("effect-preview-failure", ["--exercise-effect-preview-failure", "--authoring-asset", seed["ability"], "--frames", "6"], None),
                ("pending-creation", ["--exercise-creation-drafts", "--character-preview-clip", clip, "--frames", "6"], "Creation draft Undo/Redo"),
                ("actor-fresh-play", ["--exercise-actor-workflow", "--frames", "40"], "Authored Player fresh Play"),
                ("animation-workflow",["--exercise-animation-workflow","--authoring-asset",heavy_action,"--frames","6"],"Animation workflow preserved action drafts"),
                ("mask-field",["--player-workspace","--authoring-asset",seed["mask_player"],"--mask-editor",mask_id,"--exercise-mask-field","--height","1500","--frames","12"],"Editor mask_field typed=1 undo=1"),
                ("save-all",["--player-workspace","--authoring-asset",seed["mask_player"],"--mask-editor",mask_id,"--exercise-save-all","--height","1500","--frames","16"],"Editor save_all saved=1 restored=1"),
                ("game-debug",["--exercise-game-debug","--height","1600","--frames","30"],None),
                ("vfx-preview-edit",["--vfx-workspace","--vfx-select","vfx/placeholder/burn_aura.efkproj","--exercise-vfx-edit","--frames","45"],("Editor vfx_tab ","cooks=2","playing=1","unsupported=0","refused=0")),
                ("cue-authoring",["--vfx-workspace","--vfx-select","vfx/placeholder/hit_spark.efkproj","--exercise-cue-form","--frames","20"],("Editor cue_form ","saved=1","tail=20","visual_resolved=1","audio_resolved=1")),
                ("audio-audition", ["--audio-workspace", "--exercise-audio", "--frames", "30"], "Editor audio "),
                # Previews prepare themselves when a tab opens and follow draft edits; the explicit preview gates still hold.
                ("live-previews",["--exercise-live-previews","--frames","900"],"Editor live_previews character=1 action=1 action_follows_edit=1 effect=1 graph=1 camera=1 source=unchanged revisit=1"),
                ("composer-preview",["--exercise-composer","--authoring-asset",heavy_action,"--composer-tick","20","--frames","5"],"Composer isolated pose"),
                ("graph-preview",["--exercise-graph-preview","--graph-character",json.loads((source/"characters/royal_human.dacharacter").read_text())["asset"],"--authoring-asset",kit_value["locomotion_stance"],"--composer-tick","20","--frames","5"],None),
                ("effect-preview-reset",["--exercise-effect-preview-reset","--authoring-asset",seed["ability"],"--frames","6"],None),
                ("effect-preview-reprepare",["--exercise-effect-preview-reprepare","--authoring-asset",seed["ability"],"--frames","6"],None),
                ("effect-preview-corrected",["--exercise-effect-preview-corrected","--authoring-asset",seed["ability"],"--frames","6"],None),
                ("camera-preview",["--player-workspace","--authoring-asset",seed["mask_player"],"--exercise-camera-preview","--height","1500","--frames","6"],None),
            ]
            # Each native publication exercise gets a fresh source/cache pair.
            original_base = base
            for name, options, marker in cases:
                case_source, case_cache = work/name/"sources", work/name/"cache"
                shutil.copytree(seed["source"],case_source);shutil.copytree(seed["cache"],case_cache)
                base = [case_source if x==source else case_cache if x==cache else case_cache/"cas" if x==cache/"cas" else case_source/"royal_district/RoyalTraining.dascene" if x==scene else seed["registry"] if x==seed["reaction_registry"] else x for x in original_base]
                preferences.write_text('{"version":999}',encoding="utf-8")
                run(name,options,marker)
            base = original_base
        receipt["passed"] = True
    finally:
        receipt["protected_unchanged"] = all(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest()==sha for p,sha in protected.items())
        receipt_path.write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
    if not receipt["protected_unchanged"]:
        raise RuntimeError("Protected content changed")
    (EVIDENCE / "r2-editor-ux.json").write_text(json.dumps(receipt,indent=2)+"\n",encoding="utf-8")
    print(receipt_path,flush=True)

if __name__ == "__main__":
    main()
