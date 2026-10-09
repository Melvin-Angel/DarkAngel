# First native Ability/Effect authoring loop

9 October 2026, from clean local c4072bd. M4/M5 remain **In progress**; M6 is not started. This report describes a focused authoring slice, not complete multiplayer qualification.

## Delivered workflow

Launch `powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1 -AbilityWorkflow`. Select an ability, effect or action in the left browser. Enter a new name and use Create from template or Duplicate; creation preserves compatible action/schema references and supplies fresh persistent asset IDs (and fresh cue IDs). Create ability clears costs/cooldown/damage; Create effect clears execution, period, tags, modifiers and cues so ordinary status/modifier effects need no custom formula.

The embedded center form edits costs, cooldown/activation/cancellation, named tag requirements and melee damage/radius/offsets; effect lifetime/period/stacking/cleanup, named granted/application/ongoing tags, visibility, typed statistic modifiers and persistent status labels. Action forms expose the existing timeline windows/commit marker and frozen clip selection. Edit action timeline follows the native reference; duplicate an action before assigning it if its shared timing should remain unchanged.

The right panel selects the Play kit, assigns abilities to the canonical eight slots and binds an assigned ability's native melee window to a compatible effect/magnitude. It updates UUID references and source locators together, removing obsolete bindings when the replaced ability no longer occupies a slot. All reference fields use one typed picker implementation over the existing AssetService inventory; ability/effect choices filter by attribute schema, clip choices by the native normalized-clip importer. Native cook and runtime preparation retain final reference/rig/generation validation.

Save gameplay changes writes changed actions, effects, abilities and kits in dependency order through AssetService::edit_native. Drafts retain the optimistic source hash; a failed command leaves the draft visible and restores the previous source. Create uses AssetService::create_native: new path and identity checks, non-overwriting publication, native validation/cook and failed-creation source/inventory cleanup. No external interchange file enters these commands. Revert draft and Reload source are available; source-edit undo/redo history remains open.

Enter Game and Play after saving. Play rejects unsaved gameplay drafts. The editor cooks the selected kit and every scene model/skin plus collision into a complete candidate registry in the project cache, using that cache's CAS. It prepares GPU models/skins, fresh CharacterPreviewResources, a compatible fresh input profile and a probe isolated character session before resource publication. It then creates a new isolated Preview session through the existing WorldSession authority/prediction path. Failed cook/package/runtime preparation retains the previous registry/resource leases. Stop never patches a live combat world. Game shows Health, Stamina and persistent status/timer from the current session.

## Focused verification

Reproduce `python scripts/verify_native_authoring.py --build`. Receipt: `evidence/native-authoring.json`; captures: `native-authoring-panel.png`, `native-authoring-effect-panel.png`, `native-authoring-action-panel.png`, `native-authoring-game-active.png` and `native-authoring-game-expired.png`.

The combined native fixture copies owned content to an isolated test source mount. It creates/duplicates assets, edits heavy cost30->20 and damage35->40, edits Burn duration180->120 and period60->30, binds power7, saves, cooks the dependent kit and complete scene closure and reprepares fresh Play resources. At tick30 the new session has Health60, Stamina80 and one public Burn; after three periodic executions Health39 and status expires. Its owner correction and public observer tag/status path converge with zero pending prediction. An already-prepared old package/session retains cost30, direct damage35 and its original Burn timing/magnitude. Invalid creation/edit/dependency candidates preserve previous usable source/package state; duplicate IDs/paths and stale edits reject. Authoring scene serialization remains unchanged.

The D3D12 full-editor fixture calls the same native draft/create/assign/bind/Save commands used by the forms, enters fresh Play and checks active and expired results after a new editor launch. Panel captures are inspected separately. The unchanged light action is checked through the same fresh Play path. These are scripted native commands and rendered forms, not physical mouse/gamepad automation or frame-performance receipts.

The separate GNS host/client fixture loads the same edited frozen kit/effect package. A native activation supplies effect credit, then the caster is retired before join. Owner/public clients reconstruct the edited Burn identity, duration120/period30/power7 and one cue lifetime; authoritative periodic Health100->79 arrives and the effect expires at121. This is a stationary authored-status transport check with synthetic motor delivery fences, not remote graphical/Jolt melee qualification. No protocol, prediction or physics boundary was changed.

The animation-disabled profile checks the added create API plus the existing edit rollback/hash/identity safeguards. No historical verifier chain or second graphics backend is recursively run. Original project content and external assets are unchanged by the demonstration; generated fixture sources/packages are isolated under the build cache.

## Exact remaining work

- Native source-edit undo/redo and coordinated multi-source transactions. Save is ordered per-source validation/cook; successful earlier saves remain if a later source fails, with an explicit diagnostic. This is not cross-process source/catalog atomicity.
- A visual Composer timeline and character/animation preview, structural action-block creation/removal, graph/layer/mask authoring and broader compatible thumbnail/tag picker. Current timeline forms preserve the existing block identities and native validators.
- Attribute/tag schema designers and rebuilt-consumer handling for generated tag definitions. Existing tag selections are authorable; defining new compiled tags still requires native generation/rebuild.
- Additional self/on-commit/projectile/area triggers, captures/custom formula registration, broader kit/character/player/NPC workflows and runtime cue VFX/audio. Fixed surviving-target melee effect bindings are the delivered composition boundary.
- Full M4/M5 clock/fault/streaming/provider/graphical/performance qualification remains separate; live EOS is externally blocked. Do not start M6 or mark M4/M5 Verified on these receipts.

All checkpoints are local. Nothing is pushed.
