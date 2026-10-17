# Deferred work and future candidates

Date: 17 October 2026

This is a consolidated, non-active list. It exists to prevent old milestone
plans from being mistaken for current tasks. Nothing here is scheduled until a
new user decision establishes a concrete objective, dependencies and acceptance
criteria.

## Qualification exceptions from the accepted baseline

- Multi-system/cross-machine networking, including real latency/loss,
  reconnect/late-join and broader lifecycle qualification.
- EOS integration and qualification, pending external identities, deployment
  and service access.

These are the only current verification exceptions. Local gameplay and
same-machine Play/party behavior are accepted for the current phase.

## Conditional future improvements

Promote these only when Ashen Roots content or profiling demonstrates a need:

- More encounter and NPC content breadth, behavior variants and authoring
  convenience beyond the working RoyalCombat slice.
- Broader progression/pickup/world-ledger features and additional scene
  transition content beyond the currently working gameplay paths.
- Optional editor workflow refinements, layout tooling, localization and
  packaged front-end improvements.
- World-surface/planar UI panels and related U3-style interaction.
- Tiled navigation/world streaming and volumetric terrain/world authoring.
- GPU instancing, batching, frustum/occlusion culling, mesh LOD and texture
  streaming, driven by captured workloads.
- Small game-driven rendering polish such as advanced outlines, hit/status/
  dissolve effects, practical transparency, SSAO or LUT grading.

## Explicitly not planned

The following are outside the Ashen Roots engine direction and must not be
reintroduced as milestones:

- Baked global illumination, lightmap baking or dedicated lightmap UV
  authoring workflows.
- Full light-probe systems, advanced real-time GI and hardware ray tracing.
- SSR, SSGI, motion blur, depth of field and HDR10 display output.
- A replacement SR0–SR7 stylized-rendering program. R7 is the accepted
  renderer foundation; future polish must be selected individually.

Volumetric fog, atmospheric scattering, light shafts, GPU particles, temporal
reconstruction, indirect rendering and advanced GPU culling remain conditional
research only, not commitments.

## Promotion rule

Before promoting a candidate into active work, document the concrete game or
workflow problem, reuse the existing runtime/editor contracts, define a small
vertical slice, and specify the verification receipt. Do not reopen archived
milestones as a substitute for making that decision.
