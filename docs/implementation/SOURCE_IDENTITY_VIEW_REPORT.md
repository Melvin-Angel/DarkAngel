# Native source identity/status inspection — 10 October 2026

Base `da8db7f`. Source identity / status is available in the selected native gameplay asset's property panel. It identifies relative path, stable UUID, catalog generation and pending/edited/saved state; shows the exact saved/creation baseline SHA and current canonical draft serialization SHA; explicitly distinguishes source hashes from frozen runtime generations. No disk reads, source changes or additional asset registry are introduced by inspection. Save remains responsible for baseline/conflict/consumer validation.

`python scripts/verify_binding_navigation.py source-identity-view` passes targeted editor build and one D3D12 form smoke with the existing disposable fixture. Scene unchanged. The collapsible section was not physically opened; this receipt claims compilation/form stability, not pointer interaction or visual verification of its conditional contents. Native/history/publication checks were not repeated for these read-only details.

[Receipt](evidence/source-identity-view.json), [build](evidence/source-identity-view-build.log), [D3D12](evidence/source-identity-view-d3d12.log).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: explicit bounded source-on-disk comparison to help diagnose stale writers before Save, retaining native checks as the final gate.
