# Native Kit copy controls — 10 October 2026

Base `320768a`. Kit composition exposes Duplicate combat kit / New kit name / Create kit copy, reusing NativeAuthoring::duplicate and pending-source history. It copies current native kit references with a fresh asset UUID. Kit-only preview selection moves to the copy. If a Player owns the Play kit, its kit assignment is retained and the console identifies the created Kit for explicit Player assignment. Creation Undo remains pre-publication only; Save still validates the complete native closure.

Targeted editor build and D3D12 form smoke passed: `python scripts/verify_binding_navigation.py kit-creation-controls`. No physical creation-control activation claim. Underlying six-source Kit creation/publication/fresh Play is covered by the existing CREATION_VERTICAL_REPORT; not rerun for this form-only exposure. Scene unchanged; no runtime/schema/dependency change.

[Receipt](evidence/kit-creation-controls.json), [build](evidence/kit-creation-controls-build.log), [D3D12](evidence/kit-creation-controls-d3d12.log).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: pending reference selection cleanup after creation Undo, without modifying authored Player references or silently rewriting sources.
