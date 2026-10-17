# R3 Movement Authoring

16 October 2026. **Implemented and locally verified** (see *Verification* below for exactly what ran). This delivers R3 (feature A) in the [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026) against the [gameplay configuration design](../future/GAMEPLAY_CONFIGURATION_DESIGN.md), whose [R3 implementation decisions](../future/GAMEPLAY_CONFIGURATION_DESIGN.md#r3-implementation-decisions-16-october-2026) section records the frozen ranges and every choice made while the user was unavailable.

## Delivered behavior

- **Movement Profile asset** (`.damovement`, under `movement/` in the source root): walk speed, crouch speed, acceleration, jump velocity, gravity, coyote time and jump buffer. It is a leaf native source cooked to a `movement.json` product, with the same catalog, preview, validation, coordinated Save and history paths as the other native gameplay sources.
- **Shared reference from the common Actor Loadout**: `loadout.movement` plus the `sources.movement` locator, on both Players and NPCs. An actor without a reference keeps the exact legacy built-in profile, so existing content, including protected Royal sources, is unchanged.
- **One Movement panel in the Player and NPC tabs**: pick a profile, edit the shared profile's fields (bounded inputs; timers in whole ticks with milliseconds shown), *Create profile*, *Duplicate and assign*, *Use built-in profile*. Profiles appear in the Player and NPC browsers and open with the same fields. Undo/Redo and coordinated Save use the existing native authoring history; no profile-only store was added.
- **Runtime**: an immutable `MotorParameters` value (`engine/runtime/include/darkangel/motor_parameters.hpp`) is validated against named ranges and frozen in every `CharacterMotor`. Host party members and the owner's prediction use the Player profile; spawned and encounter NPCs use their own. Prediction-world replacement, resynchronization and `OwnerPrediction` replay all construct their motors from the same profile. `replay_motor` takes the profile explicitly; the default argument remains the legacy profile for existing callers.
- **Fresh Play only**: a session keeps the profile it was prepared with; saving during Play does not change live motors.

## Implementation notes

- The motor's formulas are unchanged; only the literals became fields. The legacy profile reproduces the previous floating-point expressions exactly, and a motor test compares an explicit legacy profile against the default constructor tick for tick.
- Coyote time and jump buffer may now be up to 30 ticks, so `validate_motor_state` and the wire reader accept timers up to `motor_timer_tick_limit` (30) instead of a literal 6. The wire layout is unchanged.
- Session fingerprint: the editor handshake already hashes the Player product generation and each NPC product generation; both digests cover the frozen closure, which now includes the movement product. A changed profile therefore changes the fingerprint, and a host/guest pair with different profiles refuses readiness through the existing mismatch path. The native authoring test asserts that both generations change.
- `motor_parameters.hpp` and `motor_state.cpp` were added to the cooker hash inputs because the cooker validates profiles with them.

## Verification

All on this machine (D3D12, pinned MSVC 14.51 / Windows SDK 10.0.26100.0, `VCPKG_MANIFEST_INSTALL=OFF`, `D3D12_SUPPORTED=TRUE`; no third-party objects rebuilt). Same-machine evidence only.

- **Guarded full build** of every target passed. The first build compiled cleanly; two follow-up fixes were needed during focused testing: the asset catalog's importer allowlist (`native-movement-v1`) and cached-product extension allowlist (`movement.json`).
- **Focused tests**: `M4.motor` (new movement-profile block: exact legacy equivalence, authored walk speed, jump velocity, gravity and coyote time, range refusal at validation and motor construction, owner replay with a non-default frozen profile), `M5.native_authoring` (new block: legacy cook without a reference, create, duplicate and assign, Undo/Redo, out-of-range and missing-locator refusal without publication, coordinated Save, frozen Player and NPC profiles, generation change, clearing back to legacy) and `M5.actor_assets`.
- **Full CTest: 114/114 passed** serially in 628 s ([log](../evidence/r3-ctest.log)).
- **Editor gates: 54/54 D3D12 gates passed** through the composite authoring gate script (editor-only, after the legacy NativeAuthoringTests entry point regenerated the fixture) ([log](../evidence/r3-editor-gates.log)); the per-gate logs and captures in `evidence/` were refreshed by this run.
- **M8 slice gates passed**: solo, save/load, two-player party and party travel ([log](../evidence/r3-m8-slice.log)). These exercise host, owner prediction, replay and a guest client with the legacy profile through the new plumbing.
- Protected Royal content was not modified; the new profiles in the authoring test live in the disposable fixture.

## Deferred or not done

- **No dedicated editor UI gate** for the Movement panel. The panel is covered by the native authoring commands it calls (create, duplicate, assign, clear, field edits, Undo/Redo, Save and failure retention in `M5.native_authoring`); a scripted ImGui exercise of the panel itself was not written.
- **No new separate-process host/guest gate with non-default profiles.** The mismatch refusal relies on the existing fingerprint check, and coverage of the new dependency is shown by the generation-change assertion rather than by a two-process run with differing profiles.
- **No consumer list** in the panel; it says the profile is shared instead.
- New Players and NPCs do not get an owned profile file on creation (see the design's decisions section); the built-in profile is identical to the legacy default.
- Slope limit, step height, capsule geometry, sprint, air control and braking stay out of scope as the design specifies.
