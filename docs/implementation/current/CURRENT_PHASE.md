# DarkAngel current phase baseline

Date: 17 October 2026

This is the clean implementation baseline for the next Ashen Roots development
phase. It replaces the previous milestone-by-milestone handoff as the active
status source.

## Accepted current state

All currently implemented DarkAngel systems are accepted as verified for active
game development. This includes the renderer, materials, editor, asset and
cooking paths, scene/world authoring, physics and collision, movement,
animation, combat, abilities, effects and statuses, NPC sensing and
pathfinding, encounters, interactables and keys, inventory and mask equipment,
runtime UI, audio, VFX, save/progression foundations and local gameplay.

The RoyalCombat playtest confirms the practical slice: two enemies can kill the
player; death, respawn and dash work; status effects work; masks equip and
unequip; interactables and keys work; and NPCs sense and pathfind toward the
player.

R7 rendering is accepted for the current phase on D3D12 and Vulkan, including
HDR rendering, procedural skies, toon materials, clustered directional/point/
spot lighting, cascaded directional shadows, outlines, fog, bloom, FXAA,
exposure, tone mapping, colour grading and editor-authored persistence.

The R6 UI is accepted for its current scope. Gameplay UI uses cooked documents
through `UiService`, including HUD, markers, modal input, inventory/altar
surfaces and puzzle interaction. The old HUD path is not a competing system.

## Only current verification exceptions

The only current qualification exceptions are:

1. Multi-system/cross-machine networking qualification, including real
   latency/loss and broader multiplayer lifecycle coverage.
2. EOS qualification, which requires the external identities, deployment and
   service conditions.

Local gameplay, local party/Play behavior and same-machine evidence are not
described as missing systems. Historical evidence may describe narrower gates;
those records remain preserved in `legacy/` and do not override this baseline.

## Source-of-truth rules

- This document is the active implemented-capability baseline.
- `docs/implementation/STATUS.md` and `CONTINUE.md` are short entry points,
  not chronological logs.
- `docs/implementation/future/DEFERRED_WORK.md` is the only active deferred
  work list. Nothing there is scheduled without a new user decision.
- `docs/implementation/legacy/` contains historical reports, plans, prompts,
  receipts and superseded roadmaps. Do not infer current incompleteness from
  an archived status sentence.
- Code, schemas and editor/runtime behavior remain the implementation source
  of truth. This document records the accepted project-management boundary.
- Do not create replacement R8, R9 or SR0–SR7 programs merely to preserve old
  numbering.

## Next-phase rule

No new implementation milestone is automatically active from this reset. The
next development task must be selected from a concrete Ashen Roots need and
given its own focused acceptance criteria. Until then, preserve the accepted
baseline and the two networking/EOS exceptions above.

## Selected task: animation editor and camera routing

The current user-selected task is recorded in
[ANIMATION_EDITOR_OVERHAUL_REPORT.md](ANIMATION_EDITOR_OVERHAUL_REPORT.md).
That report supersedes this baseline only for the files and behavior it names;
it does not convert the still-open state-machine, typed-parameter or full
effect-preview design into accepted runtime functionality.

## Selected task: Royal Crown performance

The 18 October performance audit and first renderer optimization pass are in
[ROYAL_CROWN_PERFORMANCE_AUDIT.md](ROYAL_CROWN_PERFORMANCE_AUDIT.md). Average
Iris Xe performance improved substantially, but a strict 60 FPS minimum and
native-resolution performance remain unqualified. Use that report's actual
viewport dimensions, quality settings and timing boundaries when continuing.

The selected follow-up adds procedural sky/cloud caching, tighter conservative
shadow visibility, static batching with shared native overrides and Game-view
internal-resolution presets stretched across the existing panel. Exact target
720p/768p measurements and validation receipts are in
[ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md](ROYAL_CROWN_SKY_SHADOW_OPTIMIZATION.md).
Both graphics-backend feature checks pass. Its whole-suite attempt exposes an
actor-cooking closure failure outside these edits (five failing tests, six
dependent tests unrun); the report records this boundary explicitly.
