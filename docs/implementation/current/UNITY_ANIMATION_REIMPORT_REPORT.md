# Unity animation reimport

19 October 2026

## Fix

The previous converter transferred independent world-position deltas into every
joint's local translation. Rotating bones therefore changed length. The original
Earth sources show maximum relative segment-length errors of 0.975 (punch),
1.099 (kick), and 1.141 (Spear). The corrected GLBs have zero descendant
translation and bone-length error relative to the canonical bind skeleton.

Unity's canonical HumanMale Humanoid Avatar now evaluates the source clips.
The converter transfers rotations and retains fixed descendant bind translations
and unit scales. Quaternion signs remain continuous. The exact Unity composer
references select `H2H_PowerPunch01_Forward`, `AR_LeftCutKick_Forward`, and
`HumanF@ThrowBall01_R`, preserving their existing native durations and IDs.

A second defect treated hips translation and twist as actor movement. The import
now separates Unity Animator motion from the skeletal pose. Native cooking and
runtime loading accept an explicit motion-root profile while preserving the
hips' crouch, bob, and body rotation. Legacy imports retain their existing policy.
Existing ability timelines still choose how movement affects the motor.

## Library and workflow

Unity reports 1,321 clips. The final library contains 1,068 compatible clips: 28
existing reimports and 1,040 additions. Exclusions are 134 non-Humanoid
prop/UI/generic clips, 38 reference poses, 73 duplicate pack takes, and eight clips outside the
native duration bound of 1/60 to ten seconds.

New sources are organized by pack under `content/animation/unity`. Every clip has
a native import sidecar and conversion provenance. Existing IDs and loop flags
are preserved. The catalogue records original GUID/local file ID, source path,
native ID, loop flag, and duration. The importer checks original Unity source
hashes and removes its temporary Editor exporter after the batch.

See [the repeatable import workflow](UNITY_ANIMATION_IMPORT.md). The pipeline
uses Unity's Avatar evaluation and the existing native GLB/Ozz asset path.

## Validation

- AssetTool, DarkAngelEditor, AnimationClipTests, and CharacterCombatSceneTests
  build successfully.
- Six Python regression tests pass: four for retargeting and two for safe
  duplicate removal and preservation of edited files.
- Native legacy, Royal locomotion, and Earth clip tests pass, including explicit
  actor motion, independent hips pose, frozen reload, compression, masked blends,
  and rejected-import generation preservation.
- Earth compressed socket/tip errors are 0.501 mm punch, 0.592 mm kick, and
  0.489 mm Spear. Royal locomotion's maximum measured error is 0.806 mm. Explicit
  Avatar poses use a documented 1 mm codec budget; legacy budgets are unchanged.
- Combat tests pass with the corrected assets: punch damage, authored combo
  continuation, kick damage, and released Spear damage against a loaded Royal
  Guard, plus the existing combat regression checks.
- Six full editor pose captures and five actual RoyalVillage gameplay captures
  pass on D3D12. The five gameplay captures also pass on Vulkan. Inspected punch,
  kick, emergence, hand preparation, and release poses have normal proportions.
- All 1,076 Unity actor rotation tracks contain yaw only, matching the native
  movement-track contract.
- All 1,068 clips package and reload in 17 native bundles. Sampling every cooked
  frame (86,445 total) finds finite transforms and a maximum bone-length error
  of 0.05972 mm, below the independent 0.1 mm bone-length gate.
- All final GLB hashes match their conversion receipts. All 28 existing clips
  preserve their exact metadata bytes and actual authored durations.

The final independent audit passes all 1,068 clips (86,445 authored frames).
Every descendant translation and bone length matches canonical bind data exactly.
The maximum sampled Unity hand/foot/head position difference is 0.003967 mm.
All quaternion normalization checks pass. Asset scanning and the complete focused
build/Python/native/combat validation batch pass again after imports finish.

Receipts: [inventory](../evidence/unity-animation-reimport/final-inventory-summary.json),
[Unity pose audit](../evidence/unity-animation-reimport/audit.json),
[native package sampling](../evidence/unity-animation-reimport/package-summary.json),
and [validation](../evidence/unity-animation-reimport/verification.json).
New meshless clip GLBs occupy about 241 MiB; no animation FBX packages or duplicate
character meshes were copied into content.

Full-window evidence includes [punch](../evidence/unity-animation-reimport/d3d12-punch.png),
[kick](../evidence/unity-animation-reimport/d3d12-kick.png),
[Spear emergence](../evidence/unity-animation-reimport/d3d12-spear-ground.png),
[hand preparation](../evidence/unity-animation-reimport/d3d12-spear-hand.png),
[release](../evidence/unity-animation-reimport/d3d12-spear-release.png), and
[Vulkan kick](../evidence/unity-animation-reimport/vulkan-kick.png).

## Blender option

Reviewed and downloaded the official Rokoko Blender plug-in at commit
`b031e5a001b3d87e62359d18159c4e4ab479c732` into `.tools/rokoko`, with a download
receipt. It is not enabled in the user's Blender profile. The reviewed code has
account/dependency integration and changes Python's default HTTPS verification
inside its login module. Unity Avatar baking provides the automated workflow for
these assets without requiring that integration. Blender retargeting remains an
option for future sources that lack a compatible Humanoid Avatar.

## Scope limits

The full imported library receives numeric and native validation, rather than
individual visual/artistic review of every clip. Captures assess Earth on the
current canonical character. This run is not a new renderer performance audit or
a full-project test-suite qualification.
The optional Khronos Vulkan validation layer is unavailable on this machine;
Vulkan rendering and captures ran on the installed Intel driver.
