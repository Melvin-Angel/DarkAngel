# Contextual looping clip references

10 October 2026, after `a0eb75e`. The shared catalog/reference picker accepts a looping-only context for native locomotion graph clip fields. It excludes normalized non-looping clips while preserving them for Action Composer and Character playback; malformed loop metadata is returned as a disabled diagnostic. Existing asset IDs/catalog, source metadata and native cook compatibility remain authoritative. No parallel registry or new graph runtime.

`python scripts/verify_loop_reference.py` records build, native authoring/reference fixture and one D3D12 panel in `evidence/loop-reference.json`. Native assertions cover looping inclusion, non-looping exclusion, ordinary clip selection retention and malformed flag diagnostics; existing graph/history/publication checks also remain covered by that fixture. No physical popup interaction or new milestone qualification. M4/M5 remain In progress; EOS blocked; M6 not started.

Next: graph node/point/topology construction with coherent history and consumer validation, followed by source creation/history usability. Preserve the recently delivered Character/Player references, input overrides, native graph forms and isolated previews rather than rebuilding them.
