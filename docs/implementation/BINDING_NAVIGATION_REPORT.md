# Binding navigation and selector layout — 10 October 2026

Base `e4c0555`. Existing Combat Kit effect-binding rows now expose Open ability and Open effect links through shared `open_native_asset` routing. Navigation resolves the row reference before changing selection and balances ImGui row IDs. Draft data, powers and history remain in their existing native authoring ownership.

Shared asset pickers display labels above full-width fields, avoiding the fixed right-hand label gutter clipping longer selector labels. Slot Edit controls occupy a separate line so they remain reachable at narrow widths. Long source paths may still truncate inside the selected field; reference popup entries retain path/UUID tooltips. This is an incremental layout change, not a docking framework.

`python scripts/verify_binding_navigation.py` passed targeted editor build and one D3D12 rendered authoring form using the existing disposable fixture. Capture inspected: Action, Play kit, Locomotion graph and slot labels are visible. Scene bytes unchanged. This smoke receipt does not claim pointer activation of the new links or a visible binding row in the initial scrolled viewport. Shared routing has its separate existing typed-navigation receipt. No native fixture/full historical matrix rerun was needed for these controls.

Evidence: [receipt](evidence/binding-navigation.json), [build](evidence/binding-navigation-build.log), [D3D12](evidence/binding-navigation-d3d12.log), [capture](evidence/binding-navigation.png).

M4/M5 remain In progress; live EOS blocked; M6–M9 not started. No runtime/schema/dependency change or publication. Next: workflow asset-list full-path/UUID search and precise reference inspection for truncated selected paths.
