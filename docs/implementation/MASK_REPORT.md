# Masks: native asset, four Player slots, active-mask kit and worn visual - 10 October 2026

User direction: masks take over what stances did; each mask is worn visibly on a face socket; the mask model comes from the Unity project. M4/M5 remain In progress. This is the first mask slice, not the complete mask system (see Not delivered).

## Delivered

- **Mask asset** (`.dafacemask`, kind `mask`): display name, the Combat Kit it owns (eight slots, dodge, death timeline, effects, locomotion graph), optional worn visual (an imported static model), tint, and an attachment (rig socket name, offset in metres, rotation in degrees, scale). It is a full native source type: catalogued, draft/Undo/Redo, private validation, coordinated Save, consumer revalidation when its kit or model changes, frozen product `mask.json` with its closure.
- **Player loadout**: optional `masks` (exactly four slots, each a mask or empty, a mask at most once) and `active_mask` (must point at an equipped slot). The base `kit` stays and is used when no mask is equipped. Every equipped mask's kit must share the Player's attribute schema, input profile and rig. `load_cooked_player` resolves the active mask's kit as the Player's effective starting kit.
- **Worn visual**: fresh Play resources carry the active mask's model, tint and socket joint; the editor draws it on the wearing actor each frame from the same pose the skin uses (owner pose in Game). Mask visuals are packaged as loadable roots of the Play registry. Presentation only.
- **Authoring**: Player workspace lists Players and Masks. Player form: four mask slots (Up/Right/Down/Left) with pick, "Starts active", Edit, Unequip, and "Create mask from base kit". Mask form: name, kit (with Edit kit), model, tint, socket, offset, rotation, scale. Commands: `create_mask`, `equip_mask`, `select_mask`, `worn_visuals`.
- **Owned content**: `content/masks/horned-mask.glb` converted read-only from `Assets/thirdparty/3D/MelvinMasks/HornedMask.fbx` (the only mask model in the Unity project; its prefabs are variants of it), Ember/Earth/Water/Air masks tinted and placed on the `head` socket (offset 0.04, 0.11, 0 m; rotation 0, -90, 0; scale 0.01 because the FBX is in centimetres), `characters/royal_human.dacharacter` and `players/royal_player.daplayer` with the four masks equipped and Ember active. All four masks currently reference the same Royal kit.
- **Tuning aid**: editor flag `--mask-tune px,py,pz,rx,ry,rz,scale` overrides the worn transform and also shows the mask on the camera-facing target, used to find the placement by capture.

## Decisions

- The face attachment uses the rig's existing `head` socket plus a per-mask offset. A dedicated `face_mask` socket on the canonical skeleton was not added because that file's hash and rig signature fence every converted clip and skin; changing it would invalidate all of them. A Character-level attachment table can replace the per-mask offset later.
- Mask is its own asset rather than an embedded loadout entry so Players and later NPCs can share masks.

## Verification

`python scripts/verify_effect_reaction.py --build`: build plus fourteen gates pass ([receipt](evidence/effect-reaction.json)). NativeAuthoringTests mask block: creation, equip/select/unequip with Undo/Redo, duplicate/empty/out-of-range rejection, out-of-range tint and invalid socket rejected with nothing published, private validation, coordinated Save, frozen Player resolving the active mask's kit and four slots, a mask without a model giving no visual, changing the starting mask changing kit and visual while the earlier package stays unchanged, attachment values surviving the cook, and fresh isolated Play with the masked Player (authored Heavy, target Health 60 at tick 30, pending 0, scene unchanged). D3D12: [worn on the player](evidence/mask-worn-d3d12.png) and [placement view](evidence/mask-placement-d3d12.png) (tuning flag, mask also on the target); both inspected: the Ember-tinted mask covers the face with horns up.

## Not delivered

- Switching the active mask during Play (authoritative kit replacement with a client intent) - next.
- Distinct kits per mask, equipped versus selected grants/effects, stance movement/jump/dodge overrides, mask switch VFX, auras, HUD feedback.
- Masks on other actors (the target has no loadout), masks in the Character/Animation previews, Vulkan check.
- The model still uses the converter's checker test material; only the tint distinguishes elements.
- The default Royal launcher still starts kit-based Play; choose "Use Player for next fresh Play" on `royal_player` (or pass `--character-clips player:<id> --player-asset <id>`) to play masked.
