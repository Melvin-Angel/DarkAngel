Continue authorized DarkAngel development in C:\DarkAngel. This is my first session using Claude Code on this project. You have no reliable prior project context, so spend substantial time understanding the vision, architecture, implemented systems, authoring workflows and evidence before changing code. Treat this as a mature, partly implemented native engine, not a greenfield project.

Implementation, useful UI/UX improvements and coherent owned local checkpoint commits are authorized. Do the work after orientation; do not stop at a plan or ask routine approval questions. Ask only when a material product decision, destructive operation, new dependency, private vendor access or other genuine missing authority prevents safe progress. Do not push, use subagents, add dependencies, modify external assets or perform a monolithic mask/reaction/AI rewrite.

1. Establish the actual workspace and protect my work.

Use C:\DarkAngel and its existing Windows-native toolchain. Start with git status, current branch, HEAD and recent commits. The last verified engine handoff was 9c43bbc3809c5b0dda7c467e539157c29390e2a9. Feature checkpoint 53c919791d9e4d372da56a59e645af0e023473d8 delivered upper-body presentation; a08cfdf0782e202e90565e2b8e03a4a60bb0f163 added reaction planning. Later documentation-only commits may follow. Inspect fresh Git rather than resetting to any of these hashes.

Preserve all user changes, particularly the untracked directories:
- content/animation/clips/
- content/characters/
- content/royal_district/RoyalCombat.dascene.records/

These are valuable user assets and scene records, not disposable test output. Never git clean, reset --hard, delete them, overwrite them with fixtures or stage them indiscriminately. Stage explicit owned files for your commits. Do not launch concurrent editor/development processes against shared project sources without understanding the existing process state.

The Codex automation darkangel-authoring-after-usage-reset has been DELETED at my request because weekly Codex usage is low. Older handoff sections saying ACTIVE or PAUSED are historical. Do not recreate it, schedule replacement work or depend on Codex-specific tools. Claude Code should work through available filesystem, shell and project tooling. Historical Codex usage thresholds are not instructions to stop Claude after a particular amount of work.

2. Read the product and technical sources of truth before implementation.

Read these documents in a deliberate order:
- docs/vision/DarkAngel_Engine_Vision.md
- docs/architecture/Actor_Gameplay_Design.md
- docs/architecture/Decision_Log.txt and relevant original decision material
- docs/architecture/DarkAngel_Implementation_Handoff.md
- docs/implementation/EDITOR_WORKFLOW_PLAN.md
- Current headers of docs/implementation/STATUS.md, CONTINUE.md, ROYAL_CONTINUE.md, ABILITY_AUTHORING_CONTINUE.md and AUTHORING_USAGE_HANDOFF.md
- docs/implementation/M5_REACTION_PRESENTATION_PLAN.md

Then read the delivered reports and relevant evidence:
- UPPER_BODY_ACTION_REPORT.md
- OBSERVED_CHARACTER_POSE_REPORT.md
- TAG_GRAPH_REPORT.md
- ACTION_WINDOW_AUTHORING_REPORT.md
- MOTOR_HUT_OVERFLOW_FIX_REPORT.md
All are under docs/implementation/. Follow relevant links to the native authoring, actor asset, creation, coordinated Save, Player/defaults and graph reports when needed to understand the actual contracts.

Check for applicable AGENTS.md, CLAUDE.md and other repository guidance. Newest continuation headers supersede older chronological instructions. The final vision describes intended capabilities, not proof that they are implemented. Reports and receipts describe specific verified profiles, not blanket milestone completion. Do not blindly repeat stale statements about creation Undo, collision failures or next tasks.

3. Understand what we are building and how the implementation supports it.

DarkAngel is a personal native C++20 engine and integrated editor for Ashen Roots, a third-person multiplayer action RPG combining Soulslike and fighting-game combat. The objective is an enjoyable, practical game-authoring environment for characters, players, NPCs, combat, behaviour and eventually encounters/world progression, not an abstract general-purpose editor or middleware showcase.

The intended production loop is: create or duplicate a native asset, configure its references/gameplay, assign it to an actor or combat kit, validate and Save/cook the complete closure, prepare fresh resources, enter isolated Play and see authoritative gameplay and synchronized presentation. Work toward this loop and the complete vision with coherent vertical increments.

Character owns reusable presentation: skin/modular meshes, rig, sockets, attachment compatibility and appearance. Player/NPC definitions reference Character and share actor loadout/gameplay infrastructure; controller-specific configuration differs. Do not move starting attributes, combat kits, gameplay equipment or AI into Character just because prepared preview resources currently contain them. Four gameplay mask slots and selected elemental stance are future loadout concepts, distinct from native animation joint masks. Preserve independent source ownership of equipped/selected grants, effects and tags.

