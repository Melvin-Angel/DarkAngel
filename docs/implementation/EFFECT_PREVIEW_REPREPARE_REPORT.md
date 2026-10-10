# Failed Effect reprepare retention — 10 October 2026

Base `eea2d78`. Prepare preview snapshot uses Shell::prepare_effect_preview, building a replacement privately before installing it. If current source validation fails for the selected Effect, the existing snapshot records contextual Reprepare failure and remains intact; the source/cook failure is also reported through existing authoring diagnostics. Successful replacement owns a new frozen snapshot. No live resources or published heads are patched.

`python scripts/verify_binding_navigation.py effect-preview-reprepare` passes targeted editor build and one D3D12 inter-frame exercise. It authors invalid finite duration0 after preparing duration60, verifies failure preserves the same preview object/definition, one active effect and ElementalDefence5, then advances the retained state to tick30. Capture inspected: native candidate/source error and contextual Reprepare/finite-duration reason visible. Pending source remains unpublished; scene unchanged. No physical control activation, Save or live Play. Native runtime/fixture was not repeated for this GUI replacement boundary; prior preview fixture remains evidence for core state operations.

[Receipt](evidence/effect-preview-reprepare.json), [build](evidence/effect-preview-reprepare-build.log), [D3D12](evidence/effect-preview-reprepare-d3d12.log), [capture](evidence/effect-preview-reprepare.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: successful reprepare after correcting source must replace stale state explicitly, with source-path identity and frozen revision/generation visibility.
