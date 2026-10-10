# Focused Kit binding view — 10 October 2026

Base `9eeff66`. Kit composition adds Show ability slots (on by default). Hide the eight rows to reach on-hit binding creation and existing rows immediately without changing the kit. Existing row Ability/Effect links remain through shared native routing. The toggle is editor presentation state and never enters native authored definitions/history.

`python scripts/verify_binding_navigation.py binding-links-view` passes targeted editor build and one focused D3D12 inter-frame exercise. It selects an existing authored binding, hides slots, navigates its Effect and Ability through open_native_asset, and verifies both native kinds, clean drafts and empty Undo history. Capture inspected: binding selectors, power7, row/window2 and both links visible. Scene unchanged. No physical button activation claim; no cook/publication/live Play, runtime or dependency change. Receipt includes matching native source hashes; helper now records scope and source identities for future runs.

[Receipt](evidence/binding-links-view.json), [build](evidence/binding-links-view-build.log), [D3D12](evidence/binding-links-view-d3d12.log), [capture](evidence/binding-links-view.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: source-draft state details and focused creation/reference follow-through, preserving complete fresh Play boundaries.
