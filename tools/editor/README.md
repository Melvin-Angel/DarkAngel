# Editor

The first M2 command slice is `EditorService`: revision-checked prepare/commit, atomic reflected numeric property batches, bounded undo/redo history, discarded/stale token rejection and detection of writes outside the service. Preparation validates in an isolated authoring world using the same metadata and scene serializer. Transactions identify objects by stable ID.

`EditorTests` exercises the native service without a UI. The service currently supports scenes of at most 1,024 objects and 1,024 edits per command.

The `m2-relwithdebinfo` preset builds `DarkAngelEditor`: a Win32 Dear ImGui docking shell, reflection Inspector and basic stylized static-prop viewport on Diligent D3D12 or Vulkan. Inspector edits, Undo and Redo call the native service. The viewport loads an immutable registry/CAS, without reading source assets or the catalog. Run:

```powershell
build/m2-relwithdebinfo/DarkAngelEditor.exe --registry .cache/fixtures/mask.registry.json --cas .cache/assets-real/cas --model d10003ce-2f74-41ea-928c-7cf71c8476b3 --backend d3d12
```

That UUID is this machine's adopted local fixture. Read a newly adopted fixture's UUID from its `.daimport` metadata. `python scripts/verify_m2.py --fbx PATH` performs conversion, cooking, tests and bounded captures using the actual metadata. `--frames 8 --hidden --capture PATH.png` runs a bounded viewport check.

This shell supports one loaded model and yaw. Scene assemblies, hierarchy selection, ImGuizmo, asset-browser/save UI, resource-generation reload/retirement and the agent endpoint remain pending. The Inspector uses the native World representation; the full document/provenance service is not yet implemented. See [M2 report](../../docs/implementation/M2_REPORT.md).
