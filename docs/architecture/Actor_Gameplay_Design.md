# Actor gameplay design decisions and capability traceability

Confirmed product direction, 10 October 2026. This is a planned design supplement to locked DAE-007/008/009/011/012/017, not a replacement decision log or implementation receipt. The [final vision](../vision/DarkAngel_Engine_Vision.md) owns product intent; the [roadmap](DarkAngel_Implementation_Handoff.md) owns milestone acceptance; [STATUS](../implementation/STATUS.md) owns current evidence. Original DOCX, extracted decision log and historical acceptance receipts remain unchanged.

Implementation supplement (10 October2026): [ACTOR_ASSET_REPORT.md](../implementation/ACTOR_ASSET_REPORT.md) records the initial native Character skin reference and Player embedded `{kit}` loadout. This resolves the first schema placement only; full loadout fields, editor workflow/actor integration and other unresolved policies remain planned. The traceability table below describes its stated earlier checkpoint and is not a new completion table.

## Authored ownership and runtime resolution

Character is a reusable presentation definition: meshes/modular parts, skeleton/rig/skin, sockets and equipment attachment locations, default materials/appearance, compatible animation resources and optional presentation defaults. It does not own starting attributes, kits, masks, equipped items, input, cameras or AI. Attachment locations describe compatibility, not gameplay equipment ownership.

Player and NPC definitions reference Character. Plan one shared Actor Loadout configuration containing starting attribute/stat references, kit/stance configuration, up to four mask slots and equipment references. It need not be a separate asset. First inspect existing frozen kit preparation, scene composition and AbilityState lifecycle to choose an embedded typed structure rather than another gameplay manager. Player adds input, movement/camera/targeting profiles, spawn/respawn/downed/revive and progression references. NPC adds scaling, faction/targeting, BT/profile/sensing/navigation, dialog/audio and lifecycle configuration. Both resolve into the same native actor ability/effect/attribute/action infrastructure; controller and authority role differ. Authored defaults are not mutable Health, effects, cooldowns, inventory or current mask selection.

This explicitly supersedes the 9 October product-plan wording assigning kits/stats to Character. Current `CharacterPreviewResources` is prepared runtime preview data, not proof of an authored Character schema or a requirement to embed gameplay in Character. DAE authority, canonical rig, scene lifecycle and immutable asset contracts remain intact.

## Masks, equipment and source ownership

The current product direction is Fire, Earth, Water and Air, with at most four equipped masks and one selected mask/stance. Equipment and masks configure reusable grant policies: tags, persistent effects/modifiers, kits, movement/dodge profiles, locomotion/graph conditions and cue bindings. Equipped grants and selected grants have separate ownership and cleanup. Equipping Fire need not activate Fire locomotion; switching stance must remove only the outgoing selected grants and preserve equipped grants and unrelated effects.

Extend native `OwnedTags` contributor tokens, `OwnedEffects` handles and atomic AbilityState publication. Multiple sources granting a tag retain independent ownership. Reserve collision-free identities for mask instances/slots, equipment, effects, actions/abilities, actor lifecycle and approved C++/Luau extensions; define generation/epoch and retirement semantics before implementation. A client aggregate tag snapshot is not a source ledger and must not become an authoritative grant API. Replication visibility and bounded diagnostics may expose different provenance to owner, observer and server.

| Boundary | Required planned policy |
|---|---|
| Unequip / loadout replacement | Remove that source's grants/effects; prepare replacement and references before publishing; preserve other sources and captured credit |
| Active mask switch | Atomically replace selected grants/kit/profile state; keep equipped grants; reject stale input and deduplicate cues |
| Effect removal / expiry / ability interruption | Remove exact effect/action contributions; apply authored cancellation and cleanup, not blanket tag removal |
| Death / respawn | Apply declared retained/removed grants and terminal reactions; reapply eligible loadout state once on new actor lifecycle |
| Despawn / disconnect | Retire actor/session ownership, tasks and presentation; preserve independently valid effects/credit according to existing source-retirement policy |

Canonical Royal registry currently includes `State.Invulnerable`, `Status.Burn`, `State.Casting`, `State.MovementLocked` and `State.Staggered`. `Mask.Fire.Equipped`, `Stance.Fire`, `State.Dead`, `Status.Stunned`, `Status.Stagger.Heavy` and `Movement.Sprinting` are conceptual examples, not registered identifiers. Approve names/parents/visibility and generate/rebuild consumers before use. Do not rename `Status.Burn` to Burning by analogy. Unity's design uses Storm; this task adopts Air as product terminology, with asset/tag migration or alias policy unresolved and no external assets changed.

## Shared gameplay language and events

