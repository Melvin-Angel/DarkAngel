# Local effect application failure context — 10 October 2026

Base `8ebd2fd`. Effect preview failures are scoped to their frozen snapshot, with source path/UUID, authoring revision, optional simulated source and native reason. They do not turn an intentionally blocked application into a source-authoring error. Successful preview controls clear the local error. Frozen application conditions show native matches/has results and canonical names, explaining all/any/none predicates without another gameplay rule implementation. Native calls remain individually caught to preserve GUI stacks and existing preview state.

`python scripts/verify_binding_navigation.py effect-preview-failure` passed targeted editor/native fixture build, NativeAuthoringTests and one D3D12 exercise. Native case authors None/Status.Burn, applies source1, rejects source2, verifies retained handle/modifier and contextual error, removes the blocker and proves the next successful activation is2. D3D12 retains effect1/source1 and ElementalDefence5 after source2 rejection. Capture inspected: frozen source, native requirement reason, None/Status.Burn present and blocked result visible. Pending source path absent; scene unchanged. No physical control activation, Save/publication/live Play or runtime/schema change.

[Receipt](evidence/effect-preview-failure.json), [build](evidence/effect-preview-failure-build.log), [native](evidence/effect-preview-failure-native.log), [D3D12](evidence/effect-preview-failure-d3d12.log), [capture](evidence/effect-preview-failure.png).

M4/M5 In progress; live EOS blocked; M6-M9 not started. Next: practical snapshot reset/reprepare and edited-versus-frozen preview context. Keep native evaluator Game-only boundary and complete source/cook/scene/resource/fresh isolated Play.
