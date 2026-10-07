# Editor

The first M2 command slice is `EditorService`: revision-checked prepare/commit, atomic reflected numeric property batches, bounded undo/redo history, discarded/stale token rejection and detection of writes outside the service. Preparation validates in an isolated authoring world using the same metadata and scene serializer. Transactions identify objects by stable ID.

`EditorTests` exercises the native service without a UI. The service currently supports scenes of at most 1,024 objects and 1,024 edits per command.

The `m2-relwithdebinfo` preset builds `DarkAngelEditor`: a Win32 Dear ImGui docking shell, reflection Inspector and basic stylized static-prop viewport on Diligent D3D12 or Vulkan. Inspector edits, Undo and Redo call the native service. The viewport loads an immutable registry/CAS, without reading source assets or the catalog. Run:

```powershell
build/m2-relwithdebinfo/DarkAngelEditor.exe --registry .cache/fixtures/mask.registry.json --cas .cache/assets-real/cas --model d10003ce-2f74-41ea-928c-7cf71c8476b3 --backend d3d12
```

That UUID is this machine's adopted local fixture. Read a newly adopted fixture's UUID from its `.daimport` metadata. `python scripts/verify_m2.py --fbx PATH` performs conversion, cooking, tests and bounded captures using the actual metadata. `--frames 8 --hidden --capture PATH.png` runs a bounded viewport check.

The shell reuses one loaded model across linked scene placements. A source-authoritative EditorDocument provides stable assembly/placement patches, native preparation/activation, revisioned commands, undo/redo, partitioned scene manifests and dirty tracking. Outliner/Inspector, offscreen docked viewport, ImGuizmo gestures, source/console panels and Play/Pause/Step/Stop are implemented. UI and EditorCli share the same commands. Model/shader reload prepares candidates before publication and retires GPU resources at an explicit idle boundary. The scoped agent endpoint remains M3 work. See [M2 report](../../docs/implementation/M2_REPORT.md).


Native commands:

```powershell
build/m2-relwithdebinfo/EditorCli.exe validate .cache/editor/AcceptanceScene.dascene
build/m2-relwithdebinfo/EditorCli.exe resolve .cache/editor/AcceptanceScene.dascene .cache/editor/scene.dacooked
build/m2-relwithdebinfo/DarkAngelHeadless.exe --scene .cache/editor/scene.dacooked
```

`--exercise` adds bounded native command/reload failure checks to the editor capture run. Source FBXs are untouched. Production designer panels, deeper composition/merge/recovery UI and device/release qualification remain explicit extensions in the report.
