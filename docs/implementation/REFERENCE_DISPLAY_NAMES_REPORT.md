# Reference field display names — 10 October 2026

Base `9b4adc4`. Shared reference fields now show the selected source filename rather than spending narrow field width on its parent directories. Full path and UUID remain in the existing closed-field hover inspection; popup filtering/entries still use complete source path/UUID and native compatibility. Identical filenames remain distinct identities and can be disambiguated through their full paths.

`python scripts/verify_binding_navigation.py reference-display-names` passes targeted editor build and one D3D12 form. Capture inspected: authored_action.daaction, player.dakit, locomotion.dagraph and light.daability names are visible at the current column widths. Scene unchanged. No physical hover/selection claim, runtime or publication change.

[Receipt](evidence/reference-display-names.json), [build](evidence/reference-display-names-build.log), [D3D12](evidence/reference-display-names-d3d12.log), [capture](evidence/reference-display-names.png).

M4/M5 In progress; EOS blocked; M6-M9 not started. Next: selected-reference hover compatibility details using the existing native picker policy, evaluated only on inspection rather than adding per-frame source reads.
