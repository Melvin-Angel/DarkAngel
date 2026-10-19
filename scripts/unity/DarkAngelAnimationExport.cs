// Temporary Unity Editor entry point. The Python importer installs this file for
// a batch run, samples on the owned canonical Avatar, then removes the copy.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Animations;
using UnityEngine.Playables;
using Object = UnityEngine.Object;

public static class DarkAngelAnimationExport
{
    const string TargetModel = "Assets/thirdparty/3D/Blink/Art/Characters/LowPoly/FREE_HumanLowPoly/Meshes_Humans/HumanMale_Character.fbx";
    [Serializable] public class ClipInfo {
        public string guid, path, name, error; public long localId;
        public float length, frameRate; public bool humanoid, loop, legacy;
    }
    [Serializable] public class Inventory { public string unityVersion, targetModel; public List<ClipInfo> clips = new List<ClipInfo>(); public List<string> errors = new List<string>(); }
    [Serializable] public class Joint { public string key; }
    [Serializable] public class Skeleton { public Joint[] joints; }
    [Serializable] public class Selection { public ClipRequest[] clips; }
    [Serializable] public class ClipRequest { public string guid, output; public long localId; public int sampleRate = 60; public float playRate = 1; public int outputTicks; }
    [Serializable] public class Track { public string key; public float[] restWorldPosition, restWorldRotation, worldPositions, worldRotations; }
    [Serializable] public class Sample {
        public int schema = 2, sampleRate, frameCount; public string name, sourcePath, sourceGuid, targetModel;
        public long sourceLocalId; public bool loop; public float length; public float[] frameTimes; public Track[] tracks;
        public float[] restActorPosition, restActorRotation, actorPositions, actorRotations;
    }
    [Serializable] public class Receipt { public List<string> exported = new List<string>(); public List<string> failed = new List<string>(); }

