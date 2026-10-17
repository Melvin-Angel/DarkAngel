# Implementation docs

This folder was reset on 17 October 2026 so agents have one small active
surface and one explicit archive.

| Location | Purpose |
| --- | --- |
| [STATUS.md](STATUS.md) | One-paragraph current status and the only known verification exceptions. |
| [CONTINUE.md](CONTINUE.md) | Fresh-phase handoff; no chronological backlog. |
| [current/CURRENT_PHASE.md](current/CURRENT_PHASE.md) | Authoritative accepted-capability baseline. |
| [future/DEFERRED_WORK.md](future/DEFERRED_WORK.md) | Consolidated, non-active deferred work and explicit out-of-scope rendering. |
| [legacy/](legacy/) | All pre-reset reports, plans, prompts, roadmaps and historical evidence references. |
| [evidence/](evidence/) | Gate logs, receipts and captures; preserved at the path used by scripts. |

`dependencies.json`, `notice-inventory.json` and `PACKAGING.md` remain at the
repository paths required by tooling. The bootstrap-generated M0 report now
writes to `legacy/tool-archive-2026-10-17/` so it cannot overwrite the active
status. These are bootstrap/package metadata, not active milestone
instructions.

The product vision and locked architecture records remain outside this reset.
The active implementation status is the current baseline above; do not infer
open work from older references inside the archive.
