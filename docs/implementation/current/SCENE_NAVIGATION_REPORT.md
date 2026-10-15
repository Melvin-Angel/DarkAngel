# R1 Scene Editor Navigation

15 October 2026. **Implemented and locally verified.** This closes the scoped R1 implementation in the [main roadmap](../../architecture/DarkAngel_Implementation_Handoff.md#recommended-development-order-15-october-2026). Verification uses synthetic ImGui input and D3D12 captures on this machine, with visual review; no independent physical mouse/keyboard hand test, Vulkan run, remote multiplayer or EOS qualification is claimed. R2 remains Planned.

## Delivered behavior

- **Orbit**: right drag or Alt+left drag around the selected object's bounds centre; without a selection, retain the last pivot. Middle drag pans, wheel zooms. Pitch and minimum distance prevent flipping or crossing the pivot.
- **Fly**: hold right mouse to look; WASD moves, Q/E move down/up, Shift moves four times faster. Wheel while holding right mouse adjusts fly speed; otherwise wheel dollies. Diagonal input is normalized.
- Visible **Orbit/Fly** switch, **Focus (F)** button, speed control (0.1–2,000 m/s, default 5), **Reset speed**, and **Navigation ?** shortcut help. **Shift+F** switches mode without moving/rotating the view; **F** frames selection with aspect-aware bounds fitting; **Esc** releases navigation.
- Model bounds account for authored placement transform/scale. NPCs and encounter volumes have bounded focus targets; other marker-only objects use a small fallback. Selection is single-object, matching the existing editor. Multi-selection is not implemented here.
- Navigation begins on a press inside the viewport away from the gizmo. A drag may continue outside it, then releases correctly. Text input, popups, asset drag/drop, active gizmo gestures, focus loss, workspace switches and Play prevent conflicting camera control. Ordinary left-click picking and Place gestures remain available.
- Camera pose/mode/speed/pivot are remembered per scene within the editor session. They are editor presentation state, absent from scene files and gameplay saves. Persistent user preferences remain R2 work; this is not another authored camera rig.

## Implementation boundaries

`apps/editor/scene_camera.*` owns dependency-light camera math and navigation ownership. `scene_navigation.cpp` adapts existing Shell selection and ImGui input, supplies the toolbar and matrices, and remembers per-scene session state. `ViewportTools` supplies cooked model/volume bounds. Renderer, picking, placement and overlays use the updated Scene matrices; rendering and mouse tools share the same projection during resize. Play and Character/Composer/Player/VFX camera paths retain their existing owners.

The Scene projection uses a 0.05 m near plane and 10,000 m far plane. The original candidate used 200,000 m, which made placement rays exceed the existing physics query coordinate/delta bounds. The fix reduced the editor projection range; it did not raise/bypass physics guards or change runtime code. Scene sources, serialization, assembly identities, collision products and gameplay cameras are unchanged by navigation.

## Verification and evidence

- Guarded supported-profile configure/build of **DarkAngelEditor + SceneCameraTests** passes: [configure](../evidence/r1-final-configure.log), [dependency scan](../evidence/r1-final-dyndep.log), [build plan](../evidence/r1-final-plan.log), [build](../evidence/r1-final-build.log). Final confirmation build took 41.1 seconds (the preceding candidate build took 148.8 seconds); this is incremental, not a benchmark claim.
- Pinned MSVC 14.51.36231, Windows SDK 10.0.26100.0, bundled CMake 4.4.4; `VCPKG_MANIFEST_INSTALL=OFF`, `D3D12_SUPPORTED=TRUE`. Build/editor activity was serialized; no third-party objects were rebuilt or dependency/build trees deleted.
- **3/3 native checks**: new SceneCameraTests (also registered as `R1.scene_camera`), NativeInputTests, and existing gameplay CameraTests. Cover view continuity, aspect-aware framing, orbit radius, pan/zoom bounds, speed, diagonal movement/Shift, empty selection and text/popup/gizmo/focus/Play ownership guards.
- **3/3 D3D12 editor gates**: new `--exercise-scene-camera`, existing `--exercise-place-mouse`, existing `--exercise-camera-look`. The new exercise drives toolbar, keyboard and mouse events; verifies orbit/fly/pan/zoom/speed/focus, unchanged scene revision during camera use, picking and a real Place drag after navigation, text/empty-selection/Play isolation, and per-scene restoration through Save/new scene/reopen.
- [Consolidated receipt](../evidence/r1-scene-navigation.json), [exact successful run](../evidence/r1-scene-navigation-1e1799c952.json), [navigation capture](../evidence/r1-7c5eace16d-navigation.png), [existing Place capture](../evidence/r1-7c5eace16d-place.png). Captures were inspected for control visibility and viewport/selection presentation.
- All **26 initially protected Royal scene/record files** retain their hashes. The verifier copies both source and asset cache into a disposable `.cache/r1-navigation-*` fixture; original authoring fixtures and protected project sources are not edited.

Reproduce after building the two targets with the pinned environment:

```powershell
python scripts/verify_scene_navigation.py
```

It uses the existing `build/m5-editor-relwithdebinfo/authoring-fixture.json` seed, runs checks serially and writes a unique receipt/log/capture set. `--gate navigation`, `--gate place`, and `--gate game-camera` support focused diagnosis. A full pass publishes the consolidated receipt; failed receipts are retained.

## Failures retained and resolved

1. The first dry-run plan stopped because the new test target's `CXX.dd` did not exist: [initial plan](../evidence/r1-build-plan.log). Regenerated the relevant SceneCameraTests/DarkAngelEditor dyndep targets with the pinned environment; no tree deletion or dependency repair was needed.
2. Candidate 2 compiled successfully: [build](../evidence/r1-candidate-2-build.log). Native tests passed, but navigation and existing Place gates refused drops because the 200,000 m rays exceeded native bounds: [navigation failure](../evidence/r1-7ec4ecd027-navigation.log), [Place failure](../evidence/r1-8d50c7b953-place.log). Candidate 3 fixes the projection range and passes both.
3. A deliberately shortened diagnostic run captured the state before the failing placement check and ended with “exercise ended before acceptance”: [diagnostic capture](../evidence/r1-7f3171f71c-navigation.png), [log](../evidence/r1-7f3171f71c-navigation.log). It is not passing evidence.

Existing compiler warnings in older editor code remain; the new adapter's member-shadowing warning was removed. No full CTest, reduced-feature, provider, device or network matrix was rerun for this editor-only change. Those historical/deferred qualification obligations remain unchanged. No commit or push was performed.
