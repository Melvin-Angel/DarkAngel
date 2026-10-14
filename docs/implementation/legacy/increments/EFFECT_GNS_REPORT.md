# Active effects over offline GNS

9 October 2026, following `b903cf6`. M4/M5 remain **In progress**.

Two native processes use the existing GNS transport and WorldSession protocol 3. Authority starts a real native ability, applies Burn plus 62 owner-only effects and one server-private effect, advances to tick 40 and destroys the caster before admitting the client. The joining owner receives 63 current effects; public presentation receives only Burn. Its start/end/next-period ticks and captured session/network/activation attribution survive the absent caster. Health is already 97, and preparing the current cue does not replay old damage or applications.

The client drives stationary motor ticks through 91. One later periodic execution at 61 reduces Health to 94. End-exclusive expiry at 91 prevents a third execution, removes Burn, and ends its persistent cue once. Owner/public fragmentation, separate current-state presentation, private filtering and the native authoritative result pass in both native and full-editor profiles. A bounded Python runner owns and cleans up only its host/client children and retains both logs on failure.

74/74 complete optimized native tests pass, including the new GNS CTest entry. Dedicated native/full-editor host/client checks and all generated-tag, asset, Royal light/heavy, Headless and D3D12/Vulkan compatibility pass. Reproduce `python scripts/verify_effect_gns.py --build`; hashes and linked receipts are in `evidence/effect-gns.json`.

This is localhost UDP with one owner connection and synthetic stationary motor publication. Graphical remote/Jolt play, four simultaneous remote owners, the full latency/loss/jitter/streaming/lifecycle/performance matrix and live EOS remain unqualified.

The current user priority is usable ability/effect authoring. Native authority and replication already work. Immediate blockers are editable declarative effect bindings, native Ability/Effect/Composer designers, and an explicit validated Save/cook/fresh-generation/Play loop. Checked Luau composition remains a later required extension. Dodge, scalable charge, combo/projectile, motor/status/equipment policies, typed graph/socket authoring, cue visual/audio/one-shot adapters, graphical RmlUi and broad qualification remain M4/M5 gates, but should not delay the first authoring loop. M6 has not started.
