# Pending creation selection — 10 October 2026

Base `70dd344`. Authoring workspaces clear selection when its asset is absent from the refreshed catalog/pending view. Undoing a selected pending creation now leaves an empty properties pane instead of attempting to reopen a missing source. Redo preserves the same UUID; selection can be restored explicitly.

Targeted editor build and D3D12 scripted pending creation test pass. At frame1 the selected Player is undone with Player visible; frame2 verifies cleared selection and no authoring diagnostic, restores the same Player and returns to isolated81-joint Character preview. Source files remain absent, catalog unchanged and gameplay stopped. Evidence: [build](evidence/creation-selection-build.log), [D3D12](evidence/creation-selection-d3d12.log), [capture](evidence/creation-selection.png). Native fixtures were unchanged and not repeated for this UI-only increment.

M4/M5 remain In progress; EOS blocked; M6–M9 not started. Next: ability/effect creation entry points and typed reference navigation usability, preserving pending history and complete publication/fresh Play.