Learn the subsystem boundaries by tracing an existing authored attack/effect end to end. Inspect engine/runtime, engine/assets, apps/editor, tools/editor, games/AshenRoots, tests and scripts. Explain to yourself where source identity, private validation, cooking, frozen generations, publication, resource preparation, authority, prediction, observer state and rendering happen. Trace failure retention as carefully as the success path. Give me a concise orientation summary and your chosen implementation approach, then continue without waiting for approval.

Core contracts to retain:
- AssetService owns native UUID sources, import metadata, rebuildable SQLite catalog, SHA-256 CAS, typed dependency closures and immutable frozen generations. No second asset database or parallel gameplay format.
- Candidate preparation and validation precede publication. Rejected edits retain old source/catalog/playable generations. Full scene closure and fresh CharacterPreviewResources preparation matter; source edits alone do not update an existing Play generation.
- WorldSession owns gameplay authority through the existing shared session/protocol paths. Never add a local-host authority bypass, client-only damage/cost path or separate networking runtime.
- Jolt fixed-tick motor and disposable prediction/replay worlds retain their ownership. Animation presentation never moves the actor or decides legal gameplay.
- Flecs/world/scene lifecycle and stable IDs remain as documented. Luau and registered C++ extensions use bounded approved APIs; ordinary statuses/modifiers should not require custom scripting.
- Public/owner/server visibility, epochs, stale-generation fences, source attribution and deterministic bounded work remain explicit.
- Generated tags require the native consumer regeneration/rebuild fences. Inspect actual registries; conceptual tag names in design docs are not automatically registered.

4. Recognize the current completed boundary.

Already delivered: native Ability/Effect/Kit/Character/Player authoring foundations, MVP draft history and creation workflows, coordinated Save/fresh Play, action-window tag forms, registered tag-conditioned graph selectors, public observed actor poses and upper-body action presentation.

Upper-body actions now reference existing native .damask assets through the Composer. Prepared complementary weights preserve locomotion below the mask and replace selected joints with the active action. Owner and public observed poses share the sampler. A masked actor uses at most three locomotion layers plus one action, with one pose sampling pass. Masked kits with a four-layer locomotion bound reject preparation/Save; nothing is silently truncated. Full-body-only graphs retain four layers. Frozen rig/clip/mask compatibility and upper-body no-motor root ownership are checked. The owned content/animation/upper_body.damask uses a content-root-compatible locator. Default Royal actions remain full-body until explicitly authored.

Seven focused gates passed via python scripts/verify_upper_body_action.py --build. Native fresh Play reached tick150 with target Health39, owner Stamina80, no active action, prediction pending0 and unchanged authoring scene. D3D12 checked the frozen masked preview and active Heavy at tick30 with Health60/Stamina80/pending0. These are scoped offline/native receipts, not multiplayer graphical or whole-M5 qualification.

The hut overflow bug is fixed using enhanced internal-edge removal, retaining cap32 and its guard. All12 approaches and288 isolated replay batches passed. Historical "not reproduced/no fix" statements are superseded. An interactive hut-jump retest is useful but should not consume the session or displace authoring progress.

History stays MVP. Creation Undo ends at publication; published assets are retained. Publication is still not crash-atomic or a cross-process multi-file reader snapshot. Do not silently claim those limitations are solved.

Eight combat slots remain Light, Heavy, RangedLight, RangedHeavy, Spell1, Spell2, Block and Parry. Utility dodge ingress remains unresolved. Do not invent slot9 or repurpose an existing slot.

M4/M5 are In progress. Live EOS qualification is externally blocked. M6-M9 have not started. Shared target presentation is not an NPC controller or AI milestone.

5. Continue with the first bounded M5 reaction presentation slice.

Start from docs/implementation/M5_REACTION_PRESENTATION_PLAN.md. It is planning only, not an implemented reaction system or a locked schema. Review its proposed ownership against the actual effect cooker, NativeAuthoring validation, frozen kit closure, EffectPresentation and existing pose consumers before finalizing fields. Refine the plan where implementation evidence warrants, documenting the reason while preserving the vision and locked contracts.

The first concrete goal: author a short finite public effect which authoritatively interrupts an actor's attack and presents one prepared full-body flinch. Implement this as a coherent native/cook/authoring/fresh-Play slice, not merely a helper function or hard-coded demo.

