# Native source-edit command

9 October 2026, following `ced11b8`. This is the Save foundation for the requested first ability/effect authoring loop. M4/M5 remain **In progress**.

`AssetService::edit_native(relative_source, expected_source_sha256, draft)` accepts existing owned `.daability`, `.daeffect`, `.daaction` and `.dakit` sources. It rejects stale source bytes, path escapes, unsupported external formats and changes to UUID/type/schema. Source replacement uses the existing atomic file writer. The existing native cook validates and publishes the edited root. Failed cook restores the exact previous source; if another process has changed it during failure, that external edit is preserved and the conflict is reported. Existing cooked product leases remain immutable.

This is a synchronous single-source command. The draft is installed while cooking and restored on failure, rather than providing cross-process source/catalog atomicity. It does not create/clone several related assets, provide undo history, rebuild dependent kits or package the whole scene. Those operations and fresh CharacterPreviewResources/isolated Play belong to the next editor increment. External GLB/FBX assets cannot enter this path.

Focused native checks pass for successful effect-duration edits, unchanged warm generation, immutable old leases, stale writers, identity rejection, malformed/invalid-policy rollback, retained published heads, traversal rejection and preservation of an external interchange file. The matching animation-disabled check also passes. Reproduce `python scripts/verify_native_source_edits.py --build`; its completed receipt is `evidence/native-source-edit.json`. The complete historical integration/backend/provider chain is not rerun for this service edit.

Five-hour remaining reached **4%**, so no further implementation started. Finish this running focused check, record the result, commit locally and retain a clean tree. Nothing is pushed. Resume with `ABILITY_AUTHORING_CONTINUE.md`: native Ability/Effect/Composer controls plus complete scene package/fresh Play publication. There is no shipped designer UI yet, and M6 has not started.

## Follow-on authoring increment

The usage stop above is historical. The next local increment adds AssetService::create_native with a fresh owned path/identity fence, non-overwriting atomic source creation and native cook. Failed creation removes only its unchanged authored bytes and restores the inventory. Existing edit safeguards remain. The native draft command layer supplies Create from template/Duplicate, edit, assignment/binding and ordered Save; the complete scene package and fresh CharacterPreviewResources/Play path are now delivered. Read NATIVE_AUTHORING_REPORT.md for focused receipts and exact limitations. Multi-source atomicity and source undo/redo history remain open.
