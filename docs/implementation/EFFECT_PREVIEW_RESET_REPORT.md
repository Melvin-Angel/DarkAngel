# Reset frozen effect simulation — 10 October 2026

Base `d5a3cfa`. Reset frozen state reconstructs isolated native attributes/effects from the existing captured definitions and dictionary, then clears tick, handles, activation counter and local error. It prepares replacement state before replacing the existing state. Source path, definition/generation and authoring revision remain unchanged. Reprepare remains the separate private cook/validation path for current drafts.

`python scripts/verify_binding_navigation.py effect-preview-reset` passed focused editor/native build, NativeAuthoringTests and one D3D12 exercise. Native case verifies baseline restoration, empty effects/tags, retained frozen definition/revision and activation restart1. D3D12 resets after30 ticks, edits draft duration to120, then proves captured duration/end60 and activation1 remain; authoring revision mismatch is visible. Capture inspected: stale snapshot warning and reset/reprepare distinction shown. Pending sources remain unpublished; scene unchanged. No physical control activation or live Play/runtime/schema change.

[Receipt](evidence/effect-preview-reset.json), [build](evidence/effect-preview-reset-build.log), [native](evidence/effect-preview-reset-native.log), [D3D12](evidence/effect-preview-reset-d3d12.log), [capture](evidence/effect-preview-reset.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: failed reprepare must retain a usable prior snapshot with clear source/cook diagnostics, using existing private candidate publication boundaries.
