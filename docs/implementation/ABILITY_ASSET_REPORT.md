# Frozen Ability and Attribute assets

9 October 2026, following 4bf7761. M4/M5 remain In progress. This checkpoint gives the existing WorldSession ability executor a native source/cook/load path; Royal still has no playable damage ability.

## Implemented subset

AssetService recognizes `.daability` and `.daattributes` as native JSON assets with embedded canonical UUID, `kind` and schema version 1. No identity sidecar is introduced. The existing inventory, recipe hashing, CAS, catalog transactions and package closure are reused. Supported fields are explicit and bounded; duplicate/unknown fields, invalid integer/enum values, nonfinite magnitudes, invalid resource maxima and invalid costs reject before catalog publication.

Attribute sources contain up to 64 fields, each with stable numeric `id`, `name`, `kind` (`resource` or `statistic`), `base`, `minimum`, `maximum`, `maximum_attribute`, `unit` and `visibility` (`server`, `owner`, `public`). The typed metadata survives cooking. Existing AttributeSet validation/evaluation owns defaults, bounds and derived resource maxima. Visibility records intent for subsequent replication work; it does not change current wire visibility. Flecs/DAE-001 editor reflection and authoring are still required.

Ability sources declare `action` and `attributes` UUIDs plus a source-only `sources` object containing mount-relative `action` and `attributes` locators. These must resolve to matching `.daaction` and `.daattributes` identities. Other fields are `costs` (up to eight `{attribute, amount}` entries), `cooldown_group`, `cooldown_ticks`, `activate_on` (`pressed`, `hold`, `released`, `tapped`), `minimum_held_us`, `cancel_on_release` and `interruptible`. Bounds and execution policy use the shared native runtime validator, including action preparation and resource-only cost validation. Costs need not fit the current defaults; eligibility still checks current resources at commitment.

Cooking an ability captures all three products and their input hashes as one generation. The parent product strips source locators, stores both dependency generations, and declares required action/schema edges. Runtime loading checks record uniqueness, IDs, kinds, fixed extensions, SHA-256 and dependency generation agreement. Swapping a different action generation into the registry fails even when it has the same UUID. Combined packages reject conflicting independently cooked generations through the existing package implementation.

`load_cooked_ability` returns a frozen AbilityDefinition and typed AttributeAsset solely from registry/CAS, without source paths or SQLite. AttributeAsset supplies the definitions to existing `WorldSession::configure_abilities`; the returned ability goes into the existing kit catalogue and `equip_combat_kit`. Request, costs, cooldowns, deduplication, action lifecycle and snapshots continue through existing APIs. No alternate authority path or transport is introduced. Semantic ability generation hashes include the action and attribute generations; old definition leases remain intact after dependency edits.

## Verification

Reproduce with `python scripts/verify_ability_assets.py --build`. The receipt is [ability-assets.json](evidence/ability-assets.json). Focused native checks cover frozen closure/warm cook, statistic/negative costs, fractional ticks, unsupported edges/fields, path escape and UUID mismatch, duplicate attribute IDs, failed cook preservation, dependency-only invalidation, old-generation leases, mixed/missing/duplicate/wrong-extension/corrupt products, loading with sources offline, fresh-cache identity and existing WorldSession cost/cooldown/action commitment with duplicate operation protection.

Focused native checks pass 4/4 (asset closure, ability commitment, action timeline and the existing asset pipeline); ordinary Headless checks pass 2/2. The same acceptance executable runs with the full editor build and without animation/physics in the M2 shared-library compatibility profile. Ordinary Headless checks exercise ability runtime compatibility. The primary full editor is rebuilt at `build/m5-editor-relwithdebinfo/DarkAngelEditor.exe`. This is focused verification, not a refreshed full suite, GUI authoring/graphics acceptance, network fault matrix, or new GNS/EOS qualification.

## Exact remaining gates

- Native AnimationGraph assets/cook and typed parameters/events, stance transitions, then a CombatKit source/frozen closure including its real stance dependency. Current kits remain validated in-memory definitions with optional slots and runtime swapping.
- DAE-001 reflected Ability/Attribute/Kit fields and EditorDocument/Character/Ability authoring. No new editor workflow is exposed by this checkpoint.
- Bind the real Royal attack clip/action through Composer pose/slot/layer/mask arbitration, fixed-tick root requests before motor and achieved-transform gameplay sockets after physics; bounded crossed-window sweep validation and overflow handling.
- Server hit identity/target/repeat validation and game-owned typed damage evaluation, attributed transactions, immunity/stagger/death ordering and cleanup. Timeline hit windows still do not damage anything.
- Existing WorldSession shared protocol extensions for client intent, accepted/rejected/included operations, coherent resources/actions/cooldowns and late-join snapshots; prediction replay must not duplicate costs/damage/cues. This checkpoint only uses the existing server API and diagnostics.
- Effects/tags, deferred reservations, checked Luau bindings, gameplay credit across respawn, health UI, visual motion qualification, precise Royal collision and all remaining M4 timing/fault/streaming gates.

Dependencies and original assets are unchanged. M3 live EOS remains blocked on the private deployment/policy and two identities; Loopback/GNS continuation remains authorized. M6 is not started.


Latest melee increment (9 October): [authoritative motor-relative melee](MELEE_REPORT.md) adds authored HitWindow profiles, a bound native post-physics query, registered game-owned damage evaluators, atomic Health/death/deduplication and existing Loopback baseline integration. Royal input/attack presentation, canonical socket sweeps, ability wire/prediction and the remaining M4/M5 gates are still open. Reproduce `python scripts/verify_melee.py --build`.