Tags describe discrete conditions; attributes numeric quantities; effects instantaneous, finite, persistent or periodic changes; typed parameters measured state; events occurrences/transitions. A persistent tag must not request an animation or sound every tick. Prefer existing action events, effect executions/snapshots/cue edges and native lifecycle boundaries over a second dispatcher.

Plan typed tag-added/removed, effect-applied/removed/stack-changed, ability-activated/committed/interrupted/completed, damage, stagger, death, mask-equipped and active-mask-changed semantics. Define which events are authoritative requests, committed results or presentation notifications. Consequential changes use bounded deterministic native phases and atomic validation; occurrence identity includes lifecycle/activation and tick/ordinal as appropriate. Predicted requests/results reconcile with server outcomes without repeating damage, grants, actions or cues. Late join reconstructs current state/loops rather than replaying expired transitions. No unrestricted event bus.

## Graphs and reaction presentation

Existing native graph nodes are Clip/Blend1D/Blend2D with Speed/Forward/Lateral inputs; this is not implemented tag-conditioned transitions or a complete layer/state designer. Plan validated bool/int/float/enum parameters with declared domains and owners: motor speed/local direction, grounded/vertical velocity/movement mode, aim and effective stance/intensity. Graphs consume committed or explicitly predicted/reconciled state; they never decide damage, legal activation or status duration.

Progressively support tag conditions, stance blend-tree selection, states/transitions, priority/interruption, layers, joint/upper-body masks, additive animation and Composer integration. Fire-to-Earth switching resolves the current stance; exiting stun resolves current locomotion rather than restoring a stale pre-stun stance. Preserve Ozz/rig compatibility, frozen generations, native corrections and motor-achieved root ownership.

Actor Reaction Profile is a proposed mapping expressed through existing graph/action definitions if adequate. It can request flinch, minor/heavy stagger, stun, knockdown, death, revival, guard break or stance/equipment-change Composers, and separate aura start/stop cues. Reuse the existing Action Composer clock/runtime. Each mapping declares trigger/condition, priority, interrupt permission, queue/retrigger/deduplication, full-body/layer scope, completion, early effect removal, terminal-state precedence and owner/server/observer behavior. Authoritative movement locks or immunity still belong to gameplay policies. Animation completion does not remove stun; a stunned actor may retain an unrelated burning aura. Effect removal can stop presentation according to policy without undoing already committed gameplay.

## Extensibility

Native C++ owns actor lifecycle, grant ownership, attribute/effect transactions, commitments/hits, movement/physics, graph sampling, prediction/networking, bounded event ordering and asset validation/publication. Data configures actors/loadouts/masks/equipment/kits/abilities/effects/graphs/reactions/profiles/cues/world interactions. Bounded Luau supplies special abilities, custom evaluators, boss/NPC decisions, encounters/quests and game event handlers where ordinary data is insufficient. Registered native and script implementations share typed approved APIs; evaluator registration and script integration remain incomplete. Neither bypasses costs, damage, effects, ownership or validation. Do not require custom Luau for ordinary modifiers/statuses or hardcode every Ashen Roots mechanic.

## Gameplay capability traceability

Sources reviewed read-only under `C:/Unity Projects/AshenRootsMP/Assets/Game/Docs`: `AshenRoots_Gameplay_Design_Document.md`, `Combat_System_TODO.md`, `Core_Traversal_System.md`, `NPC_Behaviour_Trees.md`, `World_Events_Gating_Encounters.md`, `Interactions_Inventory_Mechanisms.md`, `Altar_Checkpoint_FastTravel.md`, `Cross_Session_Persistence.md`. Their checkmarks describe Unity, not native verification. In particular Unity owner-driven movement, YAML BT and narrow JSON persistence are not native implementation prescriptions.

Status here describes requirement coverage at local checkpoint `5c6b52d`; verified subsets link through STATUS/reports, not a new milestone completion table. Newly planned means this revision adds explicit composition/authoring requirements; planned means existing roadmap scope. Dependencies are cumulative through M9 qualification.

