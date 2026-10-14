# Composer timeline timing gestures

9 October 2026, after fafea6b. M4/M5 remain In progress; live EOS remains externally blocked.

Delivered: drag a native action block to move it, or its interval edges to resize it. Timing remains in native 1/1024-tick units; movement preserves interval length, markers remain points, and editor clamping keeps endpoints in the action duration. Native core validation runs before each timing command. Block IDs/kinds/keys/tracks remain unchanged. Cursor-only scrubbing remains available on empty lane space.

Continuous native timing commands merge into one meaningful Undo/Redo entry. Escape, application focus loss, invalid pointer position or workflow/asset changes cancel the owned gesture. Cancellation restores its pre-command values and removes that gesture entry; beginning a new edit discards the prior redo branch as usual. Expected revision/command label and source hashes prevent cancellation from undoing a different or externally changed command. Source conflicts retain the last draft and show diagnostics; explicit Reload/reconciliation remains required. Native consumer validation still runs before Save publication; gesture clamping alone does not qualify combat semantics.

Focused verification: `python scripts/verify_composer.py --gestures --build` passes the editor build, one isolated native command test and a D3D12 panel capture. Native checks prove grouped fractional move/resize Undo/Redo, cancellation without retained mutation/history, invalid intervals/stale cancellation rejection, independent marker edits and native cooked resize publication. Receipt: `evidence/composer-gestures.json`. The UI pointer/focus paths are compiled and the panel is captured; physical pointer/Escape/focus automation is not claimed. No provider/physics/protocol or full historical matrix was rerun.

Next: practical isolated preview playback/camera and compatible reference-picker refinements, then Character/Player kit references and agreed schema tools. Full graph/layer/mask authoring and M4/M5 integration qualification remain open. Source-file/SQLite crash recovery and asset-level creation/deletion Undo remain separately deferred.
