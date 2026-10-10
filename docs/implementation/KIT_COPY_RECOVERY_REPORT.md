# Explicit Kit copy recovery — 10 October 2026

Base `2e49bde`. Undoing a selected pending Kit copy leaves an unavailable preview selection. Kit composition now displays that condition and lets the user choose a published/pending Kit or explicitly Return to original kit when its copy origin remains available. It does not attempt to reopen the removed source, silently choose another Kit, or change an authored Player loadout. Normal kit-only copy controls remember their origin. Player-owned assignment remains explicit.

`python scripts/verify_binding_navigation.py kit-copy-recovery` passes targeted editor build and a focused D3D12 inter-frame exercise. A Kit is copied and undone; subsequent draw retains the removed selection, clean drafts and empty Undo history. Capture inspected and shows the recovery text/button. No physical button activation claim. Scene unchanged; no Save/publication or live Play; invalid selected roots continue to fail native preparation safely.

[Receipt](evidence/kit-copy-recovery.json), [build](evidence/kit-copy-recovery-build.log), [D3D12](evidence/kit-copy-recovery-d3d12.log), [capture](evidence/kit-copy-recovery.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: selected source names in reference fields, preserving complete path/UUID details and source filtering.
