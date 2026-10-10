# Shared history source comparison — 10 October 2026

Base `5b7f910`. Open-source rows in shared authoring history expose Compare disk source through the same read-only NativeAuthoring method. Its revision/result observation is displayed per matching identity, with stale-authoring and no-lock/Save-revalidation guidance. No duplicate comparison or publication logic; history remains independent of scene transactions.

`python scripts/verify_binding_navigation.py history-source-comparison` passes targeted editor build and one D3D12 source/history exercise. It captures an external whitespace conflict, restores exact original fixture bytes, confirms a fresh comparison matches, then shows the shared history with the earlier observation. Source history/revision remain unchanged. Capture inspected: Compare controls, captured conflict and empty Undo/Redo visible. No physical button activation, publication/live Play/runtime/schema change. Native comparison fixture passed in the previous increment and was not repeated for the shared access point.

[Receipt](evidence/history-source-comparison.json), [build](evidence/history-source-comparison-build.log), [D3D12](evidence/history-source-comparison-d3d12.log), [capture](evidence/history-source-comparison.png).

M4/M5 In progress; EOS blocked; M6-M9 not started. Next: source/history filtering and readable navigation for larger open draft sets, retaining native transaction and fresh-Play boundaries.
