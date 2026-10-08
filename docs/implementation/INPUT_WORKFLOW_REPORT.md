# Native input and Game workflow increment

Implemented 9 October 2026 after d7de329. M4/M5 remain In progress. This extends the same full editor; milestone build folders are temporary iteration checkpoints.

## Available now

Launch `powershell -ExecutionPolicy Bypass -File scripts/open_royal_scene.ps1`, select Game, press Play and click the viewport. WASD moves, Shift walks, Q/E turns, Space requests jump and Esc releases controls. Scene retains its authoring camera; Game uses the existing follow camera. Play/Stop/Pause/Step appear only in Game, alongside the docked viewport, profiler, input monitor and logs. Save/Save As and resource reload live in File. Tools contains embedded Console, Input bindings, Script source and Runtime phases. Settings has Editor/Project placeholders.

The SDK-free input manager maps keyboard, mouse and gamepad controls to stable actions and axes. Button actions emit Pressed, Hold, Released and Tapped with microsecond durations. The native `content/input/royal_player.dainput` profile is validated, cooked into frozen CAS generations and packaged with the Royal scene. EditorDocument validates native input source transactions, undo/save/open. The Tools binding table is read-only; interactive rebinding and local preferences remain open, although a validated runtime rebind API exists.

Chords put their trigger first and support None, Trigger or All consumption. Priority, specificity and stable binding ID resolve competing consuming bindings. A late chord cancels a preempted action with a balanced Released event; it cannot retract an earlier Pressed event. Consumed controls stay suppressed until neutral. Focus loss, pause, workspace changes and rebind cancel without generating taps; a held input must return neutral before rearming. Device cancellation does not cancel unrelated controls. Updates and event counts are bounded and invalid updates preserve prior state.

The Windows adapter buffers keyboard/mouse message edges, filters repeats and reconciles physical state. It dynamically loads system XInput and normalizes four gamepad slots. Gamepad polling cannot guarantee short taps between polls. Physical controller hardware has not been exercised.

The profile includes light/heavy, defensive/ranged modifiers, utility/signature and projectile chords, mask slots, interaction, dodge and lock-on intents. Shift+left click demonstrates consuming a chord. These combat intents do not execute abilities, damage, dodge or lock-on gameplay yet. Keyboard Q/E remains prototype turning and Ctrl/Alt are combat modifiers; this is not the complete Unity input scheme.

## Verification

`python scripts/verify_input_workflows.py --build` passed. [Receipt](evidence/input-workflow.json) pins implementation and binary hashes, with logs and screenshots alongside it.

- Core action/chord/consumption, tap/hold, aggregation, cancellation, rebind, bounds, invalid-profile and timestamp tests passed, including frozen cook failure preservation and EditorDocument transactions.
- Synthetic native keyboard/mouse edge, repeat and overflow tests passed. XInput API loading compiled and ran; hardware acceptance remains open.
- Seven affected native tests and four no-animation shared-library compatibility tests passed. These are scoped checks; the prior full 46/46 integration baseline was not refreshed.
- D3D12 and Vulkan each ran 120 mapped-input character ticks and workspace-switch fixtures preserving authoring data and resuming after neutral input. Existing Play/Step/undo/redo/save/open/resource reload regression passed.
- Game, Scene, bindings and Settings captures were visually inspected, including Vulkan dock retention. [Game capture](evidence/input-workflow-game-d3d12.png).

Hidden synthetic fixtures are not realtime performance proof or new GNS/EOS acceptance. Original Unity/DCC sources and dependency pins remain unchanged.

## Remaining work

Player authoring must bind character, input profile and camera configuration, with free look/aim/dialog/lock-on modes and clear ownership. NPC authoring must expose shared character/ability kits, decision profiles, behavior trees, sensing, dialogue and SFX packs; the runtime remains the native BT.CPP/JSON/Luau direction. Both are scoped in the [workflow plan](EDITOR_WORKFLOW_PLAN.md), without empty authoring tabs being presented as implementation.

Next M4/M5 gameplay work remains native abilities/attributes/effects/tags, atomic costs/cooldowns, action arbitration and authoritative hit intervals with accepted/rejected prediction and cleanup. Input intents must use the existing WorldSession protocol. Automatic clock/jitter/input lead, redundancy, fault/streaming matrices, full graph/composer authoring, posture and motion-quality gates remain open. M6 begins after required M4/M5 gates.
