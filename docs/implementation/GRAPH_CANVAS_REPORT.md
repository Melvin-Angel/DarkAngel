# Visual native Animation graph

Animation now shows a bounded node/connection canvas for native clip, 1D and 2D blend nodes. Root/selection highlights, input-port navigation, typed graph-scoped drag payloads and disconnected-node hints expose existing topology. Factory layout and scrolling are editor presentation only, not a second graph runtime or asset format.

Dragged input connections use NativeAuthoring.connect_graph_input, with current source checks and ordinary draft Undo/Redo. Missing nodes/ports and self/transitive cycles are rejected. Save still validates reachability, triangles, looping clips, rig compatibility and frozen consumers.

Focused native gate passed for connection Undo/Redo, rejected self/transitive cycles, missing ports/nodes and privately validated replacement of a referenced input. The first renderer fixture assumed input node1 and failed because the cooked fixture already contained nested blends; it now copies/replaces the actual root input. `python scripts/verify_binding_navigation.py graph-canvas --ui-only` then passed build/D3D12 without repeating the unchanged native gate. [Receipt](evidence/graph-canvas.json) records retained native evidence explicitly. Capture inspected.

Normal canvas draw and scripted native edits; physical pointer dragging is not automated. No source publication/live gameplay mutation. M4/M5 In progress; EOS blocked; M6-M9 not started. Tag graph states/reactions/layers remain planned. Next: visual 1D/2D blend geometry tied to typed point editing and isolated preview inputs; history stays MVP.