| Ashen Roots requirement | Reusable native capabilities and owner | Current coverage / missing requirement |
|---|---|---|
| Shared Character; distinct Player/NPC | Frozen presentation refs + common loadout + native actor state; M5 Player, M6 NPC | Newly planned ownership/schema composition; current preview/session are partial foundations |
| Four masks; Fire/Earth/Water/Air; equipped vs selected | Source grants/effects + kit/profile selection; M5, durable inventory M8 | Newly planned mask/loadout lifecycle; contributor/effect primitives have scoped verified receipts |
| Shared/mask actions, light/heavy combos, utility/signature | Eight-slot native kits, commitments/costs, Composer hit/commit/combo timing; M5 | Light/heavy/Burn subset verified; complete combos, utility, charge and mask routing planned |
| Ranged/projectile/area | Native targeting, authoritative spawn/hit, effects and Composer timing; M5 | Planned; current surviving-melee binding does not prove these triggers |
| Block/parry/dodge | Ability requirements, timed windows, motor policy, validated outcomes; M4/M5 | Slots/action primitives partial; full defensive/dodge acceptance remains open |
| Sprint/jump/slide/stance movement | Motor queries + authored profiles selected by actor state; M4/M5 | Motor partial/verified subsets; profile selection and full slide policies planned |
| Wall jump/clamber/mantle/vault | Universal traversal queries/clearance/ownership/cancellation; M4 integration, M5 presentation | Planned explicit coverage; traversal must not become universally mask-gated |
| Stagger/stun/knockback/interrupt/guard break | Effects/tags/attrs + authoritative motor restrictions + reaction mapping; M4/M5 | Effect interruption and action foundations partial; complete reaction/motor integration newly planned |
| Burn, buffs, Earth armour, custom damage | Ordinary modifiers/statuses + optional registered evaluator; M5 | Burn/modifier subsets verified; final defence formula/evaluator/script interfaces planned |
| Targeting/lock-on/cameras | Authoritative aim/target validation plus Player camera presentation; M5 | Current follow camera partial; full camera modes and targeting planned |
| NPC combat/BT/sensing/navigation | Shared abilities/kits/effects + BT.CPP/JSON, Recast/Detour/Jolt perception; M6 | Planned; no Unity YAML executor port |
| Encounters/waves/world state | Cells/ledger, typed conditions/events, bounded native/Luau orchestration; M6 | Planned; transient event, durable world fact and actor combat tag retain distinct domains |
| Doors/gates/elevators/interactions/quests/dialog | Scene components + typed world definitions + validated scripts; M6 foundations, M8 progression | Planned; no competing graph execution system |
| Altars/rest/checkpoints/teleport/travel | Coordinated WorldSession lifecycle, repeatable reset, coherent host save; M6/M8 | Planned; rest resets repeatable combat, preserves durable progression; fast travel is not rest reset |
| Death/downed/revive/party wipe | Authoritative lifecycle/effects/cleanup + Player/reaction configuration; M5 foundations, M6/M8 world recovery | Death cleanup partial; downed/revive and complete party policy planned |
| Equipment/inventory/progression/persistence | Common grants + stable item identities + host roster/SQLite coherent revision; M5/M8 | Source grants newly planned; durable equip/inventory/reload integration planned |
| Auras/impacts/footsteps/music/reactions | Validated action/effect cue identities and lifetime; M5 bindings, M7 Effekseer/Amplitude | Descriptor/status presentation partial; actual audio/VFX authoring planned |
| HUD/menus/inventory/dialog | RmlUi typed bindings, source/preview first; M5 combat HUD, M8 expansion | Game developer values partial; production runtime UI and broader tools planned |
| Co-op world state/prediction/late join | WorldSession authority, bounded transport, epoch/content fences; M3–M6, M9 qualification | Scoped native/offline GNS/effect evidence; full motor/combat matrix and live EOS open |
| End-to-end production | Validated closure, fresh Play, packaging, profiling and regression; M2 foundation through M9 | First authoring loop verified subset; full production qualification planned |

General-purpose plugin ecosystems, arbitrary workflow creation, full IDE/DAW/modeler and universal node editors remain deferred until personally justified, not required capabilities.

## Dependencies, staged gates and unresolved decisions

Asset/scene transactions and frozen references precede authored actor resolution. Source-aware grants/effects precede mask switching. Authoritative typed state/event identity precedes graph/reaction binding; M7 adapters consume those cues. M6 controllers request the same M5 execution; M8 persists durable identities/configuration rather than live handles. M9 qualifies integrated content production.

After the current picker/effect-preview foundation: M5 authors one presentation Character and Player with kit/attributes; M6 adds an NPC referencing that same Character with a different kit/attributes and proves both use native gameplay execution. This cross-milestone gate does not move NPC ownership to M5. Next M5 mask slice proves equipped/selected source grants, overlapping ownership, switch/cleanup, locomotion selection and an authored reaction; M7 supplies real aura/audio content. Include failed publication, fresh generation and one authoritative test before widening the multiplayer matrix at integration checkpoints.

Unresolved before implementation: embedded loadout/schema location and versions; mask/equipment instance identity and contributor namespace; selected-kit precedence versus base/default kit; four-slot empty/duplicate policy; canonical tag names/visibility and Air/Storm migration; event API/ordering/capacity/reconciliation mapping; reaction representation/priority/queue/end policy; typed graph parameter registry/ownership and authored override compatibility; retained grants on death/respawn/disconnect; final armour/poise/damage formulas and evaluator registration; durable loadout/world-state domain mapping. Resolve with focused native contract inspection and small slices, not a monolithic rewrite.