    static string Arg(string name) {
        var args = Environment.GetCommandLineArgs(); var index = Array.IndexOf(args, name);
        if (index < 0 || index + 1 == args.Length) throw new ArgumentException("Missing " + name);
        return args[index + 1];
    }
    public static void InventoryClips() {
        var inventory = new Inventory { unityVersion = Application.unityVersion, targetModel = TargetModel };
        foreach (var path in AssetDatabase.FindAssets("t:AnimationClip", new[] { "Assets" }).Select(AssetDatabase.GUIDToAssetPath).Distinct().OrderBy(p => p, StringComparer.Ordinal)) {
            try {
                foreach (var clip in AssetDatabase.LoadAllAssetsAtPath(path).OfType<AnimationClip>()) {
                    if (clip.name.StartsWith("__preview__", StringComparison.Ordinal)) continue;
                    AssetDatabase.TryGetGUIDAndLocalFileIdentifier(clip, out string guid, out long localId);
                    inventory.clips.Add(new ClipInfo { guid = guid, localId = localId, path = path, name = clip.name,
                        length = clip.length, frameRate = clip.frameRate, humanoid = clip.humanMotion,
                        loop = AnimationUtility.GetAnimationClipSettings(clip).loopTime, legacy = clip.legacy });
                }
            } catch (Exception error) { inventory.errors.Add(path + ": " + error.Message); }
        }
        File.WriteAllText(Arg("-darkAngelInventory"), JsonUtility.ToJson(inventory, true));
        Debug.Log("DarkAngel animation inventory: " + inventory.clips.Count + " clips, " + inventory.clips.Count(c => c.humanoid) + " Humanoid");
    }
    public static void ExportClips() {
        var selection = JsonUtility.FromJson<Selection>(File.ReadAllText(Arg("-darkAngelSelection")));
        var skeleton = JsonUtility.FromJson<Skeleton>(File.ReadAllText(Arg("-darkAngelSkeleton")));
        var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(TargetModel);
        if (!prefab) throw new InvalidOperationException("Missing canonical Unity character " + TargetModel);
        var receipt = new Receipt();
        foreach (var request in selection.clips) {
            try {
                var path = AssetDatabase.GUIDToAssetPath(request.guid);
                var clip = AssetDatabase.LoadAllAssetsAtPath(path).OfType<AnimationClip>().Single(c => {
                    AssetDatabase.TryGetGUIDAndLocalFileIdentifier(c, out string guid, out long localId); return localId == request.localId;
                });
                if (!clip.humanMotion) throw new InvalidOperationException("Clip is not Humanoid; cannot use Avatar retargeting");
                Export(prefab, clip, request, skeleton.joints, path);
                receipt.exported.Add(request.output);
                if (receipt.exported.Count % 25 == 0) Debug.Log("DarkAngel exported " + receipt.exported.Count + "/" + selection.clips.Length);
            } catch (Exception error) { receipt.failed.Add(request.guid + ":" + request.localId + " " + error); }
        }
        File.WriteAllText(Arg("-darkAngelReceipt"), JsonUtility.ToJson(receipt, true));
        Debug.Log("DarkAngel animation export complete: " + receipt.exported.Count + " exported, " + receipt.failed.Count + " failed");
        if (receipt.failed.Count > 0) Debug.LogWarning(string.Join("\n", receipt.failed.Take(5)));
    }
    static float[] V(Vector3 value) { return new[] { value.x, value.y, value.z }; }
    static float[] Q(Quaternion value) { return new[] { value.x, value.y, value.z, value.w }; }
    static void Export(GameObject prefab, AnimationClip clip, ClipRequest request, Joint[] joints, string sourcePath) {
        var scene = EditorSceneManager.NewPreviewScene();
        var instance = Object.Instantiate(prefab); SceneManagementMove(instance, scene);
        PlayableGraph graph = default;
        try {
            instance.transform.SetPositionAndRotation(Vector3.zero, Quaternion.identity); instance.transform.localScale = Vector3.one;
            var animator = instance.GetComponentInChildren<Animator>();
            if (!animator || !animator.avatar || !animator.avatar.isValid || !animator.avatar.isHuman) throw new InvalidOperationException("Canonical Avatar is not valid Humanoid");
            animator.runtimeAnimatorController = null; animator.cullingMode = AnimatorCullingMode.AlwaysAnimate;
            animator.applyRootMotion = true; animator.fireEvents = false; animator.Rebind();
            var transforms = instance.GetComponentsInChildren<Transform>(true).GroupBy(t => t.name).ToDictionary(g => g.Key, g => g.First());
            var bones = joints.Select(j => transforms.TryGetValue(j.key, out var bone) ? bone : throw new InvalidOperationException("Missing canonical bone " + j.key)).ToArray();
            var ticks = request.outputTicks > 0 ? request.outputTicks : Mathf.CeilToInt(clip.length / request.playRate * request.sampleRate);
            var count = Math.Max(2, ticks + 1);
            var sample = new Sample { sampleRate = request.sampleRate, frameCount = count, name = clip.name, sourcePath = sourcePath,
                sourceGuid = request.guid, sourceLocalId = request.localId, targetModel = TargetModel,
                loop = AnimationUtility.GetAnimationClipSettings(clip).loopTime, length = clip.length, frameTimes = new float[count], tracks = new Track[joints.Length] };
            sample.restActorPosition = V(animator.transform.position); sample.restActorRotation = Q(animator.transform.rotation);
            sample.actorPositions = new float[count * 3]; sample.actorRotations = new float[count * 4];
            for (var i = 0; i < bones.Length; i++) sample.tracks[i] = new Track { key = joints[i].key,
                restWorldPosition = V(bones[i].position), restWorldRotation = Q(bones[i].rotation),
                worldPositions = new float[count * 3], worldRotations = new float[count * 4] };
            graph = PlayableGraph.Create("DarkAngel read-only clip sampling"); graph.SetTimeUpdateMode(DirectorUpdateMode.Manual);
            var playable = AnimationClipPlayable.Create(graph, clip); playable.SetApplyFootIK(false); playable.SetApplyPlayableIK(false);
            var output = AnimationPlayableOutput.Create(graph, "Canonical Humanoid", animator); output.SetSourcePlayable(playable);
            graph.Play();
            float previous = 0;
            for (var frame = 0; frame < count; frame++) {
                var time = clip.length * frame / (count - 1); sample.frameTimes[frame] = frame / (float)request.sampleRate;
                graph.Evaluate(time - previous); previous = time;
                Array.Copy(V(animator.transform.position), 0, sample.actorPositions, frame * 3, 3);
                Array.Copy(Q(animator.transform.rotation), 0, sample.actorRotations, frame * 4, 4);
                for (var i = 0; i < bones.Length; i++) {
                    Array.Copy(V(bones[i].position), 0, sample.tracks[i].worldPositions, frame * 3, 3);
                    Array.Copy(Q(bones[i].rotation), 0, sample.tracks[i].worldRotations, frame * 4, 4);
                }
            }
            Directory.CreateDirectory(Path.GetDirectoryName(request.output)); File.WriteAllText(request.output, JsonUtility.ToJson(sample));
        } finally {
            if (graph.IsValid()) graph.Destroy(); Object.DestroyImmediate(instance); EditorSceneManager.ClosePreviewScene(scene);
        }
    }
    static void SceneManagementMove(GameObject instance, UnityEngine.SceneManagement.Scene scene) { UnityEngine.SceneManagement.SceneManager.MoveGameObjectToScene(instance, scene); }
}
