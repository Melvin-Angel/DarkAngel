# Shared native authoring history inspection

10 October2026 after `9c8062f`. Tools -> Authoring history reads the existing native draft history: newest-first command labels, affected source identities, bounded JSON change paths/values, Undo and Redo branches, pending source changes and catalog/source hash information. Source links navigate to Character/Player/Ability/Animation/Input Mapper; Save uses the same coordinated full-closure publication. No competing history or asset database. Scene history remains explicitly separate.

Snapshots own their strings/IDs and survive later history movement. Change previews are bounded to128 entries and512 characters per value. Undo/Redo/Save are disabled during Play. Errors appear inline and in the console. Asset creation remains explicitly outside Undo; this UI does not delete published sources.

`python scripts/verify_authoring_history_view.py` passed three focused gates (build/native authoring fixture/D3D12 history panel). Native tests cover command/source identity, source diff paths, immutable snapshots, Redo ordering and read-only inspection. GUI exercise edits a pending cost21, travels Undo/Redo, shows the changed source/path and asserts unchanged source/scene with gameplay stopped. Capture inspected; no physical navigation/button automation claim. Receipt `evidence/history-view.json`. M4/M5 remain In progress; EOS blocked; M6–M9 not started.

Next coherent foundation: extend coordinated native preparation/publication to stage new owned sources before touching the real mount, with name/UUID/type validation, exclusive creation, failed publication cleanup and previous generation retention. Then integrate pending creation and its pre-publication history into editor drafts; published-asset deletion remains deferred.
