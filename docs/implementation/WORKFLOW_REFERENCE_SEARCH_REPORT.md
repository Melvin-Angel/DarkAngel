# Workflow reference inspection — 10 October 2026

Base `ac0003a`. Character, Player, Ability and Animation workflow asset lists now reuse existing ASCII-case-insensitive path/UUID matching rather than filename-only matching. Hovering a row identifies its complete source path, UUID and pending-creation/catalog-generation state, allowing similarly named assets to be distinguished. Closed native reference selectors expose complete selected path and identity on hover. No new registry or reference schema.

Targeted editor build and D3D12 rendered form passed via `python scripts/verify_binding_navigation.py workflow-reference-search`. Existing reference_search fixture already covers case folding/full-path matching; no new mirror test. Capture inspected. Scene unchanged; no pointer/hover activation claim, no new publication or runtime checks. Smoke helper accepts an evidence prefix to keep earlier receipts intact.

[Receipt](evidence/workflow-reference-search.json), [build](evidence/workflow-reference-search-build.log), [D3D12](evidence/workflow-reference-search-d3d12.log), [capture](evidence/workflow-reference-search.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: explain invalid binding selections before Bind, using existing native prerequisites rather than duplicating combat execution.
