# Source reload and pending history — 10 October 2026

Base `542c26c`. Reload now invalidates whole shared commands touching the reloaded source, retaining unrelated Undo/Redo commands and pending creations. Shared-command atomicity is preserved; history travel continues to check exact draft/source state. Unpublished drafts cannot Reload source; the disabled button explains publication. Revert restores a pending creation baseline as a reversible edit without publishing.

Focused editor/native build, native authoring fixture and D3D12 Player form smoke passed. Native checks cover unrelated published kit reload followed by pending Player creation Undo/Redo, rejected unpublished reload and reversible pending Revert with absent source. Existing stale-source reload checks pass. The first added test used a pending effect instead of a published kit and correctly rejected; fixture corrected before passing receipt.

Evidence: [build](evidence/reload-history-build.log), [native](evidence/reload-history-native.log), [D3D12](evidence/reload-history-d3d12.log). No new multiplayer qualification. M4/M5 In progress; EOS blocked; M6–M9 not started. Next: missing-selection cleanup after pending Undo, then concise creation/reference usability.
