# Pending reference selection recovery — 10 October 2026

Base `8c6a749`. Removing a pending creation now clears temporary Ability binding/template, Character creation and graph-preview selections if their identities disappear from the merged native inventory. Input Mapper drops an unavailable editor selection and can return to the existing loaded published input profile. Authored references and controller Player/Kit configuration are not rewritten; unresolved authored references still need validation/correction.

`python scripts/verify_binding_navigation.py pending-reference-selection` passes targeted editor build and a focused D3D12 inter-frame exercise. It duplicates and undoes an Effect selected for binding, draws Ability, verifies selection cleared; duplicates and undoes an input profile selected in Input Mapper, draws the tool, verifies its selected profile remains in native inventory. Native Undo history stays empty and authoring remains clean. No Save/publication/live Play. Capture inspected. Scene unchanged. This invokes normal form draw/creation/history paths, not physical clicks.

[Receipt](evidence/pending-reference-selection.json), [build](evidence/pending-reference-selection-build.log), [D3D12](evidence/pending-reference-selection-d3d12.log), [capture](evidence/pending-reference-selection.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: Kit copy selection recovery should provide an explicit return path to the original kit after Undo, preserving Player ownership and avoiding silent source edits.
