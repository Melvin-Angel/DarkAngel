# Lighting authoring repair and defaults promotion

18 October 2026. Local implementation; no commit or push.

## Root cause

The Lighting tab reads `World::optional_json["rendering"]`, resolved from the
scene entity's `optional.rendering`. `scene_set_component` instead wrote a
top-level `rendering` field, which SceneAssembly never transfers into the native
world. Thus the document changed but the next UI frame reloaded the previous
resolved settings. The light inspector and creation of a new rendering-settings
object had the same native-component/top-level-role mismatch.

## Repair

`render/scene_rendering_authoring.hpp` centralizes optional-component writes
for rendering and lights. Both creation and edits use it through the existing
scene document transaction. Unrelated fields/components are preserved; ignored
top-level duplicates are retired when that component is edited. Gameplay roles
continue using their existing top-level storage. This retains normal scene
Undo/Redo, dirty state and Save rather than bypassing document authority.

Lighting's scene mode now has **Apply scene overrides to project defaults**.
It copies the current render settings and toon material defaults through the
existing `rendering/project.darendering` writer. It retains scene overrides and
project material tint/roughness/metallic/opt-in. Scene-specific main-light object
identity is excluded. The button is disabled during Play or without a project
asset source. Project defaults remain immediate writes as in the existing UI;
scene edits still require the normal scene Save to persist across editor runs.

## Verification

`LightingAuthoringTests` passed next-frame resolved-world edits over eight
successive transactions, unrelated light preservation, ignored duplicate
cleanup, scene Undo/Redo, Save/reopen, native component creation/removal and
defaults promotion/disk round trip. Test data is disposable under the system
temporary directory, not authored project content. R7RenderingTests and
PreviewRoutingTests also passed.

Final editor build passed; Ninja reports no remaining work for the editor and
LightingAuthoringTests. Post-build D3D12 Animation preview smoke passed with
21,178 changed playback pixels and protected sources unchanged:
`artifacts/viewport-output-f6bdd14513/receipt.json`. No manual native-UI
interaction qualification is claimed by the functional tests above.
