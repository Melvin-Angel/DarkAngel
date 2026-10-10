# Final vision and actor gameplay planning revision

10 October 2026. Documentation only; no new implementation or runtime verification.

Canonical final product vision remains [DarkAngel_Engine_Vision.md](../vision/DarkAngel_Engine_Vision.md). The existing [M0–M9 handoff](../architecture/DarkAngel_Implementation_Handoff.md) remains the roadmap. The new [Actor_Gameplay_Design.md](../architecture/Actor_Gameplay_Design.md) is a design/traceability supplement to the locked architecture, not a competing final vision or status table. README is the repository documentation entry point; no separate documentation index exists in the inspected native tree.

Created: this report and the actor design supplement. Modified: README, canonical vision, architecture handoff, EDITOR_WORKFLOW_PLAN, STATUS, CONTINUE and ABILITY_AUTHORING_CONTINUE. The original decision-log TXT/DOCX, implementation reports, acceptance evidence and AUTHORING_CHECKPOINT_REPORT were preserved. Current continuation headers explicitly supersede that checkpoint's old scheduled implementation authorization and older next-task paragraphs.

## Decisions and scope reconciliation

Character owns reusable physical/presentation resources. Player/NPC own gameplay defaults through a common loadout, while live state stays native. Source-owned mask/equipment grants distinguish equipped from selected; tags/attributes/effects/typed parameters/events form a coherent bounded gameplay language. Graphs consume gameplay state; existing Composer presents reactions without owning effect duration. Native C++ and bounded Luau extensions use the same authoritative APIs. Workflow requirements expose provenance and reasons, not just values.

The earlier vision/editor plan assigned kits/stats to Character; this revision corrects that product-plan ownership explicitly. The locked DAE graph/action, authority, asset and scene contracts are compatible and unchanged. Unity's Storm terminology conflicts with the requested Air direction; final naming/migration remains unresolved without changing external assets. Unity YAML BT, owner-driven movement and JSON persistence are gameplay references only; native BT.CPP, authoritative motor and host SQLite choices remain.

M1/M2 verified scope and M3 transport/EOS gates are preserved. M4 planning clarifies motor-affecting stance/effect policies and universal traversal; original movement acceptance is unchanged. M5 adds explicit common Player loadout, masks/grants, typed graph/reaction and extension planning. M6 keeps NPC/AI/world ownership and adds the cross-milestone shared Character slice. M7 keeps real audio/VFX and composable cue lifetime. M8 keeps coherent host saves, durable equipment/inventory/progression and expanded UI/dialog. M9 adds integrated authoring/production qualification while preserving original release gates. No renumbering or completed milestone reopening.

The supplement maps gameplay mechanics to reusable primitives and labels partial, verified-subset, existing planned and newly planned coverage. Loadout schema placement, grant source identity, kit precedence, mask slot policy, canonical tags/Air–Storm mapping, event and reaction policies, graph parameter ownership, lifecycle retention and final evaluator/formula design remain unresolved. General-purpose ecosystems and unnecessary editors remain deferred.

## Local checkpoint and continuation

Observed HEAD: `5c6b52d6690cb3dfbd832fed578ef47b42309412`; implementation checkpoint `08f64e9`. Branch master was 11 commits ahead of local origin/master; no fetch was performed, so this is comparison to the locally stored remote reference. Tree was clean before revision and is dirty only with this documentation afterward. No commit or push was created. The existing five-gate authoring integration receipt remains historical evidence, not a test performed here.

When implementation is authorized again, begin compatible asset-reference search/filtering and resource-only cost selection, then isolated ordinary effect/modifier preview, then presentation-only Character and Player loadout integration. Preserve delivered Composer, Animation and attribute forms and full Save/cook/scene closure/resource preparation/fresh Play. Shared Player/NPC and mask/reaction slices are subsequent planned acceptance scenarios, not immediate rewrites.

The existing scheduled follow-up prompt was updated to respect this documentation-only scope and require a subsequent implementation instruction. Its schedule was preserved.

## Documentation validation

Review covered the canonical vision/roadmap/workflow/status/continuations/latest checkpoint, locked DAE graph/ability contracts, native tag/effect/graph/preview/workflow declarations and read-only Unity gameplay reference sections. Checks: Markdown local file links in revised documents, unchanged original milestone table, unchanged historical status body/checkpoint receipt, documentation-only changed paths and `git diff --check`. No build, runtime check, full suite, dependency change or external asset modification. M4/M5 remain In progress; live EOS externally blocked; M6–M9 Not started.
