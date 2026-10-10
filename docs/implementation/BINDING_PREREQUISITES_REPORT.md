# Binding prerequisites — 10 October 2026

Base `8a1bf84`. Combat Kit Bind shows why a selected binding cannot currently be requested: missing Ability/Effect, Ability not assigned to a canonical kit slot, invalid authored melee hit-window selection, or non-finite power. The control is disabled until these form prerequisites hold. Existing NativeAuthoring::bind and private cooking remain authoritative validation; this UI does not certify tag/schema compatibility or cooked publication.

Targeted editor build and D3D12 form passed with `python scripts/verify_binding_navigation.py binding-prerequisites`; source scene unchanged. This rendered smoke does not claim physical interaction or inspection of the initially off-screen binding section. No native execution changes, full matrices or additional dependencies.

[Receipt](evidence/binding-prerequisites.json), [build](evidence/binding-prerequisites-build.log), [D3D12](evidence/binding-prerequisites-d3d12.log).

M4/M5 In progress; EOS blocked; M6-M9 not started. Next: expose reusable native Kit duplication in the kit composition panel, retaining pending creation/history/publication semantics.
