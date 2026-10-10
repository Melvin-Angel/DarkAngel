# Frozen preview policy/results — 10 October 2026

Base `2a57c97`. Effect preview displays captured source/death cleanup policy beside its operation results. Apply distinguishes a refreshed existing handle from a new effect; source cleanup explains removed effects, no matching source, or disabled frozen policy. Death cleanup, selected removal and bounded tick advance report their results. Messages are disposable preview metadata and never authored into native assets or Actor state. Captured policy remains separate from editable draft flags.

`python scripts/verify_binding_navigation.py effect-preview-results` passed targeted editor/native build, NativeAuthoringTests and one D3D12 source-ownership exercise. Native case now also prepares removal-disabled policy and verifies source/death cleanup retains the effect with policy-disabled diagnostics, plus unmatched-source diagnostics. Earlier source ownership/cleanup/expiry checks remain passing. Capture inspected: frozen policies and one removed source-owned effect result visible; source2 and ElementalDefence5 remain. Scene unchanged; no publication/live Play or new runtime/schema. No physical control activation claim.

[Receipt](evidence/effect-preview-results.json), [build](evidence/effect-preview-results-build.log), [native](evidence/effect-preview-results-native.log), [D3D12](evidence/effect-preview-results-d3d12.log), [capture](evidence/effect-preview-results.png).

M4/M5 In progress; EOS blocked; M6-M9 not started. Next: focused effect-preview failure/source context and continued creation/reference iteration. Keep registered evaluator Game-only boundary, source/cook/scene/resource/fresh isolated Play and documented publication recovery limits.
