# Unity animation import

The reusable batch pipeline is `scripts/import_unity_animations.py`. It uses
Unity's own Humanoid Avatar evaluation on the canonical HumanMale character,
then converts the sampled poses into native clip GLBs. Blender is not required.

```powershell
python scripts/import_unity_animations.py all --project "C:/Unity Projects/AshenRootsMP"
```

Stages `inventory`, `plan`, `export`, and `import` can run separately. Intermediate
files live in `.cache/animation-reimport`; `plan.json` records every selection and
exclusion. Source GUID and local file ID select exact embedded clip subassets.
The exporter is temporarily copied into Unity's Editor folder and removed in a
`finally` block. It never saves scenes, models, animations, or import settings.
Keep Unity closed during CLI batches.

The native AssetTool must be built before the import stage. Each clip is cooked
through the existing importer. Rejected imports restore their previous files;
existing source backups remain under the work directory's `originals` folder.
`content/animation/unity/clips.json` is the searchable imported clip catalogue,
including source identity, native asset ID, duration, loop setting, and skips.
Existing clip IDs, loop settings, and authored output durations are preserved.
New clip IDs are deterministic UUIDs with the engine's required version-4 bits.

## Pose and motion contract

Descendant joints retain canonical local bind translations and unit scales.
Only rotations are retargeted, using the canonical source Avatar bind pose.
The hips retain their animated local position, crouch, bob, and body twist.
Unity Animator world movement is removed from the pose and stored separately in
the GLB's `extras.darkangel_motion_root` array, with one `[x,y,z,yaw]` key per
60 Hz sample. The cooker preserves this pose and emits the explicit root track.
Legacy clips without the metadata keep their previous root extraction behavior.
Ability timelines continue deciding whether to apply movement to the motor.

Do not transfer each source joint's world-position delta into target joint
translations: rotation changes those positions and will stretch the skeleton.
Do not use the hips as the movement root for an Avatar-baked clip: doing so
removes authored body movement from the pose.

## Validation

```powershell
python -m unittest discover -s tests/python -p "test_unity_*.py" -v
python scripts/audit_unity_animation_conversion.py
python scripts/verify_unity_animation_packages.py
build/m5-editor-relwithdebinfo/AnimationClipTests.exe
build/m5-editor-relwithdebinfo/AnimationClipTests.exe --royal
build/m5-editor-relwithdebinfo/AnimationClipTests.exe --earth
build/m5-editor-relwithdebinfo/CharacterCombatSceneTests.exe
python scripts/verify_unity_animation_visuals.py
python scripts/verify_unity_animation_visuals.py --backend vulkan --gameplay-only
```

The independent GLB audit checks rigid bone lengths, finite normalized rotations,
absence of animated scale, and forward-kinematics hand/foot/head positions
against the Unity export. Native tests cover cooking, compressed pose sampling,
explicit movement tracks, malformed imports, and compatibility with legacy clips.
Rendering captures are still needed to assess visible pose and skinning quality.
The explicit-motion test profile permits at most 1 mm of compressed socket/tip
error: animated hips incur half-float translation error in the pinned Ozz codec.
Legacy stripped-pose clip budgets remain unchanged. Source GLB comparison to
Unity is separately gated at 0.1 mm, with rigid descendant translations.

## Blender retargeting option

Rokoko's official Blender plug-in was reviewed as an optional interactive
retargeting aid. A pinned source archive is available locally in `.tools/rokoko`
with its commit receipt. It has not been enabled in the user's Blender profile.
The reviewed version adds account login and third-party Python dependencies;
its login module also changes Python's default HTTPS verification behavior.
The Unity Avatar export path avoids making those changes a dependency of imports.
For future animations without a Unity Humanoid Avatar, evaluate an isolated
Blender profile before adopting an interactive retargeting add-on.

Official references:

- https://github.com/Rokoko/rokoko-studio-live-blender
- https://support.rokoko.com/hc/en-us/articles/4410463481489-Retargeting-an-animation-in-Blender-Plugin-1-1-and-above