Prefer a small optional presentation binding through the existing native effect/action assets if it fits their ownership and dependency contracts. Reuse the Action Composer, frozen clips/rigs and current native pose infrastructure. Do not automatically introduce a standalone reaction format, general event bus or competing action runtime.

Use checked effect-instance handle, owner/avatar/session epochs, definition generation and start/end clocks. Aggregate tags cannot reveal the contributing source or onset time. Late join should sample a still-current reaction clock and should not replay an expired onset. Duplicate/stale frames and ordinary refresh updates must not restart a finished one-shot.

Define deterministic priority/tie-breaking, refresh/retrigger, suppression, completion, early removal and terminal precedence before coding. For the first slice, keep at most one visible reaction, use root policy none and execute no gameplay blocks from presentation. Authoritative effect interruption cancels gameplay through the existing AbilityState/WorldSession path; presentation must not locally cancel an unrelated active action. Health0 suppresses flinch without pretending to provide a death-animation system.

The effect owns status duration and gameplay legality. Finishing the flinch must not remove stun/status; removing or suppressing its effect should end its presentation without clearing unrelated sources. A burning aura can coexist. Continue locomotion underneath and restore current actor state rather than a saved pre-reaction stance. Preserve the four-layer budget. No reaction queues, additive/equipment overhaul, broad graph-state rewrite or NPC/AI expansion in this first increment.

6. Improve UI/UX where it helps actual authoring.

You are authorized to make UI/UX upgrades where you see fit. Use judgment and the final vision, not just the existing layout. Prioritize clear discoverability, readable selectors, useful previews, sensible grouping, direct reference navigation and short create/configure/Save/Play paths. Useful broader layout improvements are welcome when coherent and maintainable; you do not need permission for each control or panel adjustment.

Use Blender-like workflow tabs and Ableton-like focused views, with embedded panels and contextual tools. Keep one integrated editor product, shared compatible asset picking and consistent selection/state. Avoid window sprawl, duplicate inspectors, an elaborate plugin framework and polishing history instead of delivering game-authoring capability. Keep Game as the Play/Stop/Pause/Step owner. Clearly distinguish dirty drafts, private/frozen previews, saved/cooked assets and live state. Preserve existing commands, cancellation, validation, history and publication boundaries behind improved controls. Do not expose implementation trivia unless it helps an author make a decision.

7. Build, verify, document and checkpoint responsibly.

Inspect CMakePresets.json, the existing build directory build/m5-editor-relwithdebinfo, pinned tools under .tools and scripts/msvc_environment.py. Prefer existing verification scripts that activate the pinned MSVC environment rather than inventing a toolchain, assuming a Unix shell or installing dependencies. The normal Royal launcher is:

powershell -ExecutionPolicy Bypass -File .\scripts\open_royal_scene.ps1

Read it before using it. D3D12 is the default; -Backend vulkan selects Vulkan. Avoid repeatedly launching interactive editors during builds or tests.

Choose focused meaningful checks around a coherent chunk. For the reaction slice, demonstrate edit -> Undo -> Redo -> private validation -> coordinated Save -> complete scene closure -> fresh resources -> fresh isolated WorldSession Play. Check interruption, removal/suppression/expiry, unrelated-source retention, authoritative cost/damage, public visibility, actor/source/generation fences, duplicates/stale frames/current-state late join and layer/root bounds. Prove failed Save/preparation retains prior source/catalog/resources.

Use disposable copied fixtures for source edits and one D3D12 graphical check. Inspect captures when possible; report any visual verification you could not actually perform. Rebuild the actual editor for implemented UI/runtime changes. A script/API/form draw is not physical pointer automation. Do not run every historical suite after each small edit or create tests that merely mirror the implementation. Broaden verification when shared protocol/provider/physics changes, failures or unresolved concerns justify it. Do not repeatedly rerun completed native gates for a later isolated layout adjustment; record what was retained and what was rechecked.

Maintain short progress updates explaining findings, decisions and remaining uncertainty. Make coherent owned local commits after meaningful completed increments. Update STATUS, an implementation report/evidence receipt and relevant continuation headers for substantive work. Preserve historical evidence and distinguish Implemented, Verified, Planned, In progress, Blocked and untested work accurately. Only revise the final vision if product intent truly changes.

Do not stop after orientation or present a plan as completion. Deliver the bounded reaction authoring slice when feasible, then continue with the next justified increment toward the documented vision if time/context permits. If something genuinely blocks completion, preserve working progress and write a precise handoff with exact HEAD, owned changes, tested boundaries, failures, remaining gates and the next action. Do not leave a half-finished experimental rewrite disguised as a completed feature.
