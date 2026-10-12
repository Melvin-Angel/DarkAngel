# M8 brief from the user

Received 13 October 2026 (00:01 Copenhagen), verbatim. This is the scope of M8; it replaces the M8 rows of the roadmap where they differ. The plan built from it is [M8_REPORT.md](M8_REPORT.md). Work starts after the M7 session in progress finishes.

---

DarkAngel — M8: World State, Inventory, Scene Transitions & Co-op Progression

We are ready to move forward with M8. The goal is to establish the foundational systems needed to create a complete, playable, multiplayer action RPG level.

Prioritize clean, reusable native C++ architecture, editor integration, and multiplayer readiness. We are still early in development, so replace legacy systems where appropriate rather than introducing temporary workarounds.

Do not implement world-cell streaming yet. We want full scene transitions, persistent world state, inventory, checkpoints, and reusable interactables first.

1. World State Ledger

Implement a host-owned, shared world-state ledger that tracks persistent progression across scenes, deaths, respawns, and save/load operations.

Requirements:

* Every persistent scene object must have a stable authored ID independent of runtime entity IDs.
* The ledger must track object states, activated altars, unlocked gates, collected one-time items, completed encounters, and other persistent world events.
* Support versioned state serialization and restoration.
* Separate persistent world state from temporary runtime state.
* World objects should expose a common mechanism for saving and restoring their logical state.
* Design the ledger to support world-cell streaming later without requiring it now.
* The host owns authoritative world-state changes. Clients receive synchronized updates.
* Avoid special-case persistence implementations for every object type.

The ledger should work independently of the scene manager and should survive scene unloading and reloading.

2. Scene Management and Transitions

Implement a reusable scene transition system that supports travelling between separate levels without requiring additive streaming.

Scene transitions

* Support full scene replacement during gameplay.
* Allow doors, portals, altars, scripted events, and other interactables to request transitions.
* Scene transitions must support a destination scene asset and a named entry point or persistent destination identifier.
* Introduce placeable Scene Entry Point objects in the Scene Editor, with stable IDs, position, and facing direction.
* Preserve the world ledger, player inventories, equipped masks, player progression, and shared checkpoint state across transitions.
* Unload the current scene and load the destination scene through a centralized SceneTransitionCoordinator.
* Restore persistent world objects after loading and before gameplay resumes.
* Place players at the correct destination entry point.
* Ensure collision, gameplay entities, and required runtime systems are initialized before restoring player control.
* Provide a basic loading-screen lifecycle with transition progress or status feedback.
* Handle invalid destinations, missing entry points, and load failures gracefully.
* Avoid leaving the session in a partially transitioned state if loading fails.

Multiplayer scene transitions

* All scene transitions involving party travel must move the entire party together.
* The host authoritatively initiates and coordinates transitions.
* Clients must load the same destination scene and acknowledge readiness before the transition completes.
* Players must not resume gameplay until the party is synchronized and the destination is ready.
* Preserve player identity, inventory, equipment, and gameplay state throughout the transition.
* Support party-wide travel between different scenes, including fast travel and respawning at an altar in another scene.
* Keep the architecture compatible with future additive scene loading and world streaming.

For now, we do not need independent player travel between scenes.

3. Items and Inventory

Implement a reusable item definition and inventory system.

Items Editor

Add a dedicated Items tab to DarkAngel’s editor.

It should allow designers to create and manage item definitions with:

* Name and description
* Stable asset ID
* Item classification (Key, Consumable, Mask, Material, Quest Item, etc.)
* Inventory icon
* Optional 3D model for world representation and previews
* Stack limit and quantity rules
* Usable/equippable properties
* Classification-specific properties and behaviors

Item types should be extensible without introducing separate inventory implementations for each classification.

Masks

Masks are classified as items but retain their specialized gameplay functionality.

* Keep the existing mask definitions and combat-kit integration.
* Provide a reusable, dockable Mask Editor window.
* The same Mask Editor must be accessible from the Items, Player, and NPC editors.
* Do not create a dedicated top-level Mask tab.
* Separate mask definitions from inventory/equipment instances.
* Allow players to own masks in their inventories and equip them into their available mask slots.
* NPCs should continue to reference/equip mask definitions without requiring a player-style inventory.

