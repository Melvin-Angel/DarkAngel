# Independent Ability and Composer creation

Create Ability now defaults to copying its native Action Composer alongside the new Ability. Disable the option to intentionally share timing. Attribute schemas and clip references remain shared; copied block IDs retain hit/commit bindings.

The editor prepares the pair in a private NativeAuthoring candidate, rewrites both action ID and source locator, and accepts them as one creation command. Undo/Redo handles the pair together before publication. Name/type/source rejection leaves the original authoring state intact. Existing publication and fresh-Play contracts remain unchanged.

`python scripts/verify_binding_navigation.py independent-ability` passed editor/native build, native authoring fixture and one D3D12 exercise. Checks cover paired references/blank defaults, Undo/Redo, independent hit timing, collision rejection preserving history/revision and private two-source consumer validation. Capture inspected: the new Ability selects its newly created Composer. [Receipt](evidence/independent-ability.json).

This increment does not yet exercise the new paired path through Save/fresh Play; that is the next integration check. Scripted API/form draw, no physical pointer claim. M4/M5 In progress; EOS externally blocked; M6-M9 not started. Published assets remain retained and publication crash-recovery limits remain.