Inventory

* Each player has an independent inventory.
* Support adding, removing, stacking, consuming, and querying items.
* Support unique items and future instance-specific item data.
* Inventory state must survive scene transitions, respawns, and save/load operations.
* Item grants, consumption, and equipment changes must be server-authoritative.
* Provide a basic runtime inventory UI for testing and using items.
* Provide a simple editor/debug workflow for granting items during development.

World pickups

Implement placeable pickup objects that reference item definitions and quantities.

* Players interact with pickups to collect them.
* Pickups grant items to the collecting player’s inventory.
* Support configurable collection and respawn policies.
* One-time pickups must remain collected according to the shared world ledger.
* Prevent duplicate collection during simultaneous multiplayer interactions.
* Restore pickup availability correctly after scene reloads and save/load.

4. Altars, Checkpoints and Fast Travel

Implement reusable, scene-placeable altars with configurable behavior.

Altar functionality

Altars should support:

* Activation/discovery
* Resting
* Setting the shared party respawn checkpoint
* Restoring player health and resources
* Resetting eligible world objects and encounters
* Optional fast travel functionality
* Optional save trigger

Not every altar is a fast travel destination.

Each altar must have a stable persistent ID and a configured respawn transform.

Checkpoints

* The party shares one active respawn checkpoint.
* Activating or resting at an altar can set that checkpoint according to its configuration.
* If no altar has been activated, use the configured initial party spawn point.
* Checkpoint state must persist through scene transitions and save/load.
* A checkpoint can reference an altar in a different scene.

Fast travel

* Fast travel is available only through altars configured to support it.
* Available destinations are determined by discovered/unlocked fast-travel altars.
* Fast travel moves the entire party to the selected destination.
* If the destination is in another scene, use the centralized scene transition system.
* All players must arrive at the destination altar’s configured arrival transform.
* Fast travel should not automatically change the active respawn checkpoint unless explicitly configured.
* For the initial implementation, the host can initiate fast travel. Voting or party consent can be added later.

5. Downed, Revive and Party Wipe

Implement a native C++ PartyLifecycleCoordinator.

Downed state

* When a player reaches zero health, they enter a Downed state rather than immediately dying.
* Downed players cannot perform normal combat, movement, or interactions.
* Downed state must be visible to teammates.
* A downed player remains in the world and can be revived.

Reviving

* A living teammate can revive a downed player by holding Interact within a configurable range.
* Reviving requires a configurable hold duration.
* Revive progress should be visible through a basic UI indicator.
* Releasing Interact, moving out of range, becoming downed, or otherwise invalidating the interaction interrupts the revive.
* By default, interrupted revive progress resets.
* Successful revival restores a configurable amount of health.
* Revive completion and state changes must be server-authoritative.
* Multiple players attempting to revive the same teammate must not cause duplicate completion.

Party wipe

* A party wipe occurs when all active party members are downed.
* When the final living player becomes downed, the entire party enters the wipe/death sequence.
* All party members respawn together at the last shared checkpoint altar or initial spawn point.
* If the checkpoint belongs to another scene, transition the entire party there before respawning.
* Restore health and resources according to configurable respawn rules.
* Reset eligible enemies, encounters, pickups, and world objects.
* Preserve permanent progression, inventories, equipped masks, collected one-time items, and activated altars.
* Solo play should trigger an immediate wipe when the only player reaches zero health.
* Handle disconnects and party membership changes so disconnected players cannot prevent a wipe.

Avoid introducing bleed-out timers, revive tokens, or additional complexity for the initial version.

6. Doors, Gates and Movable Platforms

Implement reusable scene-placeable interactables.

Doors and gates

Support:

* Open/close states
* Locked/unlocked states
* Interaction requirements
* Item/key requirements
* Optional item consumption
* World-state requirements
* Encounter-completion requirements
* Persistent and resettable logical states
* Optional scene transitions when activated

Doors and gates should use reusable state machines rather than bespoke scripts.

Movable platforms

Support:

* Configurable start/end transforms or movement paths
* Activation through interactions, triggers, or world events
* Movement, arrival, return, and idle states
* Configurable movement speed and behavior
* Persistent logical state when appropriate
* Reset behavior on death, rest, or scene reload
* Server-authoritative movement with client synchronization

Do not require every intermediate animation frame or movement position to be saved. Persist meaningful logical states and reconstruct transforms where appropriate.

7. Object Persistence and Reset Policies

Scene objects need configurable persistence and reset behavior.

Support distinctions such as:

* Persistent across scene reloads and deaths
* Reset on party wipe/respawn
* Reset when resting at an altar
* Reset on scene entry
* One-time objects that never reactivate once collected, completed, or consumed
* Session-only state
* Save-file persistent state

Do not implement this as one overly restrictive enum.

Separate persistence scope from reset triggers, and allow individual logical properties to have different behavior.

For example:

* A gate can remain permanently unlocked but close again after resting.
* An enemy can respawn when resting while a boss remains permanently defeated.
* A key pickup can remain collected permanently.
* A movable platform can reset to its starting position when re-entering a scene.
* An altar remains activated across deaths and scene transitions.

Expose these policies through the Scene Editor inspector with sensible defaults.

Ensure reset operations are deterministic in their ordering, idempotent where applicable, and compatible with multiplayer authority.

8. Editor Integration

These systems must be authorable through DarkAngel’s existing Dear ImGui editor.

Extend Scene / Level Design with placeable objects for:

* Scene Entry Points
* Scene Transition Triggers
* Item Pickups
* Altars
* Doors and Gates
* Movable Platforms

Provide appropriate inspectors, asset pickers, validation, and viewport gizmos.

The Items tab should be the central place for item authoring, while specialized editors such as the Mask Editor remain reusable windows.

Avoid hardcoding demo content or relying on hand-edited files for ordinary authoring workflows.

9. Multiplayer Architecture

Use the following ownership model:

* Host-owned shared world ledger
* Per-player inventories and equipment
* Shared party checkpoint
* Host-authoritative world interactions
* Host-coordinated scene transitions
* Server-authoritative downed, revive, wipe, and respawn transitions

Maintain stable authored world-object IDs separately from transient runtime entity IDs.

Design state synchronization and transition sequencing for real multiplayer, even if some qualification must wait until two-machine testing is available.

Do not implement a complete account-level persistence system yet.

10. Implementation Order

Implement in stages, compiling and validating each stage before proceeding.

1. World ledger, stable persistent IDs, and scene transition foundation.
2. Item definitions, Items Editor, inventory, mask integration, and world pickups.
3. Altars, shared checkpoints, party-wide fast travel, and save/load integration.
4. Downed state, hold-to-revive, party wipe, and coordinated respawn.
5. Doors, gates, movable platforms, requirements, and reset policies.
6. Full integration testing, editor workflows, and milestone documentation.

Prefer a coherent end-to-end implementation over isolated features that do not integrate.

11. Acceptance Test — Two-Scene Co-op RPG Slice

Create or configure a small two-scene test scenario.

Scene A:

* Two players spawn together.
* Fight several NPCs.
* Collect a temple key.
* Unlock a gate using the key.
* Activate an altar.
* Travel to Scene B.

Scene B:

* Fight an encounter.
* Activate a second altar.
* Test a movable platform.
* Fast travel back to Scene A.

Verify:

* Both players transition together between scenes.
* Each player retains their own inventory and equipped masks.
* Shared world progression survives scene transitions.
* Collected one-time items cannot be duplicated.
* Doors and gates restore their configured logical states.
* Activated altars remain discovered.
* Fast travel does not unintentionally overwrite the checkpoint.
* One downed player can be revived by holding Interact.
* When all players are downed, the party wipes and respawns together.
* Respawning in another scene works correctly.
* Enemies and objects reset according to their configured policies.
* Persistent progression survives save/load and application restart.
* No duplicate grants, invalid entity references, or inconsistent client states occur during transitions.

Include automated native tests for ledger serialization, inventory operations, state restoration, transition sequencing, and party lifecycle where practical.

For features requiring live multiplayer or external deployment, clearly report what was tested and what remains unverified. Do not claim full verification based solely on single-process tests.

Overall goal: After this milestone, DarkAngel should support building a small but complete cooperative action RPG level with inventory, keys, doors, altars, checkpoints, fast travel, scene transitions, persistent world progression, and party-based death and respawning, without needing world-cell streaming.